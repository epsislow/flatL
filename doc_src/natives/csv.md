# Native module `csv`

**Cursor-based CSV reader and whole-file writer** on a numeric **`file.*` fp id**. **Optional** — declare before use:

```text
use native csv
use native list
use native obj
```

Writer **`writeCols`** needs **`obj`**. Format is auto-detected on first read; override with **`setSeparator`**, **`setLineEndings`**, **`setEncoding`**, **`setQuote`**. Pass **`""`** to any setter to re-enable auto for that field.

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

- Example: `docs/fleditor_site/examples/csv_reader/` (MEMFS **`stock.csv`**, **`use native csv`**).
- Parity: `test/fleditor/csv_vm_parity.mjs` compares **`csv.next`** output to **FleditorCsvParse** on `test1_csv_*` (UTF-8 exports only; UTF-16 deferred).

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