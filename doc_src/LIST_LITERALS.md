# Liste tipizate și literali (AM-LIST-LIT)

**Status:** **L-core** + **L-ext** + **L.2** + **L.1a/b/b+** ✅ (rebuild mdim schema cu null-fill celulă).  
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

**Anti-confuzie jagged:** `loose:l[] = [[1], [2, 3]]:l[]` — OK (subliste de lungimi diferite). `zone:[][] = [[1], [2, 3]]:[][]` — `type.assign.list.jagged` (rank 2 impune rect). Literal fără LHS (`[[1], [2, 3]]:[][]` singur) → eroare compile.

## Liste `<schema>` 1D (L.1a)

Frunza = obiect **`<schema>`** (null-fill pe `{}`); aceleași axe ca la numeric:

```text
rows:<stock>[]              # lungime liberă
rows:<stock>[4]             # cap ≤ 4
rows:<stock>[=2] = [{}, {qty: 1}]   # exact 2 elemente
rows:<stock>[2] = [{qty: 9}, {}]    # fără sufix literal (D15)
rows = [{}, {}]:<stock>[]    # assert parse pe sufix
```

Câmp în declarație schema: `stocks: <stock>[4]` — același contract.

## Liste `<schema>` 2D+ (L.1b)

Rank 2–4, **rect** obligatoriu, fiecare celulă `<schema>` cu null-fill la assign:

```text
table:<stock>[2][=5]     # ≤2 rânduri, exact 5 coloane
grid:<stock>[][]         # 2D liber + rect
table:<stock>[2][=2] = [[{}, {}], [{}, {}]]
table[0][1].qty = 42      # path în grilă (contract frunză pe câmp)
```

Literal: `… :<stock>[][]` (depth + tip; axe opțional assert). Validare la assign: rect + shape pe axe.

**L.1b+** (assign grile schema): la `=` / literal cu sufix, rank 1 **și** 2–4 reconstruiesc un arbore LIST nou; fiecare celulă trece prin `flat_schema_bind_value` → obiecte **complete** (câmpuri lipsă = `null`, ca la `rows:<stock>[N]` rank 1). Exemplu:

```text
table:<item>[2][=2] = [[{qty: 1}, {}], [{sku: null}, {qty: 2}]]
table[0][1].qty == null
```

**Notă COW:** la literale, aceleași referințe `{}` în același rând înainte de un obiect cu câmpuri pot împărți slot în parse; rebuild clonează înainte de bind, dar ordinea în rând poate afecta valori — preferă câmpuri explicite (`{sku: null}`) sau assign după grilă.

## Shape / `rect` (faza R)

La assign pe slot tipizat / literal cu sufix, runtime păstrează **stamp** (`rect`, `dims`, rank) pe `list_id`. Introspecție: `use native list with L` → `L.is_rect`, `L.dims`, `L.rank`, `L.is_matrix`, `L.is_vector` (vezi [natives/list.md](natives/list.md)). După mutatori COW (`L.push`, …) lista nouă nu moștenește stamp — se folosește **walk** cu memo.

## Path și mutatori (L.2)

`M:f64[][]` — `M[i][j] = …` rezolvă contractul frunză (`f64`) la fiecare nivel LIST; rădăcină LIST sau OBJECT (schema). Tip greșit → `type.assign.incompatible`; range numeric → `type.assign.range`.

`L.push(M, row) -> M` pe slot `M:f64[][]` = assign pe lista rezultată (rect / jagged ca la `=` root).

`def f(g:f64[][]) -> out:f64[][]` — validare la apel și return ca assign root.

## Erori frecvente

| Cod | Când |
|-----|------|
| `type.assign.shape` | cap / exact pe axă |
| `type.assign.list.jagged` | rânduri de lățimi diferite (rect) |
| `type.assign.list.rank` | prea puține/multe nivele LIST |
| `type.assign.list.empty` | `[]` pe slot cu rank ≥2 multidim |
| `type.assign.literal.type` | sufix ≠ tip element LHS |
| `type.list.rank.max` | peste 4 axe |