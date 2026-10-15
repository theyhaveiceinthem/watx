
(include "helpers.wat")
(func (export "add") (param $left i32) (result i32)
  local.get $left
  (include "seven.wat")
  i32.add)
