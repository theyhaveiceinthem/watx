# Changelog

Notable changes to watx are recorded here.

## Unreleased

- Emit common numeric and local operations directly in the x86_64 and ARM64 JIT.
- Add a decoded interpreter loop and cache structured control targets.
- Analyze operand stack bounds and keep small stacks, locals, and control frames
  in inline storage.
- Pass Wasm call arguments directly from the caller's operand stack, avoiding
  temporary argument and result vectors for internal calls.
- Preserve the checked execution path for fuel, tracing, and cancellation.
- Add numeric differential and embedding checks. These optimizations require
  rebuilding from source and are not included in the published 0.2.0 binary assets.

## 0.2.0 — 2026-10-15

- Continue the from-scratch C++98 WebAssembly MVP compiler and validator.
- Add MVP binary decoding, interpreter execution, native baseline JIT, and CLI
  paths. Builds and smoke tests passed on Windows, macOS, and Linux on x86_64
  and ARM64. This is an experimental pre-release.

- Separate interpreter execution workers (`--threads N`) from JIT execution
  workers (`--jit [N]`); remove the hardware-thread query flag.
- Add a runtime execution worker pool and developer-defined host modules through
  `watx/host.h`.
- Configure shared library and CLI artifacts on macOS, Linux, and Windows,
  explicit Windows DLL exports, and an installable CMake package.
- Expand the README with badges and add security, contribution, and support
  policies plus GitHub issue and pull request templates.
- Skip redundant top-level text inputs already included by another input.
