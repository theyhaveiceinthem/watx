<div align="center">

<h1>watx</h1>
<h3>WebAssembly Extended</h3>

<p><strong>From WebAssembly text to execution. Built from scratch in C++98.</strong></p>

<p>
  <a href="#installation"><img src="https://img.shields.io/badge/version-0.2.0-8b5cf6?style=for-the-badge" alt="Version 0.2.0"></a>
  <a href="#build"><img src="https://img.shields.io/badge/C%2B%2B-98-00599C?style=for-the-badge&amp;logo=cplusplus&amp;logoColor=white" alt="Written in C++98"></a>
  <a href="#mvp-scope"><img src="https://img.shields.io/badge/WebAssembly-MVP-654FF0?style=for-the-badge&amp;logo=webassembly&amp;logoColor=white" alt="WebAssembly MVP"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-22c55e?style=for-the-badge" alt="MIT License"></a>
  <a href="#project-status"><img src="https://img.shields.io/badge/status-experimental-f59e0b?style=for-the-badge" alt="Experimental status"></a>
</p>

<p>
  <a href="#platforms"><img src="https://img.shields.io/badge/macOS-11%2B-111827?style=flat-square&amp;logo=apple&amp;logoColor=white" alt="macOS 11 and newer target"></a>
  <a href="#platforms"><img src="https://img.shields.io/badge/Linux-x86__64%20%7C%20ARM64-111827?style=flat-square&amp;logo=linux&amp;logoColor=white" alt="Linux x86_64 and ARM64 targets"></a>
  <a href="#platforms"><img src="https://img.shields.io/badge/Windows-10%2B-111827?style=flat-square" alt="Windows 10 and newer target"></a>
  <a href="#library-artifacts"><img src="https://img.shields.io/badge/library-dylib%20%7C%20so%20%7C%20dll-0f766e?style=flat-square" alt="Shared library targets: dylib, so, dll"></a>
</p>

<p>
  <a href="#installation">Install</a> ·
  <a href="#quick-start">Quick start</a> ·
  <a href="#embedding-and-imports">Host modules</a> ·
  <a href="#flags">Flags</a> ·
  <a href="docs/embedding.md">Embedding</a> ·
  <a href="SECURITY.md">Security</a>
</p>

</div>

---

<details open>
<summary><strong>Contents</strong></summary>

