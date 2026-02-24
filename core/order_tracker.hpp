#pragma once

#include "core.hpp"
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace bop {

enum class OrderStatus { Pending, Open, PartiallyFilled, Filled, Cancelled, Rejected };

struct OrderRecord {
  std::string id;
  Order order;
  OrderStatus status = OrderStatus::Pending;
  int filled_qty = 0;
  Price avg_fill_price = Price(0);
};

class OrderTracker {
  std::map<std::string, OrderRecord> records;
  std::mutex mtx;

public:
  void track(const std::string &id, const Order &o) {
    if (id == "" || id == "error")
      return;
    std::lock_guard<std::mutex> lock(mtx);
    records[id] = {id, o, OrderStatus::Open, 0, Price(0)};
  }

  void update_status(const std::string &id, OrderStatus status) {
    std::lock_guard<std::mutex> lock(mtx);
    if (records.count(id)) {
      records[id].status = status;
    }
  }

  void add_fill(const std::string &id, int qty, Price price) {
    std::lock_guard<std::mutex> lock(mtx);
    if (records.count(id)) {
      auto &r = records[id];
      double total_cost =
          r.avg_fill_price.to_double() * r.filled_qty + price.to_double() * qty;
      r.filled_qty += qty;
      r.avg_fill_price = Price::from_double(total_cost / r.filled_qty);
      if (r.filled_qty >= r.order.quantity) {
        r.status = OrderStatus::Filled;
      } else {
        r.status = OrderStatus::PartiallyFilled;
      }
    }
  }

  size_t count_open(MarketId market) {
    std::lock_guard<std::mutex> lock(mtx);
    size_t count = 0;
    for (auto const &[id, r] : records) {
      if (r.order.market.hash == market.hash &&
          (r.status == OrderStatus::Open || r.status == OrderStatus::Pending ||
           r.status == OrderStatus::PartiallyFilled)) {
        count++;
      }
    }
    return count;
  }

  std::vector<OrderRecord> get_all() {
    std::lock_guard<std::mutex> lock(mtx);
    std::vector<OrderRecord> result;
    for (auto const &[id, record] : records) {
      result.push_back(record);
    }
    return result;
  }
};

extern OrderTracker GlobalOrderTracker;

} // namespace bop
