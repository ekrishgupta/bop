#pragma once

#include <functional>
#include <iostream>
#include <memory>
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
 * @brief Default mock implementation of a WebSocket client.
 */
class MockWebSocketClient : public WebSocketClient {
public:
  void connect(const std::string &url) override {
    connected_ = true;
    std::cout << "[WS] Connected to " << url << std::endl;
    if (open_cb_)
      open_cb_();
  }

  void disconnect() override {
    connected_ = false;
    std::cout << "[WS] Disconnected" << std::endl;
    if (close_cb_)
      close_cb_();
  }

  bool is_connected() const override { return connected_; }

  void send(const std::string &message) override {
    std::cout << "[WS] Sending: " << message << std::endl;
  }

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
    std::cout << "[WS] Subscribing to channel: " << channel << " for symbols: ";
    for (const auto &s : symbols)
      std::cout << s << " ";
    std::cout << std::endl;
  }

  // Simulation tool: trigger a message
  void simulate_message(const std::string &msg) {
    if (message_cb_)
      message_cb_(msg);
  }

private:
  bool connected_ = false;
  std::function<void()> open_cb_;
  std::function<void()> close_cb_;
  std::function<void(const std::string &)> error_cb_;
  std::function<void(const std::string &)> message_cb_;
};

} // namespace bop
