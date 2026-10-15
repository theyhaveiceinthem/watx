(module $core
  (type $unary (func (param i32) (result i32)))
  (type $binary (func (param i32 i32) (result i32)))
  (import "host" "sum" (func $sum (type $binary) (param) (result)))
  (import "host" "offset" (global $offset i32))
  (memory (export "memory") 1 2)
  (table (export "table") 2 2 funcref)
  (global $counter (export "counter") (mut i32) (i32.const 0))
  (data (global.get $offset) "watx\00")
  (elem (i32.const 0) $increment $double)

  (func $increment (type $unary)
    (i32.add (local.get 0) (i32.const 1)))
  (func $double (type $unary)
    local.get 0 i32.const 2 i32.mul)
  (func $initialize
    (global.set $counter (i32.const 1)))
  (start $initialize)
  (func (export "callback") (type $binary)
    (call $sum (local.get 0) (local.get 1)))
  (func (export "indirect") (param i32 i32) (result i32)
    local.get 0 local.get 1 call_indirect (type $unary))
  (func $factorial (export "factorial") (param $n i32) (result i32)
    (if (result i32) (i32.le_u (local.get $n) (i32.const 1))
      (then (i32.const 1))
      (else (i32.mul (local.get $n)
        (call $factorial (i32.sub (local.get $n) (i32.const 1)))))))
  (func (export "sumTo") (param $n i32) (result i32) (local $sum i32)
    block $exit
      loop $again
        local.get $n i32.eqz br_if $exit
        local.get $sum local.get $n i32.add local.set $sum
        local.get $n i32.const 1 i32.sub local.set $n
        br $again
      end $again
    end $exit
    local.get $sum)
  (func (export "choose") (param i32) (result i32)
    block $outer (result i32)
      block $inner (result i32)
        i32.const 42 local.get 0 br_table $inner $outer
      end
      drop
      i32.const 7
    end)
  (func (export "storeLoad") (param i32) (result i32)
    (i32.store offset=4 align=4 (i32.const 64) (local.get 0))
    (i32.load offset=4 align=4 (i32.const 64)))
  (func (export "grow") (param i32) (result i32)
    local.get 0 memory.grow)
  (func (export "setCounter") (param i32)
    (global.set $counter (local.get 0)))
  (func (export "unsigned64") (result i64)
    i64.const 18_446_744_073_709_551_615)
  (func (export "nanPayload") (result i64)
    (i64.reinterpret_f64 (f64.const nan:0x1)))
  (func (export "hexFloat") (result f64)
    f64.const -0x1.8p+2)
)
