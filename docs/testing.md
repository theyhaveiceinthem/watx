# Testing watx

The macOS CLI and shared library have been built. No runtime executions or
tests were run for the new binary reader, interpreter, JIT, runtime workers,
shared libraries, or CLI expansion. The commands below document existing harnesses for
later use; they have not been executed for this change.

The default CMake build uses C++98 with compiler extensions disabled and warning
flags enabled. No external WAT compiler is used to build or test watx.

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

CTest runs the native validator checks in `tests/validator_smoke.cpp`. The CLI
can execute compiled modules through `--run --function NAME`; the optional
conformance harness below uses Node as an independent execution engine.

## Official core text tests

Fetch the WebAssembly specification tests at the pinned 1.0 revision:

```sh
git clone --depth 1 --branch wg-1.0 https://github.com/WebAssembly/spec.git build/spec-mvp
git -C build/spec-mvp checkout 977f97014c962f7bd1291fcc6d28b41a924882bf
node tests/conformance.js
```

Pass individual `.wast` filenames to narrow a run. `WATX_SPEC_DIR` selects a
different test directory, `WATX_BINARY` selects the compiler executable, and
`WATX_TEST_WORK` selects a separate scratch directory for concurrent runs.

The runner compiles text modules with watx, executes actions in Node, checks
returns and traps, and checks that malformed or invalid sources are rejected.
It includes an independent rational-to-IEEE float conversion for expected
values. NaN arguments and result bits are checked through Wasm trampolines so
JavaScript does not quiet signaling NaNs at a call boundary.

Binary-input cases are explicitly skipped by the current text conformance
harness, along with actions that depend on such a module. The CLI now accepts
binary modules, but the existing harness has not been extended to exercise its
binary decoder or runtime. Skipped categories and failures are recorded
in `build/conformance/results.json`; a passing run does not imply a binary
decoder has been tested.

The test-only `tests/support/wasm.js` adapter prechecks all active segment bounds
before instantiation, matching the 1.0 test suite. Modern Node may otherwise
write earlier segments before a later out-of-bounds segment fails. The compiler and emitted binaries do not
depend on this adapter.

## Memory and undefined behavior checks

On a compiler that supports AddressSanitizer and UndefinedBehaviorSanitizer:

```sh
cmake -S . -B build/sanitized -DCMAKE_BUILD_TYPE=Debug \
  '-DCMAKE_CXX_FLAGS=-Werror -fsanitize=address,undefined -fno-omit-frame-pointer'
cmake --build build/sanitized --parallel 4
ctest --test-dir build/sanitized --output-on-failure
```

## Deferred runtime and platform coverage

Future GitHub Actions coverage needs native jobs for macOS, Linux, and Windows,
each on x86_64 and ARM64. No workflow or runtime test suite is added by this
change. Native execution is needed to check each emitter and calling convention;
a cross-compilation job alone cannot establish execution correctness.

The planned checks include:

- Build the core library and CLI with C++98 settings on each target.
- Decode valid and malformed MVP binaries, including custom sections and LEB
  boundary cases.
- Compare interpreter, JIT, and Node results and traps over the same corpus.
- Exercise signed zeros, NaNs, numeric boundaries, memory growth, indirect calls,
  nested control flow, starts, imports, and active segment failures.
- Check typed argument parsing, raw binary streams, diagnostics, fuel, stack
  limits, and memory caps.
- Check separate interpreter/JIT execution worker settings, independent
  concurrent calls, nested calls, custom host callbacks, resource lifetimes,
  and automatic hardware-thread selection.
- Check shared-library linking, exported symbols, installed package discovery,
  Windows DLL loading, and Unix executable runtime library paths.
- Exercise executable-memory publication and instruction-cache synchronization,
  including macOS JIT write protection and both Windows calling conventions.

Until those checks exist and pass, the platform implementations and the runtime
should be treated as unverified.
