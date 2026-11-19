# Native module `math.mdim`

Operations on **rectangular**, **homogeneously numeric** LIST values (vectors, matrices, small tensors). **Optional** — declare before use:

```text
use native math.mdim with D
```

Shape comes from list **stamp** or **walk** (same infrastructure as `list.is_rect` / `list.dims`). For list mutation and introspection only, use **`list`**.

## Syntax

| Form | Example |
|------|---------|
| Arrow binding | `D.add(A, B) -> C` |
| Assignment | `s = math.mdim.sum(M)` |
| Canonical name | `math.mdim.matmul(A, B) -> C` |

**Operands:** LIST trees that are **rect** and whose leaves are **NUMBER** (AM-NUM). Element-wise ops (`add`, `subtract`, `multiply`, `divide`) use **NumPy-style trailing broadcast**; incompatible shapes → **`type.mdim.broadcast`**. Same-shape and broadcast cases for **rank 1–3** are covered by tests `math_mdim` 001–010 and **021–024**. Rank ≥3 builds use **`flat_list_build_disjoint`** so list pool ids are not reused over live subtrees (see `FLAT_MDIM_DEBUG` below).

**Debug:** set `FLAT_MDIM_DEBUG=1` when running `flvm` to trace broadcast layout, sublist ids, per-depth builds, and list trees on stderr (`[mdim] …`).

## Functions

### Element-wise (same shape)

| Call | Returns | Description |
|------|---------|-------------|
| `add(a, b)` | LIST | Pairwise `a[i…] + b[i…]` |
| `subtract(a, b)` | LIST | Pairwise difference |
| `multiply(a, b)` | LIST | Hadamard product (not matrix multiply) |
| `divide(a, b)` | LIST | Pairwise division; **divide by zero** → **`math.mdim.divzero`** (ca `math.calc.divzero`) |

### Reductions (one operand)

| Call | Returns | Description |
|------|---------|-------------|
| `sum(x)` | NUMBER | Sum of all numeric leaves |
| `min(x)` | NUMBER | Minimum leaf |
| `max(x)` | NUMBER | Maximum leaf |
| `avg(x)` | NUMBER | Mean of all leaves |

### Linear algebra

| Call | Returns | Description |
|------|---------|-------------|
| `dot(u, v)` | NUMBER | 1D vectors, same length: inner product |
| `transpose(m)` | LIST | Rank-2 swap rows/columns |
| `matmul(A, B)` | LIST | Rank-2; inner dimensions must match |
| `reshape(x, shape)` | LIST | Same element count; `shape` is a 1D list of positive integers (target `dims[]`) |

### Tags (AM-TAGURI v3.0 on `math.mdim`)

Optional trailing section after operands:

```text
D.sum(v ; "u32")
D.dot(u, v ; "vector")
D.matmul(A, B ; "matrix")
D.add(A, B ; "f64")
D.matmul(A, B ; "f64 matrix")
```

| Token kind | Example | Effect |
|------------|---------|--------|
| Numeric type | `; "u32"`, `; "f64"` | Coerce scalar result (`sum`, `dot`, …) or numeric leaves of LIST result (`add`, …) |
| Shape role | `; "vector"`, `; "matrix"` | Operand shape must match (rank 1 / 2); else **`type.mdim.tag_shape`** |
| Unknown token | `; "tensor"` | **`type.tag.not_accepted`** |
| Role not on whitelist | `; "matrix"` on `sum` / `dot` | **`type.tag.not_accepted`** |

**Whitelist role per native**

| Native | `; "vector"` | `; "matrix"` |
|--------|--------------|--------------|
| `add`, `subtract`, `multiply`, `divide`, `reshape` | yes (rank 1) | yes (rank 2) |
| `dot` | yes | no |
| `transpose`, `matmul` | no | yes |
| `sum`, `min`, `max`, `avg` | no | no |

Type tags (`u32`, `f64`, …) are allowed on **all** `math.mdim` functions above.

Without `;`, **M3 promotion** infers the result element type: `u32`+`u32`→`u32`, mixed integer widths→wider integer, integer↔`f64` or signed↔unsigned mix→`f64`. Unknown/homogeneous `f64` literals stay `f64`. Failure → **`type.mdim.incompatible`**.

### Example

```text
use native math.mdim with D

#main
  A:f64[][] = [[1, 2], [3, 4]]
  B:f64[][] = [[5, 6], [7, 8]]
  D.add(A, B) -> C
  D.sum(C) -> s
  io.outln(s)
  OK
```

Output: `36`

## Errors

### Shape / tip operanzi (`type.mdim.*`)

| Code | When |
|------|------|
| `type.mdim.not_rect` | Operand not rectangular |
| `type.mdim.not_numeric` | Non-numeric leaf |
| `type.mdim.dim_mismatch` | Incompatible shapes or bad args |
| `type.mdim.rank` | Wrong rank (e.g. `matmul` on 1D) |
| `type.mdim.tag_shape` | Role tag (`vector` / `matrix`) incompatible with operand shape |
| `type.mdim.incompatible` | Operand element types cannot be promoted together (or explicit `;` tag cannot coerce result) |

### Operație aritmetică (aliniat cu `math.calc.*`)

| Code | When |
|------|------|
| `math.mdim.divzero` | `divide(a, b)` când vreun element din `b` e 0 |

## Implementation map

| Piece | Location |
|-------|----------|
| IDs | `include/flat_native.h` (`FLAT_NATIVE_MATH_MDIM_*`) |
| Handlers | `src/lib/mod_math_mdim.c` |
| RHS parse (multi-arg) | `flat_native_parse_rhs` + `release` (`native_parse_rhs.c`) — pin LIST/OBJECT între argumente; scalar/ultim → `flat_native_parse_expr` |
| Plugin | `bin/libflat_math_mdim.so` (embedded on Windows / WASM) |

## See also

- [list.md](./list.md) — `is_rect`, `dims`, COW
- [math.md](./math.md) — scalar `math.sum` / `min` / `max`
- [TYPED_BIND.md](../TYPED_BIND.md) — `f64[][]` slots and literals