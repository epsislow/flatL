# Suite `types` — strict types T1 + T2

Runtime enforcement for optional `name:type` on **ASSIGN**, including **`var:<schema>`** and **`<schema>:`** declarations (SCHM in `.fb` v6).

Cases **012–018** cover schema compile/runtime (`null`-fill, subset, cycle, unknown schema, etc.).
Cases **019–023** cover **T3** path assign, unknown field on path, nested null materialize, compound LHS, range on path.
Cases **024–029** cover **T4** dynamic `:/expr`, `dSchemaAllow` / `dSchemaList`, schema unknown / denied / retype.
Cases **056–057** cover **`dSchemaList([])`** fail-closed and whitelist recovery after invalid update.
Cases **058–061** cover interim **`u64`/`s64`** exact-integer limits (`2^53−1`).
Cases **062–065** cover strict **schema field** type specs at compile time.
Cases **066–071** cover typed **path index**, **list slots** `xs:<schema>[N]`, and **compare** guards.
Cases **072–075** cover corrupt **SCHM** in `.fb` at **`flvm` load** (`invalid SCHM section`).
Cases **076–086** cover **T5** **`const`**, **`freeze()`**, **`free`** clearing flags, compile rejects, **`io.ctxout`** on frozen ref, and path assign on const/frozen schema objects.
Cases **087–093** cover **T4+** **`m.calc(expr, "u8")`**, homogeneous typed vars, overflow → **`type.calc.range`**, and invalid type arg → **`type.calc.type`**.
Cases **094–095** cover **T4+** typed **`io.inln(prompt, "tN")`** (`type.io.len`).
Cases **096–100** cover **T6** typed **`O.set2`** / **`O.set`**, **`def a:u8 … -> out:u16`**, and param overflow at call bind.

Panic cases use `expect_exit = 1` and `stderr_contains = "type.*"` (or `invalid SCHM` for load tests).
Compile-only rejects use `kind = "exec"` on **`flc`** (e.g. **080–081**).
