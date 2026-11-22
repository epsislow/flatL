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

## 3. Indentation defines the block

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

## 4. Generated labels

Ordinary local blocks receive unique internal labels such as:

- `#local_1_for`
- `#local_2_call`
- `#local_3_foreach`
- `#local_4_jump`

The exact sequence follows the order in which local blocks are encountered during compilation. Generated labels are implementation details and should not be referenced manually.

A local guard handler uses the existing indexed handler label convention (`#or.<owner-block>.<instruction-index>`) so the current guard compiler and VM behavior remain unchanged. In Code Map it is displayed with a short synthetic name such as `#local_4_guard`.

## 5. Code Map

FLEditor Code Map displays local blocks as separate synthetic nodes. Their displayed names use the `#local_N_<kind>` form. Edges from `call`, `jump`, `for`, `foreach` and guard instructions point to the corresponding local node.

Selecting a local node navigates to the original indented source block, not to generated code. Nested local blocks are represented as separate nodes and retain their source locations.

## 6. Compilation and runtime

Local blocks are expanded by the compiler into ordinary FlatL block labels before normal block resolution and bytecode generation. The generated blocks use the existing block directory, call stack, loop stack, jump handling and `#or` handler lookup.

There is no new VM opcode or special runtime scope for local blocks. `next`, `return`, `OK` and `FAIL` are not redefined by this feature.
