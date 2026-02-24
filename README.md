# BOP: Best Order Protocol

BOP is a high-performance C++17 algorithmic trading framework specifically designed for **prediction markets** (e.g., Kalshi, Polymarket, PredictIt). 

Unlike traditional trading libraries that rely on verbose API calls, BOP introduces a **Fluent Domain-Specific Language (DSL)** that allows quantitative traders to express complex market logic, execution algorithms, and risk gates using intuitive C++ operator overloading.

## The BOP Language

The core philosophy of BOP is that **trading logic should read like a sentence**. By leveraging C++ operator overloading, BOP transforms standard code into a readable, pipeline-based strategy language.

### Core Syntax
*   **Routing**: `/` (e.g., `Buy(100) / "BTC" / YES`)
*   **Dispatch**: `>>` (e.g., `order >> LiveExchange`)
*   **Conditional**: `When(...) >> Order(...)`
*   **Modifiers**: `|` (e.g., `order | IOC | PostOnly`)
*   **Brackets**: `&` (e.g., `order & TakeProfit(0.70) & StopLoss(0.40)`)

### Example: A Persistent Arbitrage Strategy
```cpp
auto arb = When(Market("BTC", kalshi).Price(YES) < Market("BTC", polymarket).Price(YES))
           >> (Buy(100_shares) / Market("BTC", kalshi) / YES + MarketPrice());

arb >> LiveExchange;
```

## Repository Structure

The project is organized into modular layers to separate the DSL aesthetics from the high-frequency execution core:

*   **`core/`**: The heart of the framework.
    *   `core.hpp`: Definition of the fundamental DSL atoms (`Order`, `Price`, `MarketId`).
    *   `logic.hpp`: The "Grammar" of the DSL, containing `When`, `Position`, and logical operators.
    *   `engine.hpp`: The execution core that manages multi-threaded state and risk gates.
    *   `backtest.hpp`: Deterministic historical simulation engine with latency/slippage modeling.
    *   `database.hpp`: Persistence layer for logging every fill, order, and PnL snapshot to SQLite.
*   **`exchanges/`**: Concrete implementations of exchange backends (Kalshi, Polymarket, PredictIt, Betfair, Gnosis).
*   **`include/` & `external/`**: Dependency management (nlohmann/json, simdjson).
*   **`examples/`**: Ready-to-run strategy templates and backtesting demos.

## Performance & Safety

BOP is built for the unique constraints of binary prediction markets:
*   **Pre-Trade Risk**: Automatic kill-switches based on daily loss limits and fat-finger protection.
*   **Low Latency**: Uses `simdjson` for high-speed WebSocket message parsing and a thread-safe lock-free command queue for order dispatch.
*   **Persistence**: Built-in SQLite logging ensuring that strategy performance is audited and persisted across restarts.

## Getting Started

### Prerequisites
*   C++17 Compatible Compiler (Clang 10+, GCC 9+)
*   CMake 3.15+
*   Dependencies: Boost, OpenSSL, CURL, SQLite3

### Build
```bash
mkdir build && cd build
cmake ..
make -j
```

---
*BOP is a research-grade framework. Trade responsibly.*
