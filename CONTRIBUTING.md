# Contributing to BOP

Thank you for your interest in contributing to the Binary Outcome Protocol (BOP)! We welcome contributions from the community to help make BOP the best algorithmic trading framework for prediction markets.

## How to Contribute

### Reporting Bugs
* Check the existing issues to see if the bug has already been reported.
* If not, open a new issue with a clear title and detailed description, including steps to reproduce the bug.

### Suggesting Enhancements
* Open a new issue describing the proposed enhancement and its benefits.
* Discuss the enhancement with the maintainers and other community members.

### Pull Requests
* Fork the repository and create a new branch for your contribution.
* Ensure your code follows the existing style and conventions.
* Add tests to verify your changes.
* Submit a pull request with a clear description of the changes and the problem they solve.

## Style Guidelines

### C++ Style
* We use C++17 standards.
* Follow the naming conventions used in the existing codebase (e.g., snake_case for functions and variables, PascalCase for classes).
* Use `clang-tidy` to check for common errors and style violations.

### Commit Messages
* Use descriptive commit messages that explain the "why" and "what" of the changes.
* We follow the [Conventional Commits](https://www.conventionalcommits.org/) specification.

## Development Environment Setup

1.  Clone the repository: `git clone https://github.com/ekrishgupta/bop.git`
2.  Install dependencies (Boost, OpenSSL, CURL, SQLite3).
3.  Create a build directory: `mkdir build && cd build`
4.  Configure the project: `cmake ..`
5.  Build the project: `make -j`

## Code of Conduct
Please note that this project is released with a [Code of Conduct](CODE_OF_CONDUCT.md). By participating in this project, you agree to abide by its terms.
