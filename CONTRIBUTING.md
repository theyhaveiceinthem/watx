# Contributing

Contributions are welcome. watx is a from-scratch WebAssembly MVP
implementation written in C++98. The interpreter, native JIT, binary reader,
and six native target paths are under active development and have not yet been
verified across all targets.

## Before making a change

- Keep the C++ implementation compatible with C++98 and the project's existing
  formatting configuration.
- Keep changes within the documented WebAssembly scope unless a proposal
  explicitly discusses extending that scope.
- For a substantial change, use a GitHub issue to describe the problem and
  intended behavior before investing in a large implementation.
- Avoid including private data or security exploit details in public issues.

## Pull requests

Keep each pull request focused. Describe the behavior being changed, mention
compatibility implications, and include a small reproducer or test coverage when
that is useful. Changes should preserve the intended Windows, macOS, and Linux
targets on x86_64 and ARM64; these targets are not yet verified equally.

In the pull request description, report the checks you performed. If you did not
build or run tests, say so plainly rather than implying that the change was
verified. Do not claim cross-platform success without checking the relevant
targets.

## Scope and expectations

The implementation is written from scratch and does not use an external
WebAssembly compiler or runtime. Please keep parsing, validation, execution,
and code generation behavior consistent across the interpreter and JIT. A
contribution does not imply production readiness, a performance guarantee, or
support for untrusted input.
