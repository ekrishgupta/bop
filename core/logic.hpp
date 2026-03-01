#pragma once

#include "core.hpp"
#include "market_base.hpp"
#include <functional>
#include <immintrin.h>
#include <memory_resource>
#include <vector>

namespace bop {

// Query Tags
struct PriceTag {};
struct VolumeTag {};
struct PositionTag {};
struct BalanceTag {};
struct ExposureTag {};
struct PnLTag {};
struct SpreadTag {};
struct DepthTag {};
struct MidpointTag {};
struct FairPriceTag {};
struct OpenOrdersTag {};
struct PortfolioTag {};
struct TimeTag {};
struct TicksTag {};

// Unit traits for Tags
template <typename Tag> struct TagUnit {
  using type = int64_t;
};
template <> struct TagUnit<PriceTag> {
  using type = Price;
};
template <> struct TagUnit<VolumeTag> {
  using type = Shares;
};
template <> struct TagUnit<PositionTag> {
  using type = Shares;
};
template <> struct TagUnit<BalanceTag> {
  using type = Price;
};
template <> struct TagUnit<PnLTag> {
  using type = Price;
};
template <> struct TagUnit<ExposureTag> {
  using type = Price;
};
template <> struct TagUnit<SpreadTag> {
  using type = Price;
};
template <> struct TagUnit<DepthTag> {
  using type = Shares;
};
template <> struct TagUnit<MidpointTag> {
  using type = Price;
};
template <> struct TagUnit<FairPriceTag> {
  using type = Price;
};
template <> struct TagUnit<TicksTag> {
  using type = Ticks;
};

struct RiskAction {
  enum class Type { CancelAll, ClosePositions, Composite };
  Type type;
  std::pmr::vector<Type> sub_actions;

  RiskAction(Type t,
             std::pmr::memory_resource *mr = std::pmr::get_default_resource())
      : type(t), sub_actions(mr) {}
};

inline RiskAction CancelAll() {
  return RiskAction(RiskAction::Type::CancelAll);
}
inline RiskAction ClosePositions() {
  return RiskAction(RiskAction::Type::ClosePositions);
}

inline RiskAction operator|(RiskAction a, RiskAction b) {
  RiskAction r(RiskAction::Type::Composite,
               a.sub_actions.get_allocator().resource());
  if (a.type == RiskAction::Type::Composite)
    r.sub_actions = a.sub_actions;
  else
    r.sub_actions.push_back(a.type);
  if (b.type == RiskAction::Type::Composite)
    r.sub_actions.insert(r.sub_actions.end(), b.sub_actions.begin(),
                         b.sub_actions.end());
  else
    r.sub_actions.push_back(b.type);
  return r;
}

struct RiskViolationTrigger {};
inline RiskViolationTrigger OnRiskViolation() { return {}; }

struct StrategyProxy {
  template <typename T> void invariant(T &&condition);
};
extern StrategyProxy Strategy;

struct RiskQuery {
  enum class Type { Exposure, PnL };
  Type type;
};

// --- Stateful Strategy Triggers ---
struct Trigger {
  virtual ~Trigger() = default;
  virtual bool evaluate(const ExecutionEngine &engine) = 0;
  virtual bool is_recurring() const { return false; }

  // PMR allocation helper
  void *operator new(size_t size, std::pmr::memory_resource *mr) {
    return mr->allocate(size);
  }
  void operator delete(void *ptr, std::pmr::memory_resource *mr) {
    mr->deallocate(ptr, 0); // Size info might be needed for some MRs
  }
  void operator delete(void *ptr) {
    // This is tricky with PMR if we don't know the MR.
    // Usually we'd use a custom deleter.
  }
};

struct OnFillTrigger : public Trigger {
  std::pmr::string order_id;
  bool filled = false;
  OnFillTrigger(std::string_view id, std::pmr::memory_resource *mr =
                                         std::pmr::get_default_resource())
      : order_id(id, mr) {}
  bool evaluate(const ExecutionEngine &engine) override;
};

struct TimerTrigger : public Trigger {
  std::chrono::system_clock::time_point next_trigger;
  std::chrono::milliseconds interval;
  bool recurring;
  TimerTrigger(std::chrono::milliseconds d, bool r = false)
      : interval(d), recurring(r) {
    next_trigger = std::chrono::system_clock::now() + d;
  }
  bool evaluate(const ExecutionEngine &engine) override {
    auto now = std::chrono::system_clock::now();
    if (now >= next_trigger) {
      if (recurring) {
        next_trigger = now + interval;
      }
      return true;
    }
    return false;
  }
  bool is_recurring() const override { return recurring; }
};

// --- Stateful Strategy Actions ---
struct Action {
  virtual ~Action() = default;
  virtual void execute(ExecutionEngine &engine) = 0;

