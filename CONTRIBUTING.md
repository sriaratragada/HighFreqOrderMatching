# Contributing

## License

By contributing, you agree that your contributions will be licensed under the [MIT License](LICENSE), the same terms as this project.

## Development setup

Requirements:

- CMake 3.16 or later
- A C++17 compiler (GCC 9+, Clang 10+, or MSVC 2019+)
- Network access at **configure** time when `BUILD_HFOM_TESTS=ON` (GoogleTest is downloaded via `FetchContent`)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Pull requests

- Keep changes focused; match existing style.
- Add or update tests for behavior changes.
- Update [CHANGELOG.md](CHANGELOG.md) under **Unreleased** (or before a release tag).

## Code style

- C++17, prefer clear names over micro-optimizations in tests and CLI code.
- Avoid introducing new global mutable state without a strong reason.
