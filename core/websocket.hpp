#pragma once

#include <atomic>
#include <boost/asio/connect.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace websocket = beast::websocket; // from <boost/beast/websocket.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
namespace ssl = boost::asio::ssl;       // from <boost/asio/ssl.hpp>
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>

namespace bop {

/**
 * @brief Base class for WebSocket clients.
 */
class WebSocketClient {
public:
  virtual ~WebSocketClient() = default;

  virtual void connect(const std::string &url) = 0;
  virtual void disconnect() = 0;
  virtual bool is_connected() const = 0;

  virtual void send(const std::string &message) = 0;

  virtual void on_open(std::function<void()> cb) = 0;
  virtual void on_close(std::function<void()> cb) = 0;
  virtual void on_error(std::function<void(const std::string &)> cb) = 0;
  virtual void on_message(std::function<void(std::string_view)> cb) = 0;

  virtual void subscribe(const std::string &channel,
                         const std::vector<std::string> &symbols) = 0;
};

/**
 * @brief Production-grade implementation of a WebSocket client using
 * Boost.Beast. This class handles the actual TCP/TLS handshake and provides a
 * background thread to process incoming messages so the DSL can react in
 * real-time.
 */
class LiveWebSocketClient : public WebSocketClient {
public:
  LiveWebSocketClient()
      : work_guard_(net::make_work_guard(ioc_)), resolver_(ioc_.get_executor()),
        ws_(ioc_.get_executor(), ctx_) {
    // Configure SSL context
    ctx_.set_default_verify_paths();
    ctx_.set_verify_mode(
        ssl::verify_none); // In production, use ssl::verify_peer
  }

  ~LiveWebSocketClient() { disconnect(); }

  void connect(const std::string &url) override {
    if (is_running_)
      return;
    url_ = url;
    is_running_ = true;
    retry_count_ = 0;

    // Start the background thread
    io_thread_ = std::thread([this]() {
      try {
        ioc_.run();
      } catch (const std::exception &e) {
        if (error_cb_)
          error_cb_(std::string("IO Context Error: ") + e.what());
      }
    });

    do_connect();
  }

  void do_connect() {
    std::string host, port, path;
    parse_url(url_, host, port, path);
    host_ = host;
    path_ = path;

    // Resolve the host
    resolver_.async_resolve(
        host, port,
        beast::bind_front_handler(&LiveWebSocketClient::on_resolve, this));
  }

  void disconnect() override {
    is_running_ = false;
    work_guard_.reset(); // Allow ioc_.run() to exit when no work is left
    reconnect_timer_.cancel();

    if (ws_.is_open()) {
      beast::error_code ec;
      ws_.close(websocket::close_code::normal, ec);
    }

    ioc_.stop();

    if (io_thread_.joinable()) {
      io_thread_.join();
    }
  }

  bool is_connected() const override { return ws_.is_open(); }

  void send(const std::string &message) override {
    auto msg = std::make_shared<std::string>(message);
    ws_.async_write(net::buffer(*msg),
                    [this, msg](beast::error_code ec, std::size_t) {
                      if (ec) {
                        fail(ec, "write");
                      }
                    });
  }

  void on_open(std::function<void()> cb) override { open_cb_ = cb; }
  void on_close(std::function<void()> cb) override { close_cb_ = cb; }
  void on_error(std::function<void(const std::string &)> cb) override {
    error_cb_ = cb;
  }
  void on_message(std::function<void(std::string_view)> cb) override {
    message_cb_ = cb;
  }

  void subscribe(const std::string &channel,
                 const std::vector<std::string> &symbols) override {
    // Subscription logic typically handled by sending JSON via send()
  }

private:
  void parse_url(const std::string &url, std::string &host, std::string &port,
                 std::string &path) {
    size_t start = 0;
    if (url.substr(0, 6) == "wss://") {
      start = 6;
      port = "443";
    } else if (url.substr(0, 5) == "ws://") {
      start = 5;
      port = "80";
    }

    size_t slash = url.find('/', start);
    std::string host_port = (slash == std::string::npos)
                                ? url.substr(start)
                                : url.substr(start, slash - start);
    path = (slash == std::string::npos) ? "/" : url.substr(slash);

    size_t colon = host_port.find(':');
    if (colon != std::string::npos) {
      host = host_port.substr(0, colon);
      port = host_port.substr(colon + 1);
    } else {
      host = host_port;
    }
  }