  void *operator new(size_t size, std::pmr::memory_resource *mr) {
    return mr->allocate(size);
  }
  void operator delete(void *ptr, std::pmr::memory_resource *mr) {
    mr->deallocate(ptr, 0);
  }
};

struct OrderAction : public Action {
  Order order;
  OrderAction(Order o) : order(std::move(o)) {}
  void execute(ExecutionEngine &engine) override;
};

struct CancelAction : public Action {
  std::pmr::string order_id;
  CancelAction(std::string_view id,
               std::pmr::memory_resource *mr = std::pmr::get_default_resource())
      : order_id(id, mr) {}
  void execute(ExecutionEngine &engine) override;
};

struct CallbackAction : public Action {
  std::function<void(ExecutionEngine &)> callback;
  CallbackAction(std::function<void(ExecutionEngine &)> cb)
      : callback(std::move(cb)) {}
  void execute(ExecutionEngine &engine) override { callback(engine); }
};

struct WorkflowStep {
  Trigger *trigger;
  Action *action;
  std::pmr::memory_resource *mr;

  WorkflowStep(
      Trigger *t, Action *a,
      std::pmr::memory_resource *resource = std::pmr::get_default_resource())
      : trigger(t), action(a), mr(resource) {}
};

struct BalanceQuery {};
struct PortfolioQuery {
  enum class Metric {
    TotalDelta,
    TotalGamma,
    TotalTheta,
    TotalVega,
    NetExposure,
    PortfolioValue
  };
  Metric metric;
};

template <typename Tag> struct MarketQuery {
  MarketId market;
  bool outcome_yes;
  const MarketBackend *backend = nullptr;
  bool is_universal = false;

  inline MarketQuery<Tag> count() const { return *this; }
};

template <typename Tag>
inline SyntheticMarketQuery<Tag> operator-(MarketQuery<Tag> l,
                                           MarketQuery<Tag> r) {
  return {std::make_shared<MarketQuery<Tag>>(l),
          std::make_shared<MarketQuery<Tag>>(r), MathOp::Sub};
}

template <typename Tag>
inline SyntheticMarketQuery<Tag> operator+(MarketQuery<Tag> l,
                                           MarketQuery<Tag> r) {
  return {std::make_shared<MarketQuery<Tag>>(l),
          std::make_shared<MarketQuery<Tag>>(r), MathOp::Add};
}

// Forward declaration of composite conditions
template <typename L, typename R> struct AndCondition;
template <typename L, typename R> struct OrCondition;

struct MarketTarget {
  MarketId market;
  const MarketBackend *backend = nullptr;
  bool is_universal = false;

  MarketTarget resolve() const {
    if (backend && !market.resolved) {
      std::string id = backend->resolve_ticker(market.ticker);
      if (id != market.ticker) {
        return {MarketId(fnv1a(id.c_str()), id, true), backend, is_universal};
      }
    }
    return *this;
  }

  inline MarketTarget Universal() const {
    auto r = *this;
    r.is_universal = true;
    return r;
  }

  inline MarketQuery<PriceTag> Price(YES_t) const {
    auto r = resolve();
    return {r.market, true, r.backend, r.is_universal};
  }
  inline MarketQuery<PriceTag> Price(NO_t) const {
    auto r = resolve();
    return {r.market, false, r.backend, r.is_universal};
  }
  inline MarketQuery<VolumeTag> Volume(YES_t) const {
    auto r = resolve();
    return {r.market, true, r.backend, r.is_universal};
  }
  inline MarketQuery<VolumeTag> Volume(NO_t) const {
    auto r = resolve();
    return {r.market, false, r.backend, r.is_universal};
  }

  // Market Depth Queries
  inline MarketQuery<DepthTag> Spread() const {
    auto r = resolve();
    return {r.market, true, r.backend, r.is_universal};
  }
  inline MarketQuery<DepthTag> BestBid() const {
    auto r = resolve();
    return {r.market, true, r.backend, r.is_universal};
  }
  inline MarketQuery<DepthTag> BestAsk() const {
    auto r = resolve();
    return {r.market, false, r.backend, r.is_universal};
  }

