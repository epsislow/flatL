# Suite `types` — strict types T1

Runtime enforcement for optional `name:type` on **ASSIGN** (`var = …` / native `-> var` not typed in T1).

Panic cases use `expect_exit = 1` and `stderr_contains = "type.*"`.
