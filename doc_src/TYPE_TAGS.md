# Tag-uri la apel (`;`) — AM-TAGURI v1

Tag-urile sunt **context** pentru unele natives: tip de rezultat, I/O sau conversie. Nu sunt al doilea operand virgulă.

## Sintaxă

```text
mod.fn(operand1, operand2, … ; tagExpr)
```

| Formă | Semnificație |
|--------|----------------|
| `fn(a, b)` | Fără tag (ca înainte). |
| `fn(a, b ; )` | Listă goală de tag-uri (explicit). |
| `fn(a, b ; "u32")` | Tag literal (STRING). |
| `fn(a, b ; tags)` | Tag din variabilă STRING (runtime). |

**Nu** folosi `fn(a, b ; "")` — la compile → **`type.tag.arg`**. Pentru zero tag-uri: omit `;` sau `; )`. La runtime, variabilă `tags = ""` echivalează cu lipsa tag-ului.

Split pe `;` respectă ghilimele și parantezele (depth 0), ca virgulele în argumente.

## Natives pilot (v1)

| Native | Tag tipic | Rol |
|--------|-----------|-----|
| `m.calc(expr ; "u8")` | tip numeric | Verificare range în calc (`type.calc.range`) |
| `m.sum2` / `min2` / `max2` | tip numeric | Rezultat verificat în native |
| `io.inln` / `in_num` / `in_bool` | `tN` sau numeric | Contract după citire |
| `str.to_num(s ; "u32")` | tip numeric | Parse în tip |

Orice alt native cu `; "…"` → **`type.tag.not_accepted`**.

## Panic-uri

| Cod | Când |
|-----|------|
| `type.tag.not_accepted` | Token invalid, native fără tag-uri, conflict tokeni |
| `type.tag.arg` | `tagExpr` nu e STRING sau literal `""` la compile |

După validarea tag-ului rămân codurile callee: `type.calc.type`, `type.calc.range`, `type.io.len`, etc.

## Reguli

- Un singur token de tip numeric per apel (nu `"u32 f32"`).
- Ordinea tokenilor în STRING nu contează; intern se sortează alfabetic.
- Moștenirea tipului la assign (`-> name:u32`) este separată; tag-ul fixează comportamentul native-ului.

Vezi și [TYPED_BIND.md](./TYPED_BIND.md), [STRICT_TYPES.md](./STRICT_TYPES.md), natives în `doc_src/natives/`.
