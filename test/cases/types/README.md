# Suite `types` — strict types T1 + T2

Runtime enforcement for optional `name:type` on **ASSIGN**, including **`var:<schema>`** and **`<schema>:`** declarations (SCHM in `.fb` v6).

Cases **012–018** cover schema compile/runtime (`null`-fill, subset, cycle, unknown schema, etc.).
Cases **019–023** cover **T3** path assign, unknown field on path, nested null materialize, compound LHS, range on path.
Cases **024–029** cover **T4** dynamic `:/expr`, `dSchemaAllow` / `dSchemaList`, schema unknown / denied / retype.
Cases **056–057** cover **`dSchemaList([])`** fail-closed and whitelist recovery after invalid update.
Cases **058–061** cover legacy untyped decimal integer limits; cases **107–111** cover full-width typed integer literals, signed minimum, exact decimal-to-integer coercion, and explicit `f32` rounding.
Cases **062–065** cover strict **schema field** type specs at compile time.
Cases **066–071** cover typed **path index**, **list slots** `xs:<schema>[N]`, and **compare** guards.
Cases **072–075** cover corrupt **SCHM** in `.fb` at **`flvm` load** (`invalid SCHM section`).
Cases **076–086** cover **T5** **`const`**, **`freeze()`**, **`free`** clearing flags, compile rejects, **`io.ctxout`** on frozen ref, and path assign on const/frozen schema objects.
Cases **087–093** cover **T4+** **`m.calc(expr, "u8")`**, homogeneous typed vars, overflow → **`type.calc.range`**, and invalid type arg → **`type.calc.type`**.
Cases **094–095** cover **T4+** typed **`io.inln(prompt, "tN")`** (`type.io.len`).
Cases **096–100** cover **T6** typed **`O.set2`** / **`O.set`**, **`def a:u8 … -> out:u16`**, and param overflow at call bind.
Cases **113–119** cover **T6** typed **`def` at call**: caller **`u32` → formal `u8`** (value in range / at 255 / 256 panic), return **`-> r:u32`** with formal **`u16`**, typed return overflow in body, multi-return with wider call LHS.
Cases **120–134** cover **T6** typed **`def`** across **`u*`/`s*`/`f*`** at call: negative into **`u8`**, **`u16`→`s8`**, **`f64`/`f32`→integer**, integer→**`f32`**, fractional→integer panic, typed return across the same families.
Cases **135–142** cover **`f64`/`f32` loss**, **`u64`/`s64`** zero and max at **`def`** call.
Cases **143–147** cover **AM-NUM visible** relational **guards** in **`program.fl`** (`<`/`>`/`<=`/`>=`/`==`, wide integers, mixed **`u64`/`s64`**, **`f32`**) — complements C harness **`108`**.
Cases **101–106** cover **T7** **`t128`…`t8192`**, schema **`t255`**, compile reject **`t8193`**, **`io.inln` + `t1024`**, **`type.assign.string.len`** on overflow.

Panic cases use `expect_exit = 1` and `stderr_contains = "type.*"` (or `invalid SCHM` for load tests).
Compile-only rejects use `kind = "exec"` on **`flc`** (e.g. **080–081**).