  inline MarketQuery<MidpointTag> Midpoint() const {
    auto r = resolve();
    return {r.market, true, r.backend, r.is_universal};
  }

  inline MarketQuery<FairPriceTag> FairPrice() const {
    auto r = resolve();
    return {r.market, true, r.backend, r.is_universal};
  }

  // Event Hooks
  inline MarketId OnTrade() const { return resolve().market; }

  struct EventBinder {
    MarketId market;
    enum class Type { Fill, Cancel, Error } type;
  };

  inline EventBinder OnFill() const {
    return {resolve().market, EventBinder::Type::Fill};
  }
  inline EventBinder OnCancel() const {
    return {resolve().market, EventBinder::Type::Cancel};
  }
  inline EventBinder OnError() const {
    return {resolve().market, EventBinder::Type::Error};
  }

  // WebSocket Streaming Entry Points
  inline void
  OnOrderbook(std::function<void(const OrderBook &)> callback) const {
    auto r = resolve();
    if (r.backend)
      r.backend->ws_subscribe_orderbook(r.market, callback);
  }

  inline void OnTrade(std::function<void(bop::Price, int64_t)> callback) const {
    auto r = resolve();
    if (r.backend)
      r.backend->ws_subscribe_trades(r.market, callback);
  }
};

// Synthetic Market Support
enum class MathOp { Add, Sub, Mul, Div };

template <typename Tag> struct SyntheticMarketQuery {
  std::shared_ptr<MarketQuery<Tag>> left;
  std::shared_ptr<MarketQuery<Tag>> right;
  MathOp op;

  bool eval() const;
  double eval_value() const;
};

struct SyntheticMarket {
  std::shared_ptr<MarketTarget> left;
  std::shared_ptr<MarketTarget> right;
  MathOp op;

  inline SyntheticMarketQuery<PriceTag> Price(YES_t) const {
    return {std::make_shared<MarketQuery<PriceTag>>(left->Price(YES)),
            std::make_shared<MarketQuery<PriceTag>>(right->Price(YES)), op};
  }
  inline SyntheticMarketQuery<PriceTag> Price(NO_t) const {
    return {std::make_shared<MarketQuery<PriceTag>>(left->Price(NO)),
            std::make_shared<MarketQuery<PriceTag>>(right->Price(NO)), op};
  }
};

// Spread Logic
struct SpreadTarget {
  MarketId m1;
  MarketId m2;
  const MarketBackend *backend = nullptr;

