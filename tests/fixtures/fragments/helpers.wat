
(func (export "twice") (param i32) (result i32)
  (i32.mul (local.get 0) (i32.const 2)))
