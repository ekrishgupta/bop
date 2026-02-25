#pragma once

#include "nlohmann/json.hpp"
#include <curl/curl.h>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace bop {

using json = nlohmann::json;

class HttpClient {
public:
  HttpClient() { curl_global_init(CURL_GLOBAL_DEFAULT); }

  ~HttpClient() {
    for (CURL *handle : curl_pool_) {
      curl_easy_cleanup(handle);
    }
    curl_global_cleanup();
  }

  struct Response {
    long status_code;
    std::string body;
    json json_body() const { return json::parse(body); }
  };

  Response get(const std::string &url,
               const std::map<std::string, std::string> &headers = {}) {
    return request(url, "GET", "", headers);
  }

  Response post(const std::string &url, const std::string &payload,
                const std::map<std::string, std::string> &headers = {}) {
    return request(url, "POST", payload, headers);
  }

private:
  std::vector<CURL *> curl_pool_;
  std::mutex pool_mutex_;

  CURL *acquire_handle() {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    if (!curl_pool_.empty()) {
      CURL *handle = curl_pool_.back();
      curl_pool_.pop_back();
      curl_easy_reset(handle);
      return handle;
    }
    CURL *handle = curl_easy_init();
    if (!handle)
      throw std::runtime_error("Failed to initialize CURL");
    return handle;
  }

  void release_handle(CURL *handle) {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    curl_pool_.push_back(handle);
  }

  static size_t WriteCallback(void *contents, size_t size, size_t nmemb,
                              void *userp) {
    ((std::string *)userp)->append((char *)contents, size * nmemb);
    return size * nmemb;
  }

  Response request(const std::string &url, const std::string &method,
                   const std::string &payload,
                   const std::map<std::string, std::string> &headers) {
    CURL *curl = acquire_handle();

    std::string readBuffer;
    struct curl_slist *chunk = nullptr;

    for (const auto &[key, value] : headers) {
      std::string header = key + ": " + value;
      chunk = curl_slist_append(chunk, header.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);

    if (method == "POST") {
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
    } else if (method == "DELETE") {
      curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    }

    CURLcode res = curl_easy_perform(curl);
    long response_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

    if (res != CURLE_OK) {
      std::string err = curl_easy_strerror(res);
      curl_slist_free_all(chunk);
      release_handle(curl);
      throw std::runtime_error("CURL request failed: " + err);
    }

    curl_slist_free_all(chunk);
    release_handle(curl);

    return {response_code, readBuffer};
  }
};

static HttpClient Network;

} // namespace bop
