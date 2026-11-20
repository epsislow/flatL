# Native module `csv`

**Cursor-based CSV reader and whole-file writer** on a numeric **`file.*` fp id**. **Optional** — declare before use:

```text
use native csv
use native list
use native obj
```

Writer **`writeCols`** needs **`obj`**. Format is auto-detected on first read; override with **`setSeparator`**, **`setLineEndings`**, **`setEncoding`**, **`setQuote`**. Pass **`""`** to any setter to re-enable auto for that field.

**Encoding:** UTF-8, UTF-8-BOM, **UTF-16 LE/BE**. Reader auto-detects BOM; UTF-16 **without** BOM uses a lightweight ASCII-oriented heuristic (use **`setEncoding(rid, 'UTF-16-LE')`** when unsure). Writer emits bytes per **`setEncoding`** (`UTF-8` default when encoding is still auto); UTF-16 and UTF-8-BOM outputs include a BOM at flush.

## Reader

| Call | Returns | Panic |
|------|---------|-------|
| `reader(fpId)` | NUMBER `rid` | `csv.reader`, `csv.reader.mode`, `csv.fp.busy`, `csv.fp.closed` |
| `close(rid)` | — | `csv.close` |
| `reset(rid)` | — | `csv.reset` |
| `isEof(rid)` | BOOL | `csv.isEof` |
| `rows(rid)` | NUMBER (physical lines) | `csv.rows` |
| `cols(rid)` | NUMBER (fields on current row) | `csv.cols` |
| `getRow(rid)` / `getCol(rid)` | NUMBER | `csv.getRow` / `csv.getCol` |
| `setRow(rid, row)` | — | `csv.setRow` |
| `next(rid)` | LIST STRING | `csv.next` (>1024 fields) |
| `nextCells(rid, len)` | LIST STRING | `csv.nextCells` |
| `getCells(rid, x0, y0, xLen, yLen, mode?)` | LIST (rows) or OBJECT | `csv.getCells`, `bounds` (region / field range); product **`xLen*yLen > 1024`** |

**`getCells`** — rectangle from top-left **`(x0, y0)`** with size **`xLen × yLen`**. Default **`mode`** **`"list"`** → LIST of LIST (row-major). **`"obj"`** → OBJECT with keys **`"0"`…`"xLen-1"`** and LIST values (column-major); requires **`use native obj`** at compile time. Numeric object keys: **`obj.get(grid, '"0"')`** (path DSL, see **`obj.get`**).

Returned cells are **untyped STRING** values (not `t255` in storage). **Cap and rect** come from the **typed slot on assign** (AM-LIST-LIT + assign check), not from `getCells`:

```text
raw = csv.getCells(rid, 0, 0, cols, rows)
G:t255[][] = raw          # any tN: t8, t16, t1024, …
```

Mismatch shape or `type.assign.string.len` → panic at **`=`**; use **`L.is_rect` / `L.dims`** after assign (see **`list.md`**). Example: `docs/fleditor_site/examples/csv_reader/grid_typed.fl`; tests **`types/218`**, **`265`**, **`266`**.

## Writer

Starts at byte **0** and truncates the file. Not for `file.appendCtx` or read-only handles.

| Call | Panic |
|------|-------|
| `writer(fpId)` | `csv.writer.mode`, `csv.fp.busy` |
| `writeRows(wid, listOfLists)` | `csv.writeRows` |
| `writeCols(wid, obj)` | `csv.writeCols`, `csv.writeCols.length` |
| `flush(wid)` | `csv.flush` |
| `close(wid)` | flush + release (same as reader close) |

Format setters work on **`rid`** or **`wid`**.

## `file` interaction

- One active **`rid`** **or** **`wid`** per `fpId` — opening the other → **`csv.fp.busy`**.
- **`file.close(fpId)`** while CSV session open → **`file.close.busy`** (call **`csv.close`** first).

## FLEditor / tests

- Examples: `docs/fleditor_site/examples/csv_reader/` (Fleditor: **`file.readCtx('examples/csv_reader/stock.csv')`**) — **`grid_typed.fl`**, **`grid_row_build.fl`**.
- Parity: `test/fleditor/csv_vm_parity.mjs` compares **`csv.next`** output to **FleditorCsvParse** on `test1_csv_*` (including UTF-16 LE/BE).

## Example

```text
use native csv
use native list

#main
  fp = file.createCtx('t.csv')
  file.write(fp, "name,value\nx,1\n")
  file.close(fp)
  fp = file.readCtx('t.csv')
  rid = csv.reader(fp)
  csv.rows(rid) -> n
  io.outln(n)
  row = csv.next(rid)
  foreach(row, cell, "#out")
  csv.close(rid)
  file.close(fp)
  OK

#out
  io.out(cell)
  io.out(',')
  next
```
## Views

CSV readers and writers can expose bounded rectangular regions through numeric view IDs.
Views can be nested; child coordinates are relative to their parent. Close views explicitly,
from child to parent, before closing the root reader or writer.

| Call | Description |
|------|-------------|
| `view(parentId, x, y, width, height)` | Creates a reader or writer view; returns a numeric `viewId` |
| `closeView(viewId)` | Closes a view; panics with `csv.view.children` while child views remain |
| `rows(viewId)` / `cols(viewId)` | Returns the fixed view height / width |
| `get(viewId, x, y)` | Reads one cell from a reader view |
| `row(viewId, y)` | Reads one row from a reader view as a LIST STRING |
| `next(viewId)` | Reads the next row from a reader view; each view has its own cursor |
| `reset(viewId)` | Resets that view's cursor |
| `set(viewId, x, y, value)` | Sets one cell in a writer view (grid mode) |

Example — nested reader views:

```text
rid = csv.reader(fp)
region = csv.view(rid, 0, 10, 8, 100)
prices = csv.view(region, 4, 0, 2, 100)
price = csv.get(prices, 0, 0)
row = csv.next(prices)
csv.closeView(prices)
csv.closeView(region)
csv.close(rid)
```

Example — grid writer view:

```text
wid = csv.writer(fp)
grid = csv.view(wid, 0, 0, 3, 20)
csv.set(grid, 0, 0, "Name")
csv.set(grid, 1, 0, "Price")
csv.set(grid, 0, 1, "Keyboard")
csv.set(grid, 1, 1, 50)
csv.closeView(grid)
csv.close(wid)  # serializes the grid and flushes the writer
```

Writer grid mode is mutually exclusive with `writeRows` / `writeCols` on the same writer.
A grid cell can be assigned only once; a duplicate assignment panics with
`csv.writer.conflict`. Unassigned cells between populated columns are serialized as empty
fields. Grid output is buffered until `flush` or `close`.

A view must fit within its parent. Invalid coordinates panic with `csv.view.bounds`.
Closing a root reader or writer while any descendant view remains open panics with
`csv.view.open`; closing a parent view while a child remains open panics with
`csv.view.children`.
