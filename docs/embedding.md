# Embedding watx

`watx_core` is the shared library for applications that parse, validate,
compile, or run WebAssembly modules. Install it with CMake, then link the
exported `watx::watx` target:

```cmake
find_package(watx CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE watx::watx)
```

The package config locates the platform thread dependency and supplies the
installed include directory. Public headers are installed under `include/watx`.
The CMake target propagates the Windows DLL import declarations to consumers.

The CLI and library are installed together. On Windows, `watx.exe` and
`watx.dll` are both installed under `bin`; the import library is installed
under `lib`. On Linux and macOS, the CLI is installed under `bin` and the
versioned shared library and its `libwatx` linker name are installed under
`lib`. The CLI carries a relative runtime path to that library directory.

Applications using the C++ API must use a compatible C++ compiler and standard
library ABI. On Windows, use a compatible MSVC runtime configuration across
the application and library.

The native JIT is available on 64-bit x86 and ARM systems. Hardened macOS
applications that use the JIT need the `com.apple.security.cs.allow-jit`
entitlement. Each prepared `JitEngine` owns one executable mapping; Apple's
current `MAP_JIT` process limit means an application should use one prepared
engine per process.

## Custom host modules

Include `watx/host.h` to register application modules. Function registrations
include their Wasm signature, callback, and an optional context pointer. The
host registry copies bindings and borrows callback contexts and resource pointers.

```cpp
#include <watx/host.h>
#include <watx/runtime/executor.h>
#include <watx/validator.h>

static std::vector<watx::RuntimeValue> host_add(
    void *, const std::vector<watx::RuntimeValue> &arguments) {
    uint32_t result = arguments[0].as_u32() + arguments[1].as_u32();
    return std::vector<watx::RuntimeValue>(1, watx::RuntimeValue::i32(result));
}

std::vector<watx::RuntimeValue> run_with_host(const watx::Module &module) {
    watx::validate_module(module);
    watx::FunctionType signature;
    signature.parameters.push_back(watx::TYPE_I32);
    signature.parameters.push_back(watx::TYPE_I32);
    signature.results.push_back(watx::TYPE_I32);

    watx::HostModule math("math");
    math.add_function("add", signature, &host_add);
    watx::Host host;
    host.add_module(math);

    watx::ThreadedExecutor executor(4);
    watx::RuntimeOptions options;
    watx::RuntimeInstance instance(module, options, host.bind(module));
    instance.set_executor(&executor);
    instance.initialize();

    std::vector<watx::RuntimeValue> arguments;
    arguments.push_back(watx::RuntimeValue::i32(10));
    arguments.push_back(watx::RuntimeValue::i32(5));
    return instance.invoke_export("calculate", arguments);
}
```

The corresponding module can import that function:

```wat
(module
  (import "math" "add" (func $add (param i32 i32) (result i32)))
  (func (export "calculate") (param i32 i32) (result i32)
    local.get 0
    local.get 1
    call $add))
```

`HostModule::add_memory`, `add_table`, and `add_global` register resource
pointers. `Host::bind(module)` resolves required imports and checks function
signatures; instance initialization also checks resource compatibility. Missing
bindings, duplicate exports, and mismatched function signatures produce errors.
Host callbacks return typed values, and the runtime checks their result types.

## Execution workers

`ThreadedExecutor(N)` runs interpreted function frames through runtime workers.
To execute native frames, prepare a `JitEngine` first, then pass its address as
the second constructor argument. The selected backend and module must outlive
the executor and all invocations.

A count of zero selects detected hardware threads; one executes directly on the
calling thread. Larger counts create a worker pool. Nested calls from a worker
run inline to preserve call order and avoid exhausting the pool with dependent
calls. The CLI runs its selected export once; multiple workers do not implicitly
repeat it or change its WAT instructions.

Embedding applications can submit independent invocations concurrently after
instance initialization. Host callbacks and their contexts must support the
concurrency used by the application. Synchronize external resource mutation,
and finish all invocations before destroying the instance or executor. No
built-in guest thread imports or application modules are installed by the CLI.

## Verification

The native embedding checks cover custom host callbacks, argument order, direct
and indirect calls, host result errors, start functions, fuel, and execution
workers with both engines. Optimization checks have run on ARM64 macOS and on
x86_64 macOS through Rosetta. The new optimization paths still need native
Windows and Linux verification. Installed package consumer tests remain separate
from these runtime checks.

Modules must remain immutable while instances or prepared JIT engines use them.
The runtime caches decoded instructions and control targets when an instance is
created. A JIT engine rejects execution with an instance for a different module
object. Fuel, tracing, and cancellation contexts select the checked instruction
path instead of the direct numeric path.
