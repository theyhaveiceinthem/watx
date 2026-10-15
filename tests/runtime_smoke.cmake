if(NOT DEFINED WATX_EXECUTABLE OR NOT EXISTS "${WATX_EXECUTABLE}")
    message(FATAL_ERROR "WATX_EXECUTABLE must point to the built CLI")
endif()
if(NOT DEFINED WATX_TEST_DIRECTORY)
    message(FATAL_ERROR "WATX_TEST_DIRECTORY is required")
endif()
file(MAKE_DIRECTORY "${WATX_TEST_DIRECTORY}")

function(invoke_watx output)
    execute_process(
        COMMAND "${WATX_EXECUTABLE}" ${ARGN}
        RESULT_VARIABLE status
        OUTPUT_VARIABLE actual
        ERROR_VARIABLE errors
        TIMEOUT 15
    )
    if(NOT "${status}" STREQUAL "0")
        message(FATAL_ERROR "watx ${ARGN} failed (${status}):\n${errors}")
    endif()
    string(STRIP "${actual}" actual)
    set(${output} "${actual}" PARENT_SCOPE)
endfunction()

function(expect_value expected)
    invoke_watx(actual ${ARGN})
    if(NOT "${actual}" STREQUAL "${expected}")
        message(FATAL_ERROR "watx ${ARGN}: expected '${expected}', got '${actual}'")
    endif()
endfunction()

function(expect_error expected)
    execute_process(
        COMMAND "${WATX_EXECUTABLE}" ${ARGN}
        RESULT_VARIABLE status
        OUTPUT_VARIABLE output
        ERROR_VARIABLE errors
        TIMEOUT 15
    )
    if(NOT "${status}" MATCHES "^[0-9]+$" OR "${status}" STREQUAL "0")
        message(FATAL_ERROR "Expected a controlled failure from watx ${ARGN}; got ${status}")
    endif()
    string(FIND "${errors}" "${expected}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Expected error '${expected}', got:\n${errors}")
    endif()
endfunction()

set(source "${WATX_TEST_DIRECTORY}/runtime.wat")
set(binary "${WATX_TEST_DIRECTORY}/runtime.wasm")
file(WRITE "${source}" [=[
(module
  (type $unary (func (param i32) (result i32)))
  (memory 1 2)
  (table 1 funcref)
  (elem (i32.const 0) $increment)
  (data (i32.const 0) "abc")
  (global $counter (mut i32) (i32.const 7))
  (func $start i32.const 9 global.set $counter)
  (start $start)
  (func $increment (type $unary) local.get 0 i32.const 1 i32.add)
  (func (export "add") (param i32 i32) (result i32)
    local.get 0 local.get 1 i32.add)
  (func (export "add64") (param i64 i64) (result i64)
    local.get 0 local.get 1 i64.add)
  (func (export "multiply") (param f64 f64) (result f64)
    local.get 0 local.get 1 f64.mul)
  (func (export "counter") (result i32) global.get $counter)
  (func (export "firstByte") (result i32) i32.const 0 i32.load8_u)
  (func (export "storeLoad") (param i32) (result i32)
    i32.const 4 local.get 0 i32.store i32.const 4 i32.load)
  (func (export "indirect") (param i32) (result i32)
    local.get 0 i32.const 0 call_indirect (type $unary))
  (func $factorial (export "factorial") (param i32) (result i32)
    local.get 0 i32.eqz
    if (result i32)
      i32.const 1
    else
      local.get 0 local.get 0 i32.const 1 i32.sub call $factorial i32.mul
    end)
  (func (export "trap") unreachable)
  (func (export "forever") loop br 0 end))
]=])

invoke_watx(version --version)
message(STATUS "Testing ${version}")
if(DEFINED WATX_EXPECTED_ARCH AND NOT "${WATX_EXPECTED_ARCH}" STREQUAL "")
    string(FIND "${version}" "${WATX_EXPECTED_ARCH}-" architecture)
    if(architecture EQUAL -1)
        message(FATAL_ERROR "Expected native ${WATX_EXPECTED_ARCH}, got '${version}'")
    endif()
endif()
invoke_watx(ignored "${source}" -o "${binary}")
invoke_watx(ignored "${binary}" --validate-only)

function(check_backend label)
    message(STATUS "Checking ${label}")
    expect_value("i32:15" "${binary}" --run --function add ${ARGN} -- i32:10 i32:5)
    expect_value("i64:4294967301" "${binary}" --run --function add64 ${ARGN} -- i64:4294967296 i64:5)
    expect_value("f64:3" "${binary}" --run --function multiply ${ARGN} -- f64:1.5 f64:2)
    expect_value("i32:9" "${binary}" --run --function counter ${ARGN})
    expect_value("i32:97" "${binary}" --run --function firstByte ${ARGN})
    expect_value("i32:-123" "${binary}" --run --function storeLoad ${ARGN} -- i32:-123)
    expect_value("i32:11" "${binary}" --run --function indirect ${ARGN} -- i32:10)
    expect_value("i32:120" "${binary}" --run --function factorial ${ARGN} -- i32:5)
    expect_error("unreachable" "${binary}" --run --function trap ${ARGN})
    expect_error("fuel exhausted" "${binary}" --run --function forever --fuel 50 ${ARGN})
endfunction()

check_backend("interpreter" --interpreter)
check_backend("interpreter workers" --threads 2)
check_backend("interpreter automatic workers" --threads 0)
check_backend("JIT" --jit)
check_backend("JIT workers" --jit 2)
check_backend("JIT automatic workers" --jit 0)

expect_value("15" "${binary}" --run --function add --raw -- i32:10 i32:5)
expect_value("i32:7" "${binary}" --run --function counter --no-start)
expect_error("argument count" "${binary}" --run --function add -- i32:10)
expect_error("argument type" "${binary}" --run --function add -- i64:10 i32:5)
expect_error("requires an input file" --run --function add -- i32:10 i32:5)
expect_error("--jit requires --run" "${binary}" --jit)
expect_error("--threads controls interpreter" "${binary}" --run --function counter --threads 2 --jit 2)

file(WRITE "${WATX_TEST_DIRECTORY}/fragment.wat" [=[
(func (export "includedAdd") (param i32 i32) (result i32)
  local.get 0 local.get 1 i32.add)
]=])
file(WRITE "${WATX_TEST_DIRECTORY}/root.wat" [=[
(module (include "fragment.wat"))
]=])
invoke_watx(ignored "${WATX_TEST_DIRECTORY}/fragment.wat" "${WATX_TEST_DIRECTORY}/root.wat"
            -o "${WATX_TEST_DIRECTORY}/included.wasm")
expect_value("i32:15" "${WATX_TEST_DIRECTORY}/included.wasm" --run --function includedAdd -- i32:10 i32:5)
invoke_watx(ignored "${WATX_TEST_DIRECTORY}/root.wat" "${WATX_TEST_DIRECTORY}/fragment.wat"
            -o "${WATX_TEST_DIRECTORY}/included-reverse.wasm")
expect_value("i32:15" "${WATX_TEST_DIRECTORY}/included-reverse.wasm" --run --function includedAdd -- i32:10 i32:5)

file(WRITE "${WATX_TEST_DIRECTORY}/invalid.wasm" "invalid")
expect_error("WebAssembly" "${WATX_TEST_DIRECTORY}/invalid.wasm" --validate-only)
message(STATUS "Native CLI smoke checks passed")