  void on_resolve(beast::error_code ec, tcp::resolver::results_type results) {
    if (ec)
      return fail(ec, "resolve");

    beast::get_lowest_layer(ws_).expires_after(std::chrono::seconds(30));
    beast::get_lowest_layer(ws_).async_connect(
        results,
        beast::bind_front_handler(&LiveWebSocketClient::on_connect, this));
  }

  void on_connect(beast::error_code ec,
                  tcp::resolver::results_type::endpoint_type ep) {
    if (ec)
      return fail(ec, "connect");

    beast::get_lowest_layer(ws_).expires_after(std::chrono::seconds(30));

    // SSL Handshake
    ws_.next_layer().async_handshake(
        ssl::stream_base::client,
        beast::bind_front_handler(&LiveWebSocketClient::on_ssl_handshake,
                                  this));
  }

  void on_ssl_handshake(beast::error_code ec) {
    if (ec)
      return fail(ec, "ssl_handshake");

    beast::get_lowest_layer(ws_).expires_never();

    // Set suggested timeout settings for the websocket
    ws_.set_option(
        websocket::stream_base::timeout::suggested(beast::role_type::client));

    // Set a decorator to change the User-Agent
    ws_.set_option(
        websocket::stream_base::decorator([](websocket::request_type &req) {
          req.set(http::field::user_agent, "BOP-DSL-Client/1.0");
        }));

    // WebSocket Handshake
    ws_.async_handshake(
        host_, path_,
        beast::bind_front_handler(&LiveWebSocketClient::on_handshake, this));
  }

  void on_handshake(beast::error_code ec) {
    if (ec)
      return fail(ec, "handshake");

    retry_count_ = 0; // Reset retry count upon successful connection
    if (open_cb_)
      open_cb_();
    do_read();
  }

  void do_read() {
    ws_.async_read(buffer_, beast::bind_front_handler(
                                &LiveWebSocketClient::on_read, this));
  }

  void on_read(beast::error_code ec, std::size_t) {
    if (ec) {
      if (ec == websocket::error::closed) {
        if (close_cb_)
          close_cb_();
      }
      return fail(ec, "read");
    }

    if (message_cb_) {
      auto data = buffer_.data();
      message_cb_(std::string_view(static_cast<const char *>(data.data()),
                                   data.size()));
    }
    buffer_.consume(buffer_.size());
    do_read();
  }

  void fail(beast::error_code ec, char const *what) {
    if (!is_running_)
      return;

    if (error_cb_) {
      error_cb_(std::string(what) + ": " + ec.message());
    }

    // Don't reconnect if it was a clean close unless we're still "running"
    if (is_running_) {
      schedule_reconnect();
    }
  }

  void schedule_reconnect() {
    // Calculate backoff: 2^retry_count * 500ms, max 30s
    int delay_ms = std::min(30000, (1 << std::min(retry_count_, 10)) * 500);
    retry_count_++;

    reconnect_timer_.expires_after(std::chrono::milliseconds(delay_ms));
    reconnect_timer_.async_wait([this](beast::error_code ec) {
      if (!ec && is_running_) {
        // Re-initialize websocket stream to clear internal state
        // We need to recreate the stream because some beast states are sticky
        // after error
        ws_ = websocket::stream<ssl::stream<beast::tcp_stream>>(
            ioc_.get_executor(), ctx_);
        do_connect();
      }
    });
  }

  net::io_context ioc_;
  net::executor_work_guard<net::io_context::executor_type> work_guard_;
  ssl::context ctx_{ssl::context::tlsv12_client};
  tcp::resolver resolver_;
  websocket::stream<ssl::stream<beast::tcp_stream>> ws_;
  beast::flat_buffer buffer_;
  std::string host_;
  std::string path_;
  std::string url_;
  net::steady_timer reconnect_timer_{ioc_.get_executor()};
  int retry_count_ = 0;
  std::thread io_thread_;
  std::atomic<bool> is_running_{false};

  std::function<void()> open_cb_;
  std::function<void()> close_cb_;
  std::function<void(const std::string &)> error_cb_;
  std::function<void(std::string_view)> message_cb_;
};

/**
 * @brief A WebSocket client that does nothing.
 * Useful for exchanges that only support HTTP polling.
 */
class NullWebSocketClient : public WebSocketClient {
public:
  void connect(const std::string &) override {}
  void disconnect() override {}
  bool is_connected() const override { return false; }
  void send(const std::string &) override {}
  void on_open(std::function<void()>) override {}
  void on_close(std::function<void()>) override {}
  void on_error(std::function<void(const std::string &)>) override {}
  void on_message(std::function<void(std::string_view)>) override {}
  void subscribe(const std::string &,
                 const std::vector<std::string> &) override {}
};

} // namespace bop