  SpreadTarget resolve() const {
    if (backend) {
      std::string id1 =
          m1.resolved ? m1.ticker : backend->resolve_ticker(m1.ticker);
      std::string id2 =
          m2.resolved ? m2.ticker : backend->resolve_ticker(m2.ticker);
      return {MarketId(fnv1a(id1.c_str()), id1, true),
              MarketId(fnv1a(id2.c_str()), id2, true), backend};
    }
    return *this;
  }
};

inline SyntheticMarket operator-(MarketTarget a, MarketTarget b) {
  return {std::make_shared<MarketTarget>(a.resolve()),
          std::make_shared<MarketTarget>(b.resolve()), MathOp::Sub};
}

inline SyntheticMarket operator+(MarketTarget a, MarketTarget b) {
  return {std::make_shared<MarketTarget>(a.resolve()),
          std::make_shared<MarketTarget>(b.resolve()), MathOp::Add};
}

inline SpreadTarget ToSpread(const SyntheticMarket &s) {
  return {s.left->market, s.right->market, s.left->backend};
}

inline MarketBoundSpread operator/(const bop::Buy &b,
                                   const SyntheticMarket &s) {
  return {b.quantity, true, ToSpread(s), b.timestamp_ns, s.left->backend};
}

inline MarketBoundSpread operator/(const bop::Sell &s,
                                   const SyntheticMarket &s_in) {
  return {s.quantity, false, ToSpread(s_in), s.timestamp_ns,
          s_in.left->backend};
}

struct MarketBoundSpread {
  Shares quantity;
  bool is_buy;
  SpreadTarget spread;
  int64_t timestamp_ns;
  const MarketBackend *backend = nullptr;
};

inline MarketBoundSpread operator/(const Buy &b, SpreadTarget spread) {
  auto rs = spread.resolve();
  return {b.quantity, true, rs, b.timestamp_ns, rs.backend};
}

inline MarketBoundSpread operator/(const Sell &s, SpreadTarget spread) {
  auto rs = spread.resolve();
  return {s.quantity, false, rs, s.timestamp_ns, rs.backend};
}

inline Order operator/(const MarketBoundSpread &m, YES_t) {
  Order o{m.spread.m1, m.quantity, m.is_buy, true, Price(0), m.timestamp_ns};
  o.market2 = m.spread.m2;
  o.is_spread = true;
  o.backend = m.backend;
  return o;
}

inline Order operator/(const MarketBoundSpread &m, NO_t) {
  Order o{m.spread.m1, m.quantity, m.is_buy, false, Price(0), m.timestamp_ns};
  o.market2 = m.spread.m2;
  o.is_spread = true;
  o.backend = m.backend;
  return o;
}

struct SORTarget {
  std::string ticker;
  const MarketBackend *b1;
  const MarketBackend *b2;
};

inline SORTarget operator|(const MarketTarget &a, const MarketTarget &b) {
  return {a.market.ticker, a.backend, b.backend};
}

struct SORBoundOrder {
  Shares quantity;
  bool is_buy;
  SORTarget target;
  int64_t timestamp_ns;
};

inline SORBoundOrder operator/(const Buy &b, const SORTarget &target) {
  return {b.quantity, true, target, b.timestamp_ns};
}

inline SORBoundOrder operator/(const Sell &s, const SORTarget &target) {
  return {s.quantity, false, target, s.timestamp_ns};
}

inline Order operator/(const SORBoundOrder &m, YES_t) {
  Order o{MarketId(m.target.ticker.c_str()),
          m.quantity,
          m.is_buy,
          true,
          Price(0),
          m.timestamp_ns};
  o.algo_type = AlgoType::SOR;
  o.algo_params = SORData{m.target.b1, m.target.b2};
  return o;
}

inline Order operator/(const SORBoundOrder &m, NO_t) {
  Order o{MarketId(m.target.ticker.c_str()),
          m.quantity,
          m.is_buy,
          false,
          Price(0),
          m.timestamp_ns};
  o.algo_type = AlgoType::SOR;
  o.algo_params = SORData{m.target.b1, m.target.b2};
  return o;
}

struct MarketBoundQuote {
  Shares quantity;
  MarketId market;
  int64_t timestamp_ns;
  const MarketBackend *backend = nullptr;
  Price spread = Price::from_cents(2);
  ReferencePrice ref = ReferencePrice::Mid;
};

inline MarketBoundQuote operator/(const Quote &q, MarketId market) {
  return MarketBoundQuote{q.quantity, market, q.timestamp_ns};
}

inline MarketBoundQuote operator/(const Quote &q, const char *market) {
  return MarketBoundQuote{q.quantity, MarketId(market), q.timestamp_ns};
}

inline MarketBoundQuote operator/(const Quote &q, MarketTarget target) {
  auto r = target.resolve();
  return MarketBoundQuote{q.quantity, r.market, q.timestamp_ns, r.backend};
}

struct Spread {
  Price value;
  explicit Spread(Price p) : value(p) {}
};

inline MarketBoundQuote operator|(MarketBoundQuote q, Spread s) {
  q.spread = s.value;
  return q;
}

struct Offset {
  ReferencePrice ref;
  explicit Offset(ReferencePrice r) : ref(r) {}
};

inline MarketBoundQuote operator|(MarketBoundQuote q, Offset o) {
  q.ref = o.ref;
  return q;
}

class ExecutionEngine;

inline Order operator>>(MarketBoundQuote q, ExecutionEngine &engine);

template <typename Tag, typename Q = MarketQuery<Tag>> struct Condition {
  using Unit = typename TagUnit<Tag>::type;
  Q query;
  Unit threshold;
  bool is_greater;
  Condition(Q q, Unit t, bool g) : query(q), threshold(t), is_greater(g) {}

  bool eval() const;
};

// SIMD Condition Batching
struct PriceBatch {
  static constexpr size_t kBatchSize = 4;
  alignas(32) int64_t thresholds[kBatchSize];
  alignas(32) int64_t results[kBatchSize];
  uint32_t active_mask = 0;
  bool is_greater[kBatchSize];

