# Testing watx

The macOS CLI and shared library have been built. No runtime executions or
tests were run for the new binary reader, interpreter, JIT, runtime workers,
shared libraries, or CLI expansion. The commands below describe the available checks; they have not been executed
locally for this change.

The default CMake build uses C++98 with compiler extensions disabled and warning
flags enabled. No external WAT compiler is used to build or test watx.

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

CTest runs the native validator checks in `tests/validator_smoke.cpp` and the
CLI checks in `tests/runtime_smoke.cmake`. The optional conformance harness below
uses Node as an independent execution engine.

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

## GitHub Actions

`.github/workflows/ci.yml` runs on pushes, pull requests, and manual dispatch.
Its six native jobs cover macOS, Linux, and Windows, each on x86_64 and ARM64.
Each job builds with `BUILD_TESTING=ON`, runs CTest, installs the project, and
uploads a tar archive containing the CLI, shared library, headers, and package.

CTest includes `validator` and `runtime`. The runtime check in
`tests/runtime_smoke.cmake` generates its WAT and Wasm inputs inside the build
directory and invokes the newly built CLI. It checks:

- Native architecture reported by the executable.
- WAT compilation and binary decoding/validation.
- Interpreter and JIT execution with default, explicit, and automatic workers.
- i32, i64, and f64 values; globals and starts; active data; memory loads/stores;
  indirect calls; and recursive calls.
- Controlled traps, fuel exhaustion, argument mismatches, and invalid options.
- Redundant top-level inputs combined with textual includes, in both orders.
- Rejection of malformed binary input.

Each CLI process has a timeout, and process crashes or timeouts fail the check.
These are smoke checks, not proof of complete MVP conformance or JIT correctness.
They do not yet cover the custom host-module API, concurrent embedding calls,
all numeric boundaries, every instruction, or installed consumer linking.

## Remaining coverage

Future checks should compare interpreter, JIT, and an independent engine over a
larger corpus, including signed zeros, NaNs, numeric boundaries, all memory access
widths, nested control flow, imports, and active segment failures. Add custom host
callbacks, concurrent library invocations, resource lifetime checks, installed
package discovery, and Windows DLL consumer linking.

The workflow has been added without running tests locally. Use its actual job
results before claiming success for a platform or architecture.
