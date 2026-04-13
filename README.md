# BOP: Generalized Outcome Protocol

BOP is an embedded **Domain-Specific Language (DSL)** for financial markets, natives integrated into C++20. 

Originally built for prediction markets, BOP has evolved into a high-performance framework for expressing complex market logic, execution algorithms, and risk gates across **Binary Markets**, **Multi-Outcome (Categorical) Markets**, and **Traditional Equities**.

## The BOP Language

The core philosophy of BOP is that **trading logic should read like a sentence**. By leveraging C++20 operator overloading and concepts, BOP transforms standard code into a readable, pipeline-based strategy language.

### Core Syntax
*   **Routing**: `/` (e.g., `Buy(100) / "BTC" / Outcome(YES)`)
*   **Dispatch**: `>>` (e.g., `order >> LiveExchange`)
*   **Conditional**: `When(...) >> Order(...)`
*   **Modifiers**: `|` (e.g., `order | IOC | PostOnly`)
*   **Brackets**: `&` (e.g., `order & TakeProfit(0.70) & StopLoss(0.40)`)

### Example: Multi-Exchange Universal Arbitrage
```cpp
// Queries the best price for BTC YES across all registered exchanges
auto arb = When(UniversalMarket("BTC").Price(YES) < 0.45_usd)
           >> (Buy(100_shares) / UniversalMarket("BTC") / YES);

arb >> LiveExchange;
```

## Features

### 1. Unified Outcome Primitive
BOP uses a generic `OutcomeId` variant capable of representing:
- **Binary**: `YES` / `NO` (bool)
- **Categorical**: Candidate names or positions (uint32_t / string)
- **Equities**: Tickers and directions (string)

### 2. Universal Market Routing
Register "super-tickers" that span multiple exchanges. The engine automatically routes queries and orders to the best available liquidity provider.

### 3. Native Backtesting
Deterministic historical simulation with high-fidelity latency and slippage modeling. Transition from backtest to live trading by swapping `engine` targets.

## Repository Structure

*   **`core/`**: The heart of the framework.
    *   `core.hpp`: Definition of the fundamental DSL atoms (`Order`, `OutcomeId`, `Price`).
    *   `logic.hpp`: The "Grammar" of the DSL.
    *   `engine.hpp`: The execution core and risk management system.
    *   `backtest.hpp`: Deterministic simulation engine.
*   **`exchanges/`**: Backend implementations (Kalshi, Polymarket).
*   **`examples/`**: Templates for arbitrage, market making, and proportional sizing.

## Performance & Safety

*   **Pre-Trade Risk**: Automatic kill-switches and fat-finger protection.
*   **C++20 Zero-Cost Abstractions**: Leveraging concepts for compile-time validation of backend capabilities.
*   **Persistence**: Integrated SQLite logging for audited execution history.

## Getting Started

### Prerequisites
*   C++20 Compatible Compiler (Clang 13+, GCC 11+)
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
