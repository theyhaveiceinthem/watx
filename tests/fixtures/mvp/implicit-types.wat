(module

  (func $forward (export "forward") (type 1)
    local.get 0)

  (func $empty)
  (table 1 funcref)

  (func $indirect (export "indirect") (type 0)
    i32.const 42
    i32.const 0
    call_indirect (param i32) (result i32)
    drop)

  (func $folded (export "folded") (type 0)
    (drop
      (call_indirect (param i64) (result i32)
        (i64.const 42) (i32.const 0))))

  (func $later (export "later") (type 1)
    local.get 0)
  (func $callback (param i32) (result i32)
    local.get 0)
  (elem (offset i32.const 0) $callback))
