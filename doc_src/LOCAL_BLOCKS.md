# Local blocks

Local blocks are syntactic sugar for ordinary FlatL blocks. They let an instruction refer to the indented block immediately below it without requiring the author to declare a separate top-level block label.

The compiler creates an ordinary internal block label and rewrites the instruction to reference it. The VM does not have a separate local-block instruction or execution model.

## 1. Local block reference (`#:`)

Use `#:` wherever an instruction accepts a block label.

### `call`

```flatL
#main
  call #:
    io.outln('inside local block')
    return
  io.outln('after call')
  OK
```

The `call` invokes the indented block. The existing call stack and `return` behavior are used. The instruction after the local block remains in the parent block.

### `for`

```flatL
#main
  for(3, i, '#:')
    io.outln(i)
    next
  io.outln('loop finished')
  OK
```

The generated block is used as the loop body. `next` keeps its normal loop-continuation behavior.

### `foreach`

```flatL
#main
  items = ['a', 'b']
  foreach(items, item, '#:')
    io.outln(item)
    next
  io.outln('all items processed')
  OK
```

### `jump`

```flatL
#main
  jump #:
    io.outln('jumped into local block')
    OK
```

The local block is a normal jump target. Its terminal instructions retain their normal meaning.

## 2. Indented guard handlers (`#or`)

A guard followed by an indented block uses that block as the guard's local `#or` handler.

```flatL
#main
  amount >= 1
    io.outln('amount must be positive')
    FAIL
  io.outln('amount accepted')
  OK
```

If the guard succeeds, execution continues in the parent block and skips the handler. If the guard fails, execution transfers to the generated handler block using the existing `#or` behavior.

When a local guard handler reaches the end of its indented source block without executing an explicit terminal instruction, the compiler adds an implicit `OK` at that block boundary. This prevents execution from falling through into the next generated handler block. An explicit `next`, `return`, `OK`, `FAIL` or jump keeps its normal behavior; these instructions still do not define the indentation boundary.

A tilde (`~`) instruction can also use an indented local handler for its logical-failure route:

```flatL
#main
  ~ io.try_read() -> value
    io.outln('read was not successful')
    FAIL
  io.outln(value)
  OK
```

The handler is an ordinary `#or` block internally; no new VM failure semantics are introduced.

## 3. Local infrastructure-panic handlers (`#panic.<location>`)

An ordinary instruction can own an indented local panic handler. The handler is addressed using the instruction's panic site: its instruction index by default, or its existing `#.lineLabel` when supplied.

```flatL
#main
  raw = 'not-a-number'
  str.to_num(raw) -> value  #.badNum
    io.outln('Conversion failed at ' + panicLoc())
    FAIL
  io.outln('This runs only if conversion succeeds')
  OK
```

This is equivalent to a dedicated `#panic.#main.badNum` block. If the instruction has no line label, its generated handler is `#panic.#main.<instruction-index>`.

A local panic handler is terminal: when PANIC routes to it, execution does not return to the faulting instruction or continue in its parent block. It must end explicitly with `OK`, `FAIL`, or `jump`. `return` is forbidden in every panic handler. Unlike local `#or` handlers, local panic handlers do not receive an implicit `OK` at the indentation boundary.

Nested panic handlers can use the existing line-label alias form to keep names short:

```flatL
#main
  call #:
    data = json.parse(raw)  #.parse
      io.outln('Invalid JSON')
      FAIL
    value = str.to_num(data.value)  #.convert
      io.outln('Invalid number')
      FAIL
    return
  OK
```

The nested handlers use `#panic.parse` and `#panic.convert`, scoped to the containing panic handler. This reuses the existing `#.` line-label syntax; no new syntax or VM behavior is introduced.

## 4. Indentation defines the block

The first non-blank, non-comment line indented farther than the owner instruction starts the local block. The local block ends when indentation returns to the owner's level or a shallower level.

```flatL
#main
  call #:
    io.outln('inside')
    return

  io.outln('outside')
  OK
```

Only indentation determines the local block boundary. `next`, `return`, `OK` and `FAIL` do not close a local block. They retain their existing runtime behavior and are simply instructions inside the indented block.

Blank lines and comments do not end a local block. Local blocks can be nested:

```flatL
#main
  foreach(items, item, '#:')
    call #:
      io.outln(item)
      return
    next
  OK
```

Each `#:` belongs to its own owner instruction and receives a separate generated label.

## 5. Generated labels

Ordinary local blocks receive unique internal labels such as:

- `#local_1_for`
- `#local_2_call`
- `#local_3_foreach`
- `#local_4_jump`

The exact sequence follows the order in which local blocks are encountered during compilation. Generated labels are implementation details and should not be referenced manually.

A local guard handler without a line label uses the existing indexed handler label convention (`#or.<owner-block>.<instruction-index>`). To keep nested handler names short, a guard inside an `#or` handler can use the existing line-label syntax:

```flatL
#main
  found == 2
    found == 3  #.local_2
      io.out('yes')
    io.out('test')
    found == 4  #.local_3
      io.out('not 4')
  OK
```

The nested local handlers are named `#or.local_2` and `#or.local_3`, using the same `#.` line-label mechanism as other instructions. No additional syntax is introduced. A guard in an ordinary block with a line label uses `#or.<owner-block>.<line-label>`.

## 6. Code Map

FLEditor Code Map displays local blocks as separate synthetic nodes. Their displayed names use the `#local_N_<kind>` form. Edges from `call`, `jump`, `for`, `foreach`, guard instructions and statically identifiable panic sites point to the corresponding local node.

Selecting a local node navigates to the original indented source block, not to generated code. Nested local blocks are represented as separate nodes and retain their source locations.

## 7. Compilation and runtime

Local blocks are expanded by the compiler into ordinary FlatL block labels before normal block resolution and bytecode generation. The generated blocks use the existing block directory, call stack, loop stack, jump handling and `#or` handler lookup.

There is no new VM opcode or special runtime scope for local blocks. `next`, `return`, `OK` and `FAIL` are not redefined by this feature.
