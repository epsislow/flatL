# Liste tipizate și literali (AM-LIST-LIT)

**Status:** **L-core complet** (rank 1–4, `f64`/`tN`/`b`/numeric, literali, rect); apoi L.1a/b, L-ext, L.2.  
**Legături:** [TYPED_BIND.md](./TYPED_BIND.md), [STRICT_TYPES.md](./STRICT_TYPES.md), plan `vect-matrix-rect-roadmap`.

## Sintaxă axe (LHS și sufix literal)

După tipul elementului (`f64`, `t255`, `<stock>`, `l`, `o`, sau **shape-only** `[]` / `[][]`):

| Formă | Semnificație |
|--------|----------------|
| `[]` | Axă liberă (≥1 elemente la runtime; 1D poate fi `[]` gol) |
| `[n]` | Cap: lungimea axei **≤ n** |
| `[=n]` | Exact: lungimea axei **= n** |

Ordinea bracket-urilor = exterior → interior. Rank maxim **4** (`type.list.rank.max` la compile). `[0]` / `[=0]` → `type.list.dim.zero`.

**Exemple LHS**

```text
v:f64[]              # vector f64, axă liberă
w:f64[=3]            # exact 3 elemente
M:f64[][]            # matrice f64 dreptunghiulară
A:f64[2][=4]         # ≤2 rânduri, exact 4 coloane
rows:<stock>[4]      # listă 1D de schema (L.1a)
table:<stock>[2][=5] # grid schema 2D (L.1b)
grid:l[][]           # 2D rect, celule heterogene
bags:o[]             # vector de obiecte
zone:[][]            # shape-only 2D (≠ `l[]` rank 1)
```

**Doar contract (fără valoare):** nu există linie `M:f64[][]` fără `=` — folosește `M:f64[][] = null` (sau assign imediat). Aliniat cu restul bind-urilor tipizate din sursă.

## Literal RHS

```text
x:f64[][] = [[1, 2], [3, 4]]:f64[][]   # sufix opțional
y:f64[][] = [[1, 2], [3, 4]]          # fără sufix — OK (D15)
buf = [[1, 2], [3, 4]]:f64[][]       # fără LHS — doar parse + assert sufix
```

- Sufixul imediat după `]`-ul exterior: `:[…]:tip` sau `:[=2][3]` pe shape-only.
- `type.assign.literal.type` dacă elementul sufixului ≠ contractul slotului (ex. `u32[]` vs `:f64[]`).
- `type.assign.list.depth` dacă adâncimea LIST ≠ numărul de axe din sufix.
- **Rect** la rank ≥2: lățimi de rând egale pe fiecare axă interior → `type.assign.list.jagged` sau `type.assign.shape` pe axe exacte.
- Literale **nested** cu mai mulți frați la același nivel păstrează subliste vii până la `flat_list_build` (pin la parse).

## Tipuri container

| Formă | Rank | Rect |
|-------|------|------|
| `l` | — | nu impune |
| `l[]` / `l[n]` | 1 | nu (subliste pot fi jagged) |
| `l[][]` … | 2–4 | da, celule orice |
| `o[]`, `o[][]` | 1 / 2–4 | `o` OBJECT la frunză |
| `[][]` | 2–4 | da, frunză mixtă (≠ `l[]`) |

## Path (L.2)

`M:f64[][]` — `M[i][j] = …` rezolvă contractul frunză (`f64`) pentru indexare; assign pe celulă trece prin `flat_slot_type_check_value`.

## Erori frecvente

| Cod | Când |
|-----|------|
| `type.assign.shape` | cap / exact pe axă |
| `type.assign.list.jagged` | rânduri de lățimi diferite (rect) |
| `type.assign.list.rank` | prea puține/multe nivele LIST |
| `type.assign.list.empty` | `[]` pe slot cu rank ≥2 multidim |
| `type.assign.literal.type` | sufix ≠ tip element LHS |
| `type.list.rank.max` | peste 4 axe |