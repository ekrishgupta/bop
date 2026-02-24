#pragma once

#include <functional>
#include <ixwebsocket/IXWebSocket.h>
#include <string>
#include <vector>

namespace bop {

/**
 * @brief Base class for WebSocket clients.
 * This provides a standard interface for connecting to exchange WebSocket
 * feeds.
 */
class WebSocketClient {
public:
  virtual ~WebSocketClient() = default;

  virtual void connect(const std::string &url) = 0;
  virtual void disconnect() = 0;
  virtual bool is_connected() const = 0;

  // Send a raw message (e.g., subscription JSON)
  virtual void send(const std::string &message) = 0;

  // Set callbacks for various events
  virtual void on_open(std::function<void()> cb) = 0;
  virtual void on_close(std::function<void()> cb) = 0;
  virtual void on_error(std::function<void(const std::string &)> cb) = 0;
  virtual void on_message(std::function<void(const std::string &)> cb) = 0;

  // Convenience methods for common exchange patterns
  virtual void subscribe(const std::string &channel,
                         const std::vector<std::string> &symbols) = 0;
};

/**
 * @brief Production-grade implementation of a WebSocket client using IXWebSocket.
 */
class ProductionWebSocketClient : public WebSocketClient {
public:
  ProductionWebSocketClient() {
    ws_.setOnMessageCallback([this](const ix::WebSocketMessagePtr &msg) {
      if (msg->type == ix::WebSocketMessageType::Message) {
        if (message_cb_)
          message_cb_(msg->str);
      } else if (msg->type == ix::WebSocketMessageType::Open) {
        if (open_cb_)
          open_cb_();
      } else if (msg->type == ix::WebSocketMessageType::Close) {
        if (close_cb_)
          close_cb_();
      } else if (msg->type == ix::WebSocketMessageType::Error) {
        if (error_cb_)
          error_cb_(msg->errorInfo.reason);
      }
    });

    // Configure production-grade features:
    // 1. Automated Heartbeats (Ping/Pong)
    ws_.setPingInterval(30);

    // 2. Automatic Reconnection logic
    ws_.enableAutomaticReconnection();
    ws_.setReconnectionDelay(1000);    // Start with 1s
    ws_.setMaxReconnectionDelay(30000); // Max 30s
  }

  void connect(const std::string &url) override {
    ws_.setUrl(url);
    ws_.start();
  }

  void disconnect() override { ws_.stop(); }

  bool is_connected() const override {
    return ws_.getReadyState() == ix::ReadyState::Open;
  }

  void send(const std::string &message) override { ws_.send(message); }

  void on_open(std::function<void()> cb) override { open_cb_ = cb; }
  void on_close(std::function<void()> cb) override { close_cb_ = cb; }
  void on_error(std::function<void(const std::string &)> cb) override {
    error_cb_ = cb;
  }
  void on_message(std::function<void(const std::string &)> cb) override {
    message_cb_ = cb;
  }

  void subscribe(const std::string &channel,
                 const std::vector<std::string> &symbols) override {
    // Exchange-specific subscription logic is typically handled in backends,
    // but this provides a hook if needed.
  }

private:
  ix::WebSocket ws_;
  std::function<void()> open_cb_;
  std::function<void()> close_cb_;
  std::function<void(const std::string &)> error_cb_;
  std::function<void(const std::string &)> message_cb_;
};

} // namespace bop