  PriceBatch() {
    for (size_t i = 0; i < kBatchSize; ++i) {
      thresholds[i] = 0;
      results[i] = 0;
      is_greater[i] = true;
    }
  }

  inline void add_condition(size_t idx, int64_t threshold, bool greater) {
    if (idx < kBatchSize) {
      thresholds[idx] = threshold;
      is_greater[idx] = greater;
      active_mask |= (1u << idx);
    }
  }

  inline uint32_t evaluate_avx2(int64_t current_price) const {
    __m256i current = _mm256_set1_epi64x(current_price);
    __m256i thresh =
        _mm256_load_si256(reinterpret_cast<const __m256i *>(thresholds));

    // current > thresholds
    __m256i gt_mask = _mm256_cmpgt_epi64(current, thresh);
    // current < thresholds
    __m256i lt_mask = _mm256_cmpgt_epi64(thresh, current);

    uint32_t gt_res = _mm256_movemask_pd(_mm256_castsi256_pd(gt_mask));
    uint32_t lt_res = _mm256_movemask_pd(_mm256_castsi256_pd(lt_mask));

    uint32_t final_mask = 0;
    for (size_t i = 0; i < kBatchSize; ++i) {
      bool matched =
          is_greater[i] ? (gt_res & (1u << i)) : (lt_res & (1u << i));
      if (matched)
        final_mask |= (1u << i);
    }
    return final_mask & active_mask;
  }
};

template <typename Tag> struct RelativeCondition {
  MarketQuery<Tag> left;
  MarketQuery<Tag> right;
  bool is_greater;

  bool eval() const;
};

// Helper to identify DSL conditions
template <typename T> struct is_bop_condition : std::false_type {};

template <typename Tag, typename Q>
struct is_bop_condition<Condition<Tag, Q>> : std::true_type {};
template <typename Tag>
struct is_bop_condition<RelativeCondition<Tag>> : std::true_type {};
template <typename L, typename R>
struct is_bop_condition<AndCondition<L, R>> : std::true_type {};
template <typename L, typename R>
struct is_bop_condition<OrCondition<L, R>> : std::true_type {};

// Logical Operators for Conditions
template <typename L, typename R>
inline std::enable_if_t<is_bop_condition<L>::value ||
                            is_bop_condition<R>::value,
                        AndCondition<L, R>>
operator&&(const L &l, const R &r) {
  return {l, r};
}

template <typename L, typename R>
inline std::enable_if_t<
    is_bop_condition<L>::value || is_bop_condition<R>::value, OrCondition<L, R>>
operator||(const L &l, const R &r) {
  return {l, r};
}

// Composition structures
template <typename L, typename R> struct AndCondition {
  L left;
  R right;
  inline bool eval() const { return left.eval() && right.eval(); }
};

template <typename L, typename R> struct OrCondition {
  L left;
  R right;
  inline bool eval() const { return left.eval() || right.eval(); }
};

template <typename T> struct ConditionalOrder {
  T condition;
  Order order;

  ConditionalOrder(T c, Order &&o)
      : condition(std::move(c)), order(std::move(o)) {}
  ConditionalOrder(T c, const Order &o) : condition(std::move(c)), order(o) {}
};

template <typename T> struct Shadow_t {
  T condition;
};

template <typename T> inline Shadow_t<T> Shadow(T cond) { return {cond}; }

template <typename T>
inline ConditionalOrder<T> operator|(Order o, Shadow_t<T> s) {
  return {std::move(s.condition), std::move(o)};
}

template <typename T> struct WhenBinder {
  T condition;
};

template <typename T> inline WhenBinder<T> When(T c) { return {c}; }

struct OCOOrder {
  Order order1;
  Order order2;
  inline bool eval() const { return true; }
};

inline OCOOrder Either(Order &&o1, Order &&o2) {
  return {std::move(o1), std::move(o2)};
}

inline OCOOrder operator||(Order &&o1, Order &&o2) {
  return {std::move(o1), std::move(o2)};
}

inline OCOOrder operator||(const Order &o1, const Order &o2) {
  return {o1, o2};
}

template <typename T>
inline ConditionalOrder<T> operator>>(WhenBinder<T> w, Order &&o) {
  return {std::move(w.condition), std::move(o)};
}

template <typename T>
inline ConditionalOrder<T> operator>>(WhenBinder<T> w, const Order &o) {
  return {std::move(w.condition), o};
}

inline MarketBoundOrder operator/(const Buy &b, MarketTarget target) {
  auto r = target.resolve();
  return {b.quantity, true, r.market, b.timestamp_ns, r.backend};
}

inline MarketBoundOrder operator/(const Sell &s, MarketTarget target) {
  auto r = target.resolve();
  return {s.quantity, false, r.market, s.timestamp_ns, r.backend};
}

// Relative Comparisons
template <typename Tag>
inline RelativeCondition<Tag> operator<(MarketQuery<Tag> a,
                                        MarketQuery<Tag> b) {
  return {a, b, false};
}

template <typename Tag>
inline RelativeCondition<Tag> operator>(MarketQuery<Tag> a,
                                        MarketQuery<Tag> b) {
  return {a, b, true};
}

// Price Comparisons
inline Condition<PriceTag> operator>(MarketQuery<PriceTag> q, Price threshold) {
  return {q, threshold, true};
}
inline Condition<PriceTag> operator<(MarketQuery<PriceTag> q, Price threshold) {
  return {q, threshold, false};
}

inline Condition<PriceTag> operator>(MarketQuery<PriceTag> q, double price) {
  return {q, Price::from_double(price).raw, true};
}
inline Condition<PriceTag> operator<(MarketQuery<PriceTag> q, double price) {
  return {q, Price::from_double(price).raw, false};
}

// Volume Comparisons
inline Condition<VolumeTag> operator>(MarketQuery<VolumeTag> q, Shares t) {
  return {q, t, true};
}
inline Condition<VolumeTag> operator<(MarketQuery<VolumeTag> q, Shares t) {
  return {q, t, false};
}

// Depth Comparisons
inline Condition<DepthTag> operator<(MarketQuery<DepthTag> q,
                                     Shares threshold) {
  return {q, threshold, false};
}
inline Condition<DepthTag> operator>(MarketQuery<DepthTag> q,
                                     Shares threshold) {
  return {q, threshold, true};
}

// Position comparisons
inline Condition<PositionTag> operator>(MarketQuery<PositionTag> q,
                                        Shares shares) {
  return {q, shares, true};
}
inline Condition<PositionTag> operator<(MarketQuery<PositionTag> q,
                                        Shares shares) {
  return {q, shares, false};
}

// Balance comparisons
inline Condition<BalanceTag, BalanceQuery> operator>(BalanceQuery q,
                                                     Price amount) {
  return {q, amount, true};
}
inline Condition<BalanceTag, BalanceQuery> operator<(BalanceQuery q,
                                                     Price amount) {
  return {q, amount, false};
}

template <typename Tag>
inline Condition<Tag, SyntheticMarketQuery<Tag>>
operator>(SyntheticMarketQuery<Tag> q, typename TagUnit<Tag>::type threshold) {
  return {q, threshold, true};
}

template <typename Tag>
inline Condition<Tag, SyntheticMarketQuery<Tag>>
operator<(SyntheticMarketQuery<Tag> q, typename TagUnit<Tag>::type threshold) {
  return {q, threshold, false};
}

// --- DSL Bytecode VM ---
enum class OpCode : uint8_t {
  LOAD_PRICE, // arg: MarketHash
  LOAD_VOL,   // arg: MarketHash
  LOAD_POS,   // arg: MarketHash
  LOAD_CONST, // arg: ConstantIndex
  CMP_GT,
  CMP_LT,
  AND,
  OR,
  JUMP_IF_FALSE, // arg: Offset
  EXEC_ORDER,    // arg: OrderIndex
  HALT
};

struct Instruction {
  OpCode op;
  uint32_t arg;
};

struct BytecodeEvaluator {
  std::pmr::vector<Instruction> code;
  std::pmr::vector<int64_t> constants;
  std::pmr::vector<Order> orders;

