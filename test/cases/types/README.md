# Suite `types` — strict types T1 + T2

Runtime enforcement for optional `name:type` on **ASSIGN**, including **`var:<schema>`** and **`<schema>:`** declarations (SCHM in `.fb` v6).

Cases **012–018** cover schema compile/runtime (`null`-fill, subset, cycle, unknown schema, etc.).
Cases **019–023** cover **T3** path assign, unknown field on path, nested null materialize, compound LHS, range on path.

Panic cases use `expect_exit = 1` and `stderr_contains = "type.*"`.