- [Project status](#project-status)
- [Installation](#installation)
- [Quick start](#quick-start)
- [Build](#build)
- [Library artifacts](#library-artifacts)
- [Compile](#compile)
- [Textual includes](#textual-includes)
- [Run an exported function](#run-an-exported-function)
- [Native JIT](#native-jit)
- [Flags](#flags)
- [Raw input and output](#raw-input-and-output)
- [Debugging and execution limits](#debugging-and-execution-limits)
- [Platforms](#platforms)
- [Embedding and imports](#embedding-and-imports)
- [MVP scope](#mvp-scope)
- [Testing](#testing)
- [Troubleshooting](#troubleshooting)
- [Community](#community)
- [License](#license)

</details>

## Project status

watx implements its own text parser, module validator, binary encoder, binary
decoder, interpreter, and native code emitters. It does not depend on an external
WebAssembly compiler or runtime.

**The runtime, JIT, and shared-library API are experimental.** Builds and smoke
tests passed on all six platform and CPU combinations for version 0.2.0. Full
MVP conformance and broader runtime correctness still need testing. This is an
evolving implementation, without a production-readiness or performance guarantee.

## Installation

<p align="center">
  <strong>macOS · Linux · Windows</strong><br>
  <sub>Native x86_64 and ARM64 packages · CLI and shared library included</sub>
</p>

The installers download the matching package from
[GitHub Releases](https://github.com/theyhaveiceinthem/watx/releases), verify its
SHA-256 checksum, unpack it, and install the executable, libraries, public
headers, CMake package, and license. The default version is **0.2.0**, which is
available as a pre-release. No compiler or administrator access is needed for
the default installation locations.

### macOS and Linux

Requires `curl`, `tar`, and either `shasum` or `sha256sum`.

```sh
curl -fL https://raw.githubusercontent.com/theyhaveiceinthem/watx/main/install.sh -o install.sh
sudo sh install.sh
export PATH="$HOME/.local/bin:$PATH"
watx --help
```

The default prefix is `$HOME/.local`: the CLI goes in `bin`, libraries in `lib`,
headers in `include/watx`, and CMake files in `lib/cmake/watx`. Add the `export`
line to your shell configuration, such as `~/.zshrc` or `~/.bashrc`, to keep watx
on your PATH in new terminals. Library symlinks and the executable's relative
library lookup paths are preserved; no `LD_LIBRARY_PATH` or `DYLD_LIBRARY_PATH`
setting is needed for the CLI.

Choose a release and an absolute installation prefix:

```sh
sudo sh install.sh --version v0.2.0 --prefix "$HOME/tools/watx"
export PATH="$HOME/tools/watx/bin:$PATH"
```

Linux release binaries are built on Ubuntu 24.04 and require a compatible glibc
environment. For other Linux environments, use the [source build](#build).

### Windows

Run these commands in **PowerShell 5.1 or newer**:

```powershell
Invoke-WebRequest -UseBasicParsing https://raw.githubusercontent.com/theyhaveiceinthem/watx/main/install.ps1 -OutFile install.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\install.ps1
```

The default location is `%LOCALAPPDATA%\Programs\watx`. The installer places
`watx.exe` and `watx.dll` together in `bin`, installs the import library and
embedding files, and adds `bin` to your user PATH. Open a new terminal, then run
`watx --help`. The execution-policy option applies only to that installer
process.

Choose a release and an absolute installation directory:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\install.ps1 -Version v0.2.0 -InstallDir "$env:LOCALAPPDATA\Programs\watx"
```

Pass `-NoPath` to leave your user PATH unchanged. Windows binaries use the MSVC
runtime; install the matching Microsoft Visual C++ runtime if required by your
system.

### Manual installation and embedding

Download the archive for your OS and CPU from
[version 0.2.0](https://github.com/theyhaveiceinthem/watx/releases/tag/v0.2.0):
`.tar.gz` for macOS/Linux or `.zip` for Windows. Unpack it and keep its `bin` and
`lib` directories together under the same prefix. Add `bin` to PATH. The
`SHA256SUMS.txt` release asset contains the archive checksums.

For C++ projects, set `CMAKE_PREFIX_PATH` to the installed prefix, then use
`find_package(watx CONFIG REQUIRED)` and link `watx::watx`. See the
[embedding guide](docs/embedding.md).

## Quick start

Save this as `add.wat`:

```wat
(module
  (func (export "add") (param i32 i32) (result i32)
    local.get 0
    local.get 1
    i32.add))
```

```sh
watx add.wat -o add.wasm
watx add.wasm --run --function add -- i32:10 i32:5
watx add.wasm --run --function add --jit 0 -- i32:10 i32:5
```

A successful invocation of `add` prints `i32:15`. There are no subcommands: input
paths and flags select the operation.

## Build

Use CMake 3.16 or newer and a C++ compiler with C++98 language support. Modern Clang, GCC, or
MSVC toolchains also provide the fixed-width integer and platform APIs used here.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build --config Release --parallel
```

On macOS and Linux the executable is normally `build/watx`; with a Visual Studio
multi-configuration generator it is normally `build/Release/watx.exe`. The
examples below assume the executable's directory is on PATH. For a local Unix
build, use `export PATH="$PWD/build:$PATH"`; in PowerShell, use
`$env:Path = "$PWD\build\Release;$env:Path"` with a Visual Studio generator.

The project links the operating system thread library and the system math
library where needed. It uses no C++11 thread or language facilities.

## Library artifacts

Each native build produces the command-line tool **and** a shared library:

- **macOS:** `watx` and `libwatx.dylib`.
- **Linux:** `watx` and `libwatx.so`.
- **Windows:** `watx.exe`, `watx.dll`, and the import library `watx.lib` with MSVC.

Unix builds also produce versioned library files and the unversioned symlink.
Keep the built executable with its shared library, or install both:

```sh
cmake --install build --config Release --prefix ./install
```

Installation places executables and Windows DLLs in `bin`, Unix libraries and
Windows import libraries in the platform's library directory, public headers in
`include`, and a CMake package in the library directory under `cmake/watx`.
The package exposes `watx::watx` for embedding. See
[the embedding guide](docs/embedding.md) for linking and ABI requirements.

## Compile

```sh
watx main.wat -o app.wasm
watx main.wat helpers.wat -o app.wasm
watx main.wat --validate-only
watx app.wasm --validate-only --debug
watx app.wasm --dump --dump-hex
```

WAT inputs are assembled into one module in argument order. Each top-level file
may contain bare module fields or one `(module ...)` wrapper. This combines text
before resolving references; it is not a binary linker. A `.wasm` input must be
the only input. With a binary input, `-o` copies the validated original bytes,
including custom sections.

## Textual includes

`(include "path")` inserts the included file's tokens exactly where it appears.
Included files do not need a module wrapper, and may contain instruction
fragments inside a function.

<details open>
<summary><strong>Example: a function assembled from two files</strong></summary>

`main.wat`:

```wat
(module
  (func (export "add") (param i32 i32) (result i32)
    (include "fragments/add-body.wat")))
```

`fragments/add-body.wat`:

```wat
local.get 0
local.get 1
i32.add
```

```sh
watx main.wat -o add.wasm
```

</details>

When an input is already included by another top-level input, the CLI skips the
redundant top-level copy. For example, if `math.wat` includes `add.wat`, both
`watx math.wat -o math.wasm` and `watx add.wat math.wat -o math.wasm` assemble
that included file once. Repeated include forms inside source remain textual
inclusions. Distinct declarations with the same export name are still rejected.

Paths are relative to the including file. Nested includes are supported; cycles
and nesting beyond the implementation limit are rejected. Includes from
standard input resolve relative to the current directory. The include form is a
watx text extension; the output is ordinary MVP Wasm.

## Run an exported function

```sh
watx app.wasm --run --function add -- i32:10 i32:5
watx app.wat --run --function add -- i32:10 i32:5
watx app.wasm --run --function answer
```

`--run` requires `--function NAME`, where `NAME` is a function export. Arguments
are optional for functions without parameters. All argument tokens must follow
`--`, using a type prefix:

- `i32:10`, `i32:-10`, `i32:0xff`
- `i64:123456789`
- `f32:1.5`, `f32:-0`
- `f64:0x1.8p+2`, `f64:inf`, `f64:nan`

Argument counts and types must match the export. One result is printed per line
with its type prefix; a function with no result prints nothing. `--raw-results`
omits the prefix. Start functions execute during instantiation unless
`--no-start` is set.

## Native JIT

```sh
watx app.wasm --run --function add --jit -- i32:10 i32:5
watx app.wasm --run --function add --jit 4 -- i32:10 i32:5
watx app.wasm --run --function add --jit 0 -- i32:10 i32:5
```

The interpreter is the default. `--jit` selects a native **baseline JIT** for the
current machine. It emits a native block for each Wasm instruction and calls
shared runtime helpers for its operations. Native dispatch follows the next
instruction selected by the runtime. This is a helper-based implementation,
without an optimizing compiler or a verified speed advantage.

JIT preparation compiles all defined functions before execution. `--jit` requires
`--run`. Its optional count controls execution workers, and does not change
parsing, validation, or native preparation worker counts.
Generated machine code is held in memory; `-o` always writes a Wasm module.
There is no native object output or persistent JIT cache. Unsupported targets or
executable-memory failures produce an error.

### Execution workers

```sh
watx app.wasm --run --function add --threads 4 -- i32:10 i32:5
watx app.wasm --run --function add --jit 4 -- i32:10 i32:5
```

`--threads N` configures interpreter execution workers. `--jit [N]` selects the
JIT and configures its execution workers. These flags are mutually exclusive;
both require `--run`. `0` selects detected hardware threads, and `1` executes on
the calling thread. The default is one.

These are runtime settings. They do not add instructions to WAT, alter module
semantics, or repeat the selected function. The execution pool schedules function
frames; dependent instructions and nested calls retain their required order.
A larger pool does not automatically make one sequential invocation faster.
Embedding applications can submit concurrent invocations to use multiple workers.

## Flags

### Inputs and output

- `-o PATH`, `--output PATH`: write a validated Wasm binary.
- `--wat`: force WAT input.
- `--wasm`, `--raw-input`: force binary Wasm input.
- `--raw-output`: write binary bytes to standard output; equivalent to `-o -`.
- `-`: read an input from standard input, once per invocation.

### Execution

- `--run`: invoke an exported function after validation and instantiation.
- `--function NAME`: select the export; required with `--run`.
- `--jit [N]`, `--jit=N`: select JIT execution and optionally set its execution workers.
- `--interpreter`: select interpreted execution.
- `--threads N`, `-j N`, `-jN`: set interpreter execution workers; requires `--run`.
- `--no-start`: skip the module's start function.
- `--raw-results`, `--raw`: omit result type prefixes.
- `--`: end option parsing and introduce typed function arguments.

### Inspection and limits

- `--validate`, `--validate-only`: validate without writing an output file.
- `--dump`: print module counts, signatures, imports, and exports.
- `--dump-hex`: print the binary bytes in hexadecimal.
- `--debug`, `-g`: print target, engine, thread budgets, stage information, and CPU time.
- `--trace`: print each executed instruction to standard error.
- `--fuel N`: allow N Wasm instruction steps; `0` means unlimited.
- `--stack-limit N`: limit nested defined-function calls; default `1024`.
- `--memory-limit PAGES`: cap linear memory; default `65536` pages.
- `--version`: print the version and native target.
- `-h`, `--help`: print usage.

Long value options also accept `--name=value`. Unknown options and invalid
combinations are rejected. Exit status is `0` for success, `2` for option errors,
and `1` for compilation, decoding, validation, output, or execution failures.

## Raw input and output

```sh
watx main.wat --raw-output > main.wasm
watx --raw-input - --run --function add -- i32:10 i32:5 < main.wasm
watx main.wasm --run --function add --raw -- i32:10 i32:5
```

Raw output is the binary itself, with no textual header. Diagnostics go to
standard error; dumps also go there when standard output carries binary bytes.
Raw binary output cannot be combined with `--run`. Raw results are ordinary
numeric text with the type prefix removed.

## Debugging and execution limits

```sh
watx app.wasm --run --function work --debug --trace --fuel 100000
watx app.wasm --run --function work --stack-limit 256 --memory-limit 1024
```

`--debug` enables diagnostics; it does not emit DWARF or Wasm name sections.
`--trace` works with the interpreter and JIT through their shared runtime
helpers. A memory page is 64 KiB; a cap of `1024` is 64 MiB. Memory allocation
can fail below the configured cap. Fuel limits an invocation and its nested calls, and traps when exhausted. Execution failures report a Wasm
trap or runtime error with a nonzero exit code.

## Platforms

Native code emitters cover **x86_64** and **ARM64** for each of:

- **macOS 11+:** Clang, POSIX threads, `MAP_JIT` executable mappings, and
  per-thread JIT write protection.
- **Linux:** GCC or Clang, POSIX threads, executable mappings, and instruction
  cache synchronization.
- **Windows 10+:** MSVC, Win32 threads and processor groups, `VirtualAlloc`,
  executable protection changes, and instruction cache synchronization.

Build for the processor on which watx will run. On macOS, select a slice with
`-DCMAKE_OSX_ARCHITECTURES=x86_64` or `-DCMAKE_OSX_ARCHITECTURES=arm64`.
With a Visual Studio generator, use `-A x64` or `-A ARM64`. On Linux, select a
native toolchain or provide the appropriate CMake cross-compilation toolchain.
The JIT emits code for the watx executable's architecture, including when that
executable runs under OS translation.

macOS requires a modern SDK exposing `MAP_JIT` and the JIT write-protection
APIs. Hardened-runtime distribution requires the appropriate JIT code-signing
entitlement; [the supplied plist](packaging/macos/watx.entitlements) enables
`com.apple.security.cs.allow-jit`. The current publication API does not use
callback allowlists, so do not add `com.apple.security.cs.jit-write-allowlist`.
[Apple documents these requirements](https://developer.apple.com/documentation/apple-silicon/porting-just-in-time-compilers-to-apple-silicon).
JIT publication uses one mapping per engine. Under hardened macOS, use one
prepared engine per process because the entitlement permits one JIT region.
OS policies that prohibit dynamic executable memory prevent JIT use; interpreter
execution does not allocate native code. Version 0.2.0 passed native build and
smoke checks on all six targets; this does not establish full MVP conformance.

## Embedding and imports

Link the installed `watx::watx` CMake target and use the public headers under `include/watx/`.
`RuntimeImports` binds host functions, memories, tables, and globals;
`RuntimeInstance` initializes a validated module and invokes functions.
`JitEngine` implements the same execution interface as the interpreter.

Developers can define custom host modules with `include/watx/host.h`.
`HostModule` registers typed callbacks and resource bindings; `Host::bind` checks
required function signatures and produces imports for a `RuntimeInstance`.
The CLI does not install application host modules or implement WASI. The
[embedding guide](docs/embedding.md) includes a host-module example. Host bindings and the module
must outlive their runtime instance, and a JIT executor must outlive any invocation
that uses it. Table function references retain an instance pointer, so an
instance referenced by a shared table must also stay alive. Instances are not
safe for arbitrary concurrent mutation. Initialize an instance before submitting
concurrent invocations, synchronize external resource access, and keep host
callbacks safe for the concurrency your application uses.

## MVP scope

The compiler targets [WebAssembly 1.0 core](https://webassembly.github.io/spec/versions/core/WebAssembly-1.0.pdf):

- Four numeric types, MVP instructions, structured control flow, calls, memory,
  globals, tables, and indirect calls.
- All MVP module sections and import kinds, exports, a start function, and
  active data and element segments.
- Flat and folded WAT, symbolic and numeric indices, and declaration
  abbreviations. See the [MVP guide](docs/mvp.md).
- Binary decoding, shared validation, initialization, and interpreted or native
  execution of the same instruction representation.

WASI, a `.wast` command interpreter, custom section generation, and native object
generation are outside the documented MVP scope. Binary custom
sections are skipped for execution and retained when copying binary input.

## Testing

Version 0.2.0 passed native builds and tests on macOS, Linux, and Windows on
x86_64 and ARM64. Each job ran the native validator checks and CLI smoke checks
for the interpreter, JIT, execution worker settings, numeric arguments, memory,
start functions, calls, traps, fuel limits, and includes. Installation and
artifact packaging also succeeded. The workflow was removed after those runs;
the native tests remain available through CTest.

These are initial checks rather than a full conformance suite. The optional
Node conformance harness remains available for broader compiler coverage.
The release installers have not yet been run through installation tests.
See the [testing guide](docs/testing.md).

## Troubleshooting

<details>
<summary><strong>Duplicate export when combining inputs</strong></summary>

If `math.wat` contains `(include "add.wat")`, compile its root:

```sh
./build/watx math.wat -o math.wasm
```

The CLI also accepts `add.wat math.wat` and skips `add.wat` as a redundant
top-level input. Two distinct declarations exporting the same name still fail
validation. Renaming both copies does not resolve a duplicated declaration.

</details>

<details>
<summary><strong>Unknown option --run</strong></summary>

Rebuild the executable from the current source, then supply the input path:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build --config Release --parallel
./build/watx math.wasm --run --function add -- i32:10 i32:5
```

An older executable may not contain the runtime flags. `--run` also requires a
module path; `--function` selects an export from that module.

</details>

## Community

<p>
  <a href="CONTRIBUTING.md"><img src="https://img.shields.io/badge/contribute-read%20the%20guide-2563eb?style=for-the-badge" alt="Contributing guide"></a>
  <a href="SECURITY.md"><img src="https://img.shields.io/badge/security-report%20privately-dc2626?style=for-the-badge" alt="Security reporting policy"></a>
  <a href="SUPPORT.md"><img src="https://img.shields.io/badge/help-support%20guide-0f766e?style=for-the-badge" alt="Support guide"></a>
</p>

- [Contributing](CONTRIBUTING.md): project conventions and pull requests.
- [Security](SECURITY.md): private vulnerability reporting routes.
- [Support](SUPPORT.md): questions, bug reports, and feature requests.
- [Changelog](CHANGELOG.md): unreleased changes.

Issue forms and the pull request template live in `.github/`.

## License

Licensed under the [MIT License](LICENSE).

Copyright (c) 2026 theyhaveice (theyhaveiceinthem).

---

<div align="center">

<strong>watx</strong><br>
<sub>WebAssembly Extended · From text to execution.</sub><br><br>
<a href="#watx">Back to top ↑</a>

</div>
