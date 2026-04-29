# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.0] - 2026-04-29

### Added

- `hfom_orderbook` static library target and install rules (`cmake` package + `HighFreqOrderMatching::hfom_orderbook`).
- `match_cli` flags: `--version`, `--json` (JSON-line trade output).
- Duplicate resting order ID rejection (`OrderBook::addOrder` returns `false`, CLI reports error).
- `OrderBook::isOrderIdActive` for callers.
- Generated `hfom/version.hpp` with `HFOM_VERSION_STRING`.
- CI workflow (Ubuntu + macOS): configure, build `Release`, `ctest`.
- Tests for market peg (no multi-level walk), stop behavior, and pro-rata volume conservation.

### Changed

- GoogleTest is fetched only when `BUILD_HFOM_TESTS=ON` (default **on** only if this directory is the CMake source root).
