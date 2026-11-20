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
| `setObjHeaders(id, keys)` | — | `csv.setObjHeaders`, `csv.objHeaders.empty`, `csv.objHeaders.duplicate`, `csv.objHeaders.length` |
| `nextRecord(id)` | OBJECT | `csv.nextRecord` |
| `getCollection(id, offset, limit)` | OBJECT (column-oriented) | `csv.getCollection`, `csv.objHeaders.length` |

**`getCells`** — rectangle from top-left **`(x0, y0)`** with size **`xLen × yLen`**. Default **`mode`** **`"list"`** → LIST of LIST (row-major). **`"obj"`** → OBJECT with keys **`"0"`…`"xLen-1"`** and LIST values (column-major); requires **`use native obj`** at compile time. Numeric object keys: **`obj.get(grid, '"0"')`** (path DSL, see **`obj.get`**).

Returned cells are **untyped STRING** values (not `t255` in storage). **Cap and rect** come from the **typed slot on assign** (AM-LIST-LIT + assign check), not from `getCells`:

```text
raw = csv.getCells(rid, 0, 0, cols, rows)
G:t255[][] = raw          # any tN: t8, t16, t1024, …
```

Mismatch shape or `type.assign.string.len` → panic at **`=`**; use **`L.is_rect` / `L.dims`** after assign (see **`list.md`**). Example: `docs/fleditor_site/examples/csv_reader/grid_typed.fl`; tests **`types/218`**, **`265`**, **`266`**.


### Object records and collections

`setObjHeaders(id, keys)` configures the property names used by `nextRecord()` and
`getCollection()`. `keys` is a list of strings; it can be taken from a CSV header row
using the existing `next()` operation, or supplied manually:

```text
headers = csv.next(rid)
csv.setObjHeaders(rid, headers)
# or: csv.setObjHeaders(rid, ["product", "price", "quantity"])
```

The operation does not consume a row or change the reader cursor. The number of keys
must match the number of columns accessible through the reader or view. Empty keys,
duplicate keys, and a mismatched key count panic. Each key must fit the FlatL object-key
length limit.

When no custom keys are configured, generated names `col0`, `col1`, `col2`, etc. are
used. Object headers are configured independently for each reader and each view; views
do not inherit their parent's custom headers. All CSV values remain strings. Rows with fewer fields than the accessible column count are padded with empty strings.

- `nextRecord(id)` reads one row at the current reader/view cursor and returns an object
  whose properties map to that row's values.
- `getCollection(id, offset, limit)` returns a column-oriented object: each property
  contains a list of values for that column. Both arguments are mandatory; `offset` is
  zero-based and `limit` is the maximum number of rows to read. The requested range is
  relative to the reader or view, and this operation does not change the `nextRecord()`
  cursor.

`getCollection()` materializes its requested range in memory. The requested rectangle is subject to the existing 1024-cell collection cap. Keep `limit` bounded,
especially for large files. An offset equal to the row count returns an object with empty lists. The existing `next()`, `row()`, and `getCells()` operations
retain their existing return formats.

Example:

```text
headers = csv.next(rid)
csv.setObjHeaders(rid, headers)
record = csv.nextRecord(rid)
collection = csv.getCollection(rid, 0, 100)
```

## Writer

Starts at byte **0** and truncates the file. Not for `file.appendCtx` or read-only handles.

| Call | Panic |
|------|-------|
| `writer(fpId)` | `csv.writer.mode`, `csv.fp.busy` |
| `writeRows(wid, listOfLists)` | `csv.writeRows` |
| `writeCols(wid, obj)` | `csv.writeCols`, `csv.writeCols.length` |
| `setObjHeaders(wid, keys)` | `csv.setObjHeaders`, `csv.objHeaders.empty`, `csv.objHeaders.duplicate`, `csv.objHeaders.length` |
| `writeRecords(wid, records)` | `csv.writeRecords`, `csv.writer.no_records` |
| `writeCollection(wid, collection)` | `csv.writeCollection`, `csv.writeCollection.length`, `csv.writer.no_records` |
| `flush(wid)` | `csv.flush` |
| `close(wid)` | flush + release (same as reader close) |

Format setters work on **`rid`** or **`wid`**.


### Object-based writing

`setObjHeaders(wid, keys)` also configures the output columns of a writer. The supplied
list defines both the CSV column order and the properties selected from each input
object or collection. On a root writer, the configured list defines the output width;
on a writer view, its length must match the view width. Writer views keep their own
configuration and do not inherit headers from their parent.