  BytecodeEvaluator(
      std::pmr::memory_resource *mr = std::pmr::get_default_resource())
      : code(mr), constants(mr), orders(mr) {}

  bool evaluate(ExecutionEngine &engine) const;
};

// Global helper for DSL entry
inline MarketTarget Market(MarketId mkt) { return {mkt, nullptr, false}; }
inline MarketTarget Market(const char *name) {
  return {MarketId(name), nullptr, false};
}

inline MarketTarget UniversalMarket(const char *name) {
  return {MarketId(name), nullptr, true};
}

inline MarketTarget Market(MarketId mkt, const MarketBackend &b) {
  return MarketTarget{mkt, &b, false}.resolve();
}
inline MarketTarget Market(const char *name, const MarketBackend &b) {
  return MarketTarget{MarketId(name), &b, false}.resolve();
}

inline MarketQuery<PositionTag> Position(MarketTarget target) {
  auto r = target.resolve();
  return {r.market, true, r.backend};
}

inline MarketQuery<PositionTag> Position(MarketId mkt) { return {mkt, true}; }

inline MarketQuery<OpenOrdersTag> OpenOrders(MarketId mkt) {
  return {mkt, true};
}
inline MarketQuery<OpenOrdersTag> OpenOrders(const MarketTarget &mt) {
  return {mt.market, true};
}

inline Condition<OpenOrdersTag> operator<(MarketQuery<OpenOrdersTag> q,
                                          int threshold) {
  return {q, static_cast<long long>(threshold), false};
}

inline BalanceQuery Balance() { return {}; }

inline Condition<ExposureTag, RiskQuery> Exposure() {
  return {{RiskQuery::Type::Exposure}, 0, false};
}

inline Condition<PnLTag, RiskQuery> PnL() {
  return {{RiskQuery::Type::PnL}, 0, false};
}

inline Condition<PositionTag> MaxPosition(int64_t shares) {
  return Condition<PositionTag>(MarketQuery<PositionTag>{MarketId(0), true},
                                shares, false);
}

inline Condition<PnLTag, RiskQuery> DailyLossLimit(Price p) {
  return Condition<PnLTag, RiskQuery>(RiskQuery{RiskQuery::Type::PnL}, -p.raw,
                                      true);
}

inline Condition<ExposureTag, RiskQuery> MaxExposure(Price p) {
  return Condition<ExposureTag, RiskQuery>(RiskQuery{RiskQuery::Type::Exposure},
                                           p.raw, false);
}

inline bop::Spread Spread(Price p) { return bop::Spread(p); }
inline bop::Offset Offset(ReferencePrice r) { return bop::Offset(r); }

struct PortfolioBinder {
  PortfolioQuery::Metric metric;
};

inline PortfolioBinder Portfolio(PortfolioQuery::Metric m) { return {m}; }

struct PortfolioMetricProxy {
  PortfolioQuery::Metric metric;
};

struct PortfolioProxy {
  inline PortfolioMetricProxy TotalDelta() const {
    return {PortfolioQuery::Metric::TotalDelta};
  }
  inline PortfolioMetricProxy TotalGamma() const {
    return {PortfolioQuery::Metric::TotalGamma};
  }
  inline PortfolioMetricProxy TotalTheta() const {
    return {PortfolioQuery::Metric::TotalTheta};
  }
  inline PortfolioMetricProxy TotalVega() const {
    return {PortfolioQuery::Metric::TotalVega};
  }
  inline PortfolioMetricProxy NetExposure() const {
    return {PortfolioQuery::Metric::NetExposure};
  }
  inline PortfolioMetricProxy PortfolioValue() const {
    return {PortfolioQuery::Metric::PortfolioValue};
  }
};

inline PortfolioProxy Portfolio() { return {}; }

struct TimeTrigger {
  std::chrono::system_clock::time_point trigger_time;
  inline bool eval() const {
    return std::chrono::system_clock::now() >= trigger_time;
  }
};

inline WhenBinder<TimeTrigger> At(std::chrono::system_clock::time_point t) {
  return {TimeTrigger{t}};
}

inline WhenBinder<TimeTrigger> At(const std::string &iso_time) {
  std::tm tm = {};
  strptime(iso_time.c_str(), "%Y-%m-%d %H:%M:%S", &tm);
  auto tp = std::chrono::system_clock::from_time_t(std::mktime(&tm));
  return {TimeTrigger{tp}};
}

inline Condition<PortfolioTag, PortfolioQuery> operator>(PortfolioMetricProxy p,
                                                         double threshold) {
  return {PortfolioQuery{p.metric}, static_cast<int64_t>(threshold * 1000000),
          true};
}

inline Condition<PortfolioTag, PortfolioQuery> operator<(PortfolioMetricProxy p,
                                                         double threshold) {
  return {PortfolioQuery{p.metric}, static_cast<int64_t>(threshold * 1000000),
          false};
}

// --- Temporal Helpers ---
inline WhenBinder<TimerTrigger> Every(std::chrono::milliseconds d) {
  return {TimerTrigger(d, true)};
}

inline WhenBinder<TimerTrigger> After(std::chrono::milliseconds d) {
  return {TimerTrigger(d, false)};
}

// Support for standard chrono literals
template <typename Rep, typename Period>
inline WhenBinder<TimerTrigger> Every(std::chrono::duration<Rep, Period> d) {
  return Every(std::chrono::duration_cast<std::chrono::milliseconds>(d));
}

template <typename Rep, typename Period>
inline WhenBinder<TimerTrigger> After(std::chrono::duration<Rep, Period> d) {
  return After(std::chrono::duration_cast<std::chrono::milliseconds>(d));
}

// --- Workflow Chaining ---
struct WorkflowChain {
  ConditionalOrder<Trigger> head;
  std::pmr::vector<std::shared_ptr<Action>> tail;

