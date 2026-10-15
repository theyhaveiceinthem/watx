# Changelog

Notable changes to watx are recorded here. This project has not published a
release history yet.

## Unreleased

- Continue the from-scratch C++98 WebAssembly MVP compiler and validator.
- Add MVP binary decoding, interpreter execution, native baseline JIT, and CLI
  paths. These additions are new and have not been verified across all six
  intended native targets: Windows, macOS, and Linux on x86_64 and ARM64.

- Separate interpreter execution workers (`--threads N`) from JIT execution
  workers (`--jit [N]`); remove the hardware-thread query flag.
- Add a runtime execution worker pool and developer-defined host modules through
  `watx/host.h`.
- Configure shared library and CLI artifacts on macOS, Linux, and Windows,
  explicit Windows DLL exports, and an installable CMake package.
- Expand the README with badges and add security, contribution, and support
  policies plus GitHub issue and pull request templates.
- Skip redundant top-level text inputs already included by another input.
