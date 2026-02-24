#pragma once

#include <condition_variable>
#include <functional>
#include <ixwebsocket/IXWebSocket.h>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
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
 * Includes a background processing thread and message queue for high-frequency
 * buffer management.
 */
class LiveWebSocketClient : public WebSocketClient {
public:
  LiveWebSocketClient() : running_(true) {
    ws_.setOnMessageCallback([this](const ix::WebSocketMessagePtr &msg) {
      if (msg->type == ix::WebSocketMessageType::Message) {
        {
          std::lock_guard<std::mutex> lock(queue_mutex_);
          message_queue_.push(msg->str);
        }
        queue_cv_.notify_one();
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
    // 1. Automated Heartbeats (Ping/Pong) - Kalshi requires ~10s
    ws_.setPingInterval(10);

    // 2. Automatic Reconnection logic
    ws_.enableAutomaticReconnection();
    ws_.setReconnectionDelay(1000);     // Start with 1s
    ws_.setMaxReconnectionDelay(30000); // Max 30s

    // 3. Background Processing Thread for Buffer Management
    processing_thread_ = std::thread([this]() { this->process_messages(); });
  }

  ~LiveWebSocketClient() {
    running_ = false;
    queue_cv_.notify_all();
    if (processing_thread_.joinable()) {
      processing_thread_.join();
    }
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
    // Exchange-specific subscription logic is typically handled in backends
  }

private:
  void process_messages() {
    while (running_) {
      std::string msg;
      {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        queue_cv_.wait(lock,
                       [this]() { return !message_queue_.empty() || !running_; });

        if (!running_ && message_queue_.empty())
          break;

        msg = std::move(message_queue_.front());
        message_queue_.pop();
      }

      if (message_cb_) {
        message_cb_(msg);
      }
    }
  }

  ix::WebSocket ws_;
  std::function<void()> open_cb_;
  std::function<void()> close_cb_;
  std::function<void(const std::string &)> error_cb_;
  std::function<void(const std::string &)> message_cb_;

  // Buffer Management
  std::queue<std::string> message_queue_;
  std::mutex queue_mutex_;
  std::condition_variable queue_cv_;
  std::thread processing_thread_;
  std::atomic<bool> running_;
};

} // namespace bop