  WorkflowChain(
      ConditionalOrder<Trigger> h,
      std::pmr::memory_resource *mr = std::pmr::get_default_resource())
      : head(std::move(h)), tail(mr) {}
};

inline void operator>>(WorkflowChain chain, ExecutionEngine &engine);

inline WorkflowChain operator>>(ConditionalOrder<Trigger> co,
                                Order next_order) {
  WorkflowChain chain(std::move(co));
  chain.tail.push_back(std::make_shared<OrderAction>(std::move(next_order)));
  return chain;
}

inline WorkflowChain operator>>(WorkflowChain chain, Order next_order) {
  chain.tail.push_back(std::make_shared<OrderAction>(std::move(next_order)));
  return chain;
}

// --- Proportional Sizing ---
struct PctSize {
  double value;
};

inline Buy operator*(Buy b, PctSize p) {
  // Logic handled in ExecutionEngine sizing to avoid context dependency here
  return b;
}

inline PctSize operator"" _pct(long double v) {
  return {static_cast<double>(v)};
}
inline PctSize operator"" _pct(unsigned long long int v) {
  return {static_cast<double>(v)};
}

// Exposure/PnL comparisons
inline Condition<ExposureTag, RiskQuery>
operator<(Condition<ExposureTag, RiskQuery> c, long long threshold) {
  c.threshold = threshold;
  c.is_greater = false;
  return c;
}

// Batch DSL Entry
inline std::initializer_list<Order> Batch(std::initializer_list<Order> list) {
  return list;
}

struct StrategyContext {
  std::pmr::memory_resource *mr;
  static StrategyContext &instance() {
    static thread_local StrategyContext ctx{std::pmr::get_default_resource()};
    return ctx;
  }
};

inline void SetStrategyMemoryResource(std::pmr::memory_resource *mr) {
  StrategyContext::instance().mr = mr;
}

// --- Stateful Strategy Helpers ---
inline Trigger *After(std::chrono::milliseconds d) {
  auto *mr = StrategyContext::instance().mr;
  return new (mr) TimerTrigger(d, false);
}

inline Trigger *Every(std::chrono::milliseconds d) {
  auto *mr = StrategyContext::instance().mr;
  return new (mr) TimerTrigger(d, true);
}

inline Trigger *OnFill(const Order &o) {
  auto *mr = StrategyContext::instance().mr;
  return new (mr) OnFillTrigger(o.order_id, mr);
}

inline Trigger *OnFill(std::string_view order_id) {
  auto *mr = StrategyContext::instance().mr;
  return new (mr) OnFillTrigger(order_id, mr);
}

inline Action *Cancel(std::string_view order_id) {
  auto *mr = StrategyContext::instance().mr;
  return new (mr) CancelAction(order_id, mr);
}

inline Action *Cancel(const Order &o) {
  auto *mr = StrategyContext::instance().mr;
  return new (mr) CancelAction(o.order_id, mr);
}

inline WorkflowStep operator>>(Trigger *t, Order o) {
  auto *mr = StrategyContext::instance().mr;
  return {t, new (mr) OrderAction(std::move(o)), mr};
}

inline WorkflowStep operator>>(Trigger *t,
                               std::function<void(ExecutionEngine &)> cb) {
  auto *mr = StrategyContext::instance().mr;
  return {t, new (mr) CallbackAction(std::move(cb)), mr};
}

inline WorkflowStep operator>>(Trigger *t, Action *a) {
  return {t, a, StrategyContext::instance().mr};
}

} // namespace bop
