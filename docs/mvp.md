# WebAssembly 1.0 MVP support

watx translates WAT module declarations and function instructions to WebAssembly
1.0 MVP binary. The supported module surface covers the standard sections and
four importable MVP index spaces, plus function types.

## Module declarations

The compiler supports explicit function types, functions, tables, memories,
globals, imports, exports, one optional start function, and active element and
data segments. Each import kind—function, table, memory, or global—is supported.
Imports must precede module definitions. Imports and definitions use separate
function, table, memory, and global indices; references may use numeric indices
or `$name` identifiers. Function and type references can point forward.

Function types use `i32`, `i64`, `f32`, and `f64` parameters and at most one
result. A bare `(type $t)` use inherits the declared signature. If inline
`(param ...)` or `(result ...)` clauses accompany a type use, their complete
signature must match the referenced type. Without an explicit type use,
matching implicit types are interned and reused. Parameter and local
identifiers map to function-local indices.

Implicit type uses are expanded in source order, including signatures on
indirect calls. Numeric type references can refer forward to those synthesized
types. Empty parameter and result groups expand away before type-use matching.

Tables use `funcref` (legacy `anyfunc` is accepted in text). Memories have
32-bit byte addresses and 64 KiB pages, with at most 65,536 pages. MVP permits
at most one table and one memory. Limits have a
minimum and an optional maximum; the maximum cannot be below the minimum.
Globals may be immutable or mutable. Their initializers, plus segment offsets,
are restricted to MVP constant expressions accepted by the validator.

Exports can be declared at module level or inline on functions, tables,
memories, and globals; multiple inline exports are supported. Inline table
`(elem ...)` and memory `(data ...)` forms create active segments. Explicit
segments accept an offset expression and function references or data strings.
The writer emits the type, import, function, table, memory, global, export,
start, element, code, and data sections in their required order.

## Instruction text

Function bodies accept both flat WAT instructions and folded instruction
expressions. Supported MVP instruction families include:

- Control flow: `unreachable`, `nop`, `block`, `loop`, `if`, `else`, `end`,
  `br`, `br_if`, `br_table`, `return`, and `call`.
- Indirect calls: `call_indirect` with an explicit or matching function type
  and the MVP default table.
- Stack and variables: `drop`, `select`, `local.get`, `local.set`,
  `local.tee`, `global.get`, and `global.set`.
- Memory: all MVP loads and stores, including narrow integer loads/stores,
  plus `memory.size` and `memory.grow`.
- Constants and numeric operations: `i32`, `i64`, `f32`, and `f64` constants;
  comparisons, arithmetic, bit operations, floating-point operations,
  conversions, and reinterpretations.

The validator checks operand and control-stack types, function and local
indices, memory/table use, global mutability, segment references, start
signatures, exports, and MVP limits before the writer produces output.

## WebAssembly threads proposal extension

Threads are post-MVP. watx also accepts the shared-memory and atomic
instruction encoding from the [WebAssembly threads proposal](https://github.com/WebAssembly/threads/blob/main/proposals/threads/Overview.md).
Shared memories use an explicit maximum and the `shared` marker after the
limits, for example `(memory 1 2 shared)` or
`(import "env" "memory" (memory 1 2 shared))`. The binary reader accepts the
proposal's shared-memory limits flag (`0x03`), and the writer emits it for
shared memories.

The compiler recognizes `atomic.fence`, `memory.atomic.notify`,
`memory.atomic.wait32`, `memory.atomic.wait64`, atomic loads and stores, and the
`add`, `sub`, `and`, `or`, `xor`, `xchg`, and `cmpxchg` read-modify-write
families. Atomic memory operands must use their natural alignment; a weaker or
stronger alignment is rejected. The proposal permits atomic accesses on either
shared or unshared memory. Wait operations trap on unshared memory at runtime;
notify remains valid on unshared memory.

The proposal leaves thread creation and joining to the embedder and does not
define a `thread.spawn` instruction. Shared-memory synchronization does not by
itself create a guest thread. Runtime behavior and native target support remain
under development and have not been verified in this change.

## Text fragments and includes

Inputs may be bare module fields or use a `(module ...)` wrapper. Multiple input
files are combined in argument order into one module. `(include "file.wat")`
inserts another file's tokens at that position; paths are relative to the file
containing the include. Nested includes are supported, cycles are rejected, and
included files should contain module fields rather than a module wrapper. A
wrapper is accepted when the file is passed as a top-level input.

The compiler accepts WAT module text, not the `.wast` script command language.
It writes `.wasm` binaries and decodes binary modules into the same validated
representation. The runtime executes modules with the interpreter or the native
baseline JIT. Custom sections are skipped by the decoder and are not currently
emitted by the text compiler. The new binary reader and execution paths remain
untested in this change.
