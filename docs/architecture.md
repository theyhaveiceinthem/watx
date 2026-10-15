# Architecture

watx is a C++98 compiler and runtime built around a shared module representation
for compilation, validation, binary I/O, and execution.
The CLI contains orchestration; the core library contains parsing, validation,
binary I/O, execution, native emitters, and platform workers.

## Text compilation

1. `src/resolve/source.cpp` reads files and recursively expands `(include
   "path.wat")`. Paths are relative to the including file. Expansion inserts
   tokens without imposing module boundaries, including inside functions.
2. `src/cli/main.cpp` removes an optional wrapper from each top-level text input
   and combines fragments in argument order into one module.
3. `src/lexer/token.cpp` and `src/text/syntax.cpp` produce tokens and syntax
   nodes. `src/parser/parser.cpp` installs declarations, resolves indices, and
   interns implicit function types before parallel instruction compilation.
4. `src/parser/instructions.cpp` compiles each function into linear instructions
   with explicit control delimiters, resolved indices, and encoded constants.
   Independent functions can compile in parallel against an immutable module
   signature/index layout. Constant expressions compile after those workers join.
5. `src/validation/validator.cpp` checks module structure and constant expressions,
   then validates function bodies using independent operand/control stacks.
6. `src/wasm/binary_writer.cpp` writes the MVP header and ordered sections.

## Binary input

`src/wasm/binary_reader.cpp` decodes binary modules into the same representation.
It bounds section readers, checks encodings and section order, decodes instructions,
and skips custom-section payloads after validating their names. Structural and
instruction validation then use the same validator as text compilation.

The CLI accepts one binary input per invocation. When writing binary input to
another file, it preserves the original bytes after validation rather than
re-encoding them; custom sections are therefore retained. Text compilation does
not generate custom sections.

## Shared module model

`include/watx/model.h` defines types, imports, functions, globals, memories,
tables, exports, start metadata, and active segments. Imported and defined items
share their corresponding index-space vector, with imports first. Instructions
store opcodes, indices, block types, branch tables, memory alignment/offsets,
and little-endian constant bits.

The module must remain alive and unchanged while instances or native code use
it. The embedding application validates a module before instantiation or JIT
preparation; the CLI performs this automatically.

## Runtime

`src/runtime/runtime.cpp` owns instantiation and interpreted execution.
`RuntimeValue` carries one of the four MVP types and its bits. Each call has
locals, an operand stack, a control stack, and an instruction position.
`runtime_step` executes one instruction and returns the next position, or a
sentinel when the call finishes or traps.

Instantiation resolves host imports, allocates defined resources, evaluates
constant expressions, checks active segment bounds, applies segments, and
optionally invokes the start function. Calls check argument and result types.
Linear memory access checks include the full effective address and access width.
Fuel, call depth, and memory-page limits apply through shared execution helpers.

`RuntimeImports` is the embedding interface for functions and resource pointers.
The embedding application owns host-provided resources and callback contexts.
The CLI does not install WASI or application imports. Embedding applications
register their own host modules and synchronize access to mutable resources.

## Native baseline JIT

`src/jit/jit.cpp` prepares each defined function and implements `RuntimeExecutor`.
The x86_64 and ARM64 emitters generate an instruction block per Wasm instruction.
A block passes a fixed instruction position and frame pointer to a protected
runtime gateway, then dispatches through a native jump table to the returned
position. The gateway catches exceptions before returning to generated code.

Operations use the same helpers as the interpreter, including control stacks,
calls, bounds checks, fuel, and tracing. This is a helper-based baseline compiler,
not an optimizing JIT. It has no persistent cache or native object-file output.
Its speed and correctness have not been established by testing in this change.

The emitters implement System V and Windows x64 calling conventions on x86_64,
and platform ARM64 conventions on macOS, Linux, and Windows. They preserve the
callee-saved registers used by their generated entry points. Executable-memory
handling lives in `src/jit/platform_code_memory.cpp`:

- Windows publishes writable allocations as executable/read-only and flushes
  the instruction cache.
- POSIX systems publish writable mappings as executable/read-only and
  synchronize instruction caches.
- macOS uses `MAP_JIT` with per-thread write protection. Functions are emitted
  in parallel, aligned to 4096-byte offsets, and published in one module mapping.
  Hardened macOS permits one JIT region, so embedding there is limited to one
  prepared engine per process.

A requested JIT failure is reported instead of silently changing execution mode.
OS executable-memory policies and macOS signing configuration still apply.

## Runtime workers and ownership

`src/platform/threads.cpp` provides independent compiler worker jobs for the
library API. The CLI compiles, validates, and prepares native functions with a
single worker. Execution settings do not change compiler parallelism.

`src/runtime/executor.cpp` supplies `InterpreterExecutor` and `ThreadedExecutor`.
The latter schedules function frames on a native worker pool, optionally using
a prepared JIT as its backend. A worker executes nested calls inline, preserving
call order and avoiding dependent jobs waiting for a free worker. Independent
invocations submitted by an embedding application can occupy different workers.

`--threads N` sets interpreter execution workers. `--jit [N]` chooses native
execution and its execution worker count. Both require `--run` and are mutually
exclusive. Zero resolves to hardware thread count, while one uses the caller.
The CLI invokes its selected export once. These flags do not introduce guest
thread instructions or repeat a program.

`src/runtime/sync.cpp` supplies native mutexes, conditions, thread lifetime, and
thread-local execution context without C++11 facilities. Runtime calls have
separate operand/local/control stacks and execution budgets. Initialize a module
instance before concurrent invocation and synchronize externally mutated data.

`include/watx/host.h` provides named host modules and typed callback bindings.
`Host::bind` resolves imports before instance construction, and instance
initialization checks the supplied resources. The CLI installs no built-in
application host modules. See [embedding](embedding.md) for an example.

Native code belongs to its `JitEngine`; the engine and module must outlive every
frame using them. The executor's worker pool must remain alive until all callers
finish. Prepared code is immutable during execution.

## Shared library

`watx_core` is a shared target with output name `watx`, exported for CMake
consumers as `watx::watx`. Windows uses explicit `WATX_API` import/export
annotations. Installation supplies headers and a relocatable CMake package;
the CLI carries an appropriate relative runtime library path on Unix systems.
See [embedding](embedding.md) for the artifact layout and C++ ABI requirements.

## Verification status

The macOS CLI and shared library have been built. Runtime execution and tests
have not been run for this change. Existing Node
harnesses exercise emitted Wasm using Node's engine. They are not coverage for
watx's interpreter, binary reader, or generated native code. See
[testing](testing.md) for the deferred verification plan.