When no custom headers are configured, object writing uses generated keys `col0`,
`col1`, `col2`, etc. The input must contain the corresponding default keys.

- `writeRecords(wid, records)` writes a list of objects as consecutive CSV rows. The
  number of rows equals the number of objects in the list.
- `writeCollection(wid, collection)` writes a column-oriented object as consecutive
  CSV rows. The number of rows is the number of values in each selected column list.
  It writes data only; it does not emit a header row.

Both operations start at the writer's current position. For a writer view, they start
at that view's current row cursor, advance it by the number of rows written, and must
fit entirely inside the view's height. Root writers append the serialized rows to the
writer buffer.

Only properties named in `ObjHeaders` are written, in exactly that order. Additional
properties in records or collections are ignored. Every configured property must be
present in each record or in the collection. Missing properties panic. For
`writeCollection()`, all selected column lists must have equal lengths. An empty records
list or empty selected collection lists panic with `csv.writer.no_records` (`No records
to write.`). Values are serialized as strings using the writer's current CSV format.

The operations validate and prepare their input before committing rows, so invalid
records, mismatched collection lengths, and writer-view conflicts do not produce a
partially written batch.

Example — writing objects:

```text
wid = csv.writer(fp)
csv.setObjHeaders(wid, ["product", "price"])
records = [
  { product: "Apple", price: "10", extra: "ignored" },
  { product: "Orange", price: "20", extra: "ignored" }
]
csv.writeRecords(wid, records)
csv.close(wid)
```

Example — writing a column-oriented collection (for example, directly from
`getCollection()`):

```text
rid = csv.reader(inputFp)
csv.setObjHeaders(rid, ["product", "price"])
collection = csv.getCollection(rid, 0, 100)
csv.close(rid)

wid = csv.writer(outputFp)
csv.setObjHeaders(wid, ["product", "price"])
csv.writeCollection(wid, collection)
csv.close(wid)
```

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
| `setObjHeaders(viewId, keys)` | Configures object property names independently for this view |
| `nextRecord(viewId)` | Reads the next row as an object |
| `getCollection(viewId, offset, limit)` | Reads a bounded range as a column-oriented object |
| `reset(viewId)` | Resets that view's cursor |
| `set(viewId, x, y, value)` | Sets one cell in a writer view (grid mode) |
| `writeRow(viewId, y, values)` | Writes a complete row into a writer view; pads remaining columns with empty fields |
| `writeRecords(viewId, records)` | Writes objects from the view cursor, advancing it by the number of records |
| `writeCollection(viewId, collection)` | Writes selected column lists from the view cursor, without a header row |

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

Writer grid mode (used by writer views, `set`, `writeRow`, `writeRecords`, and `writeCollection` on a view) is mutually exclusive with sequential writer mode (`writeRows`, `writeCols`, `writeRecords`, and `writeCollection` on the root writer) on the same writer.
A grid cell can be assigned only once; a duplicate assignment panics with
`csv.writer.conflict`. Unassigned cells between populated columns are serialized as empty
fields. Grid output is buffered until `flush` or `close`. `writeRow(viewId, y, values)`
populates a complete view row atomically, starting at local column zero; values wider than
the view are rejected, and shorter values are padded with empty fields. It also rejects
overlapping cells with `csv.writer.conflict` and cannot be mixed with `writeRows` or
`writeCols` on the same writer.

A view must fit within its parent. Invalid coordinates panic with `csv.view.bounds`.
Closing a root reader or writer while any descendant view remains open panics with
`csv.view.open`; closing a parent view while a child remains open panics with
`csv.view.children`.


Example — writing complete rows through nested writer views:

```text
wid = csv.writer(fp)
region = csv.view(wid, 0, 0, 8, 20)
body = csv.view(region, 1, 2, 4, 10)

csv.writeRow(body, 0, ["Keyboard", 50])
csv.writeRow(body, 1, ["Mouse", 25, "EUR", "In stock"])

csv.closeView(body)
csv.closeView(region)
csv.close(wid)
```

`writeRow(viewId, y, values)` writes at the view's local row `y`, starting at its
local column zero. The values must fit within the view width. Remaining columns are
written as empty fields. The operation is atomic: if the row is out of bounds, too
wide, or overlaps cells already assigned through `set` or another `writeRow`, no
part of the row is added. It uses grid mode and cannot be mixed with sequential writer operations on the same writer.
