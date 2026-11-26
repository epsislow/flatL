# flatL (Flat Language)

**flatL** is a label-driven language built on a **flat graph** instead of nested scopes. Programs are collections of linear **basic blocks** linked by explicit control flow: guards (`#or` handlers), `call`/`return`, and isolated panic handlers. The toolchain compiles human-readable source (`.fl`) into packed bytecode (`.fb`) executed by a small virtual machine (**flvm**).

> **Toolchain v0.5.2** (`FLAT_TOOLCHAIN_VERSION` in `include/flat.h`) — **AM-NUM**: `.fb` **v9**  
> FGIR spec v1.0 · Binary & VM spec v1.2 (`.fb` v6 with FFUN and optional SCHM metadata)  
> **Documentation is in English** — index: [doc_src/README.md](doc_src/README.md) · offline site: **`make docs`** + **`make fleditor-serve`** → [docs/doc_site/](docs/doc_site/index.html)

## Task Runtime / Worker Pool status

The cooperative Task Runtime, wait/retry and Channel transaction model are implemented and audited through archive 290. Archives 291–295 specify the worker-pool architecture and audit native/dependency concurrency, including a focused pthread test for independent bundled Lexbor contexts (2,000 document lifecycles). Archive 296 defines the future worker-mode cancellation-request and DRAIN/CANCEL shutdown contracts. Archive 297 adds native module capability metadata and worker-policy preflight while preserving the existing plugin initializer ABI. Archive 298 adds cross-platform synchronization primitives, per-module invocation gates, serialized native-dispatch enforcement, and DOM teardown protection. Archive 299 adds the MPMC ready queue and dispatch ownership foundation. Archive 300 integrates a `FlatMutex` into `FlatRuntime`, synchronizes task registry/wait-graph operations, and applies dispatch ownership around cooperative VM quanta while keeping `flat_vm_step()` outside the lock. Archive 301 fixes the native HTTP initialization test fallback when `pkg-config` is unavailable/non-executable and restores the missing runtime/native sources in `Makefile.wasm`; the actual Emscripten link still requires validation with an installed SDK. Archive 302 integrates the MPMC ready queue into the cooperative scheduler, preserves transaction-owner priority, and converts per-Channel spin gates to `FlatMutex`; VM stepping remains outside the scheduler lock. Archive 303 adds request-only cancellation: the caller publishes intent, and scheduler/dispatch ownership performs VM cleanup at a safe boundary. Archive 304 adds cooperative DRAIN/CANCEL lifecycle, submission closure, blocked-DRAIN reporting, and escalation. Archive 305 adds the first runtime-owned pthread worker pool, condition-variable idle wait, startup rollback, DRAIN/CANCEL worker exit and explicit join. Archive 306 adds a focused four-worker regression for nested `task.wait()` and contended Channel transactions, including the no-suspend-while-owning-transaction rule; the full lifecycle passes 20 repetitions. Worker mode preflights each VM native manifest and rejects unsupported modules before pool/task admission, with a dispatcher guard as defense in depth; this is not yet complete-VM thread-safety certification. DOM remains `PENDING_THREAD_SAFETY_VALIDATION`, with `SERIALIZED` retained as fallback. Read [`plans/README.md`](plans/README.md) for the document order, [`plans/04_IMPLEMENTATION_STATUS.md`](plans/04_IMPLEMENTATION_STATUS.md) for the latest checkpoint, [`plans/12_RUNTIME_SCHEDULER_MUTEX.md`](plans/12_RUNTIME_SCHEDULER_MUTEX.md), [`plans/06_WORKER_POOL_SPEC.md`](plans/06_WORKER_POOL_SPEC.md), [`plans/09_CANCELLATION_SHUTDOWN_SPEC.md`](plans/09_CANCELLATION_SHUTDOWN_SPEC.md), and [`doc_src/NATIVE_THREAD_CAPABILITIES.md`](doc_src/NATIVE_THREAD_CAPABILITIES.md). Build-target parity fixes are documented in [`plans/13_BUILD_TARGET_PARITY.md`](plans/13_BUILD_TARGET_PARITY.md). Ready-queue integration and Channel mutex conversion are documented in [`plans/14_READY_QUEUE_CHANNEL_MUTEX_INTEGRATION.md`](plans/14_READY_QUEUE_CHANNEL_MUTEX_INTEGRATION.md). The invocation-gate design is documented in [`plans/10_NATIVE_SYNC_GATES.md`](plans/10_NATIVE_SYNC_GATES.md).

## Why flatL?

Traditional block nesting (`if`, `try/catch`, deep indentation) hides where success, failure, and recovery actually go. flatL makes both ends of every branch visible:

- **Guards** — a failed condition jumps to an **`#or`** handler block (`#or.#block.index` or `#or.#block.lineLabel`), not implicit fall-through. Source lines may end with `#.lineLabel` after the instruction.
- **Panics** — infrastructure faults route to `#panic.#block.index` or `#panic.any`, separate from logical `FAIL`.
- **No programmer `goto`** — targets are derived at compile time from block name and instruction index.

## Examples (`.fl`)

### Runnable today (toolchain v0.4)

Parses, compiles, and runs with current `flc` / `flvm` (see `make test`):

```text
#main
  amount = 100
  has_funds = true
  has_funds == true
  call #check_balance
  payment_status = 'SUCCESS'
  payment_status == 'SUCCESS'
  OK

#or.#main.2
  FAIL

#check_balance
  return
```

Types: `true` / `false`, numbers, quoted strings; `call #block`; guards with `==` (and `>=`, etc. on numbers).  
**I/O:** `io.out` / `io.outln` / `io.inln` / `io.in_num` / `io.in_bool`, binding `->` — see [doc_src/IO.md](doc_src/IO.md). **Strings:** **`str.*`** — [doc_src/natives/str.md](doc_src/natives/str.md); **`math.*`** / **`m.calc('…')`** — [doc_src/natives/math.md](doc_src/natives/math.md); **JSON / LIST / OBJECT** — [doc_src/IO_COLLECTIONS.md](doc_src/IO_COLLECTIONS.md). Test helper **`__ref__`**; literals — [LANGUAGE_TYPES.md](doc_src/LANGUAGE_TYPES.md).

**Also in v0.4:** `use "…"`, `use native …`, **`use fmod`** ([FMOD_MODULES.md](doc_src/FMOD_MODULES.md)); user **`def`** + executors ([USER_FUNCTIONS.md](doc_src/USER_FUNCTIONS.md)); dynamic **`call block expr`** ([CALL_BLOCK.md](doc_src/CALL_BLOCK.md)); toolchain **`--time`** ([TOOLCHAIN.md](doc_src/TOOLCHAIN.md)).

Minimal sample: [examples/hello.fl](examples/hello.fl). HTTPS (`http.*`): [examples/http_get.fl](examples/http_get.fl), [examples/http_post.fl](examples/http_post.fl). Payment-style flow with **`def`** (no `form.*` / `kv.*`): [examples/order_flow.fl](examples/order_flow.fl).

**User function with parameters** (runs today — see `test/cases/func/`):

```text
use "blocks/executor.fl" as #fl_
func:executor #fl_executor.global.scope
use native math with m

def add(a, b) -> sum
  m.calc('a + b') -> sum
  return

#main
  add(2, 3) -> r
  io.outln(r)
  OK
```

### Capability snapshot (toolchain v0.4)

| Area | v0.4 |
|------|------|
| Flat graph: `#main`, `#or…`, `OK` / `FAIL`, guards, panic handlers | Yes |
| `call #block` / `return` (no arguments) | Yes |
| `use` / `use native` / `use fmod`, link compile | Yes |
| User **`def …`**, calls `f(a, b) -> x`, executors | Yes |
| Natives: `io`, `str`, `math`, `json`, `file`, `vcx`, `list`, `obj`, … | Yes (`make test`) |
| `foreach`, `for`, `jump #block`, `call block <expr>` | Yes |
| **`call #block(a, b) -> out`** (plain blocks, not `def`) | **Not yet** — parser sees `(` on the call line but the VM does not pass arguments; use **`def`** for parameterized calls |
| **`#block(param, …)`** headers | Parsed into FGIR; **no runtime parameter binding** for plain blocks |
| **`return expr`** from a block | **`return` only** (no value expression) |
| Illustrative app APIs in [SPECIFICATION.md](doc_src/SPECIFICATION.md) (`form.*`, `kv.*`, `end.*`) | **Not in the toolchain** — long-term sketch of a web/order flow, not shipped modules |

The old payment-style snippet in the spec shows how guards and `#or` would wrap a business process; in v0.4 you would model **`check_balance`** as a **`def`** (or separate blocks plus shared variables / VCX), not as `call #check_balance(uid, amount) -> has_funds`.

Lexical rules (summary):

| Construct | Meaning |
|-----------|---------|
| `#name` | Block declaration |
| Sequential lines | Implicit AND — run top to bottom |
| `call #block(args) -> var` | Sub-graph invocation with output binding |
| `condition == value` | Guard; failure → `#or.#current.index` |
| `OK` / `FAIL` | Terminal success / logical failure |
| `foreach(coll, item, '#loop')` | Iteration via sub-block, no nested loop syntax |

## Toolchain pipeline

```text
 program.fl  ──►  flc  ──►  program.fb  ──►  flvm  ──►  execution
 (source)              (bytecode)              (VM loop)
```

| Extension | Role |
|-----------|------|
| `.fl` | flatL source — indent-delimited graph DSL |
| `.fb` | **flat Bytecodes** — magic header, block directory, opcode stream |

```bash
./flc program.fl -o program.fb
./flvm program.fb
# optional driver (same engine, in-process):
./flatl crun program.fl
```

See [doc_src/TOOLCHAIN.md — flatl](doc_src/TOOLCHAIN.md#flatl-driver-optional-ux).

## `.fb` binary layout

**Current emit:** **`flc`** writes **format v6** (`FLAT_FB_FORMAT_VERSION` in `include/fb_format.h`). **`flvm`** still loads **v3–v6** (older images on disk keep working).

| Version | Header | Tail sections (after bytecode) |
|---------|--------|--------------------------------|
| **v3** | 20 B | optional **FNAT** (native manifest) |
| **v4** | 24 B | **BLBL** block labels, **FNUN** native units |
| **v5** | 28 B | **FFUN** user `def` metadata (`func:callVM`) |
| **v6** | 32 B | **SCHM** schema catalog (`<schema>:` in source → `schema_id` at runtime) |

**History:** v5 was the first layout with **FFUN**; v6 only adds **`schm_off`** (byte offset 28) and the **SCHM** blob — same opcodes and FFUN layout as v5. Programs without typed schemas still use v6 with an empty SCHM.

Normative detail: [doc_src/TOOLCHAIN.md — fbdump](doc_src/TOOLCHAIN.md#fbdump) · [SPECIFICATION.md §3](doc_src/SPECIFICATION.md). Quick picture:

1. **Magic** — `FLAT` + version byte + block count + section offsets  
2. **Bytecode** + **BLBL** / **FNUN** / **FFUN** / **SCHM** (as applicable)  
3. Per-instruction stream — opcode + payload (e.g. ASSIGN text, guard `#or` target)

## Opcodes

| Opcode | Hex | Role |
|--------|-----|------|
| `OP_ASSIGN` | `0x01` | State / assignment |
| `OP_GUARD` | `0x02` | Conditional; jump on false |
| `OP_CALL` | `0x03` | Call block; push return address |
| `OP_CALL_BLOCK` | `0x0E` | Call block by runtime id (`call block expr` in source) |
| `OP_CALL_FUNCTION` | `0x10` | User `def` call via VM (`func:callVM 1`; see USER_FUNCTIONS.md) |
| `OP_JUMP` | `0x0D` | Jump to block; no call frame; clears loop frames |
| `OP_RETURN` | `0x04` | Pop call stack |
| `OP_FOREACH` | `0x05` | Collection iteration |
| `OP_FOR` | `0x0C` | Numeric `for(ntimes, i, '#block')` |
| `OP_OK` | `0x06` | Success exit |
| `OP_FAIL` | `0x07` | Logical failure exit |
| `OP_PANIC` | `0x08` | Route to panic handler |

Reference implementation direction: **C** — static IR structs, linear memory layout, switch-dispatch VM (no string parsing at runtime).

## Build & test (WSL)

```bash
make help         # list make targets
make              # compile bin/ (no tests run)
make test         # compile + run test/cases/ (flc + flvm)
make test-flatl   # same cases via [flatl] in case.toml
bin/run_tests -h
```

**Testing policy:** Every automated check in this repo is **must-pass**. There are no optional, skipped, or “nice-to-have” tiers in the runner (`bin/run_tests` has no xfail/skip flags). **`make test`** and **`make test-flatl`** must finish with **0 failed** — each `test/cases/*/…/case.toml` that is discovered counts. Additional checks (e.g. **`make fleditor-smoke`**, **`make fleditor-wasm-smoke`**, and related fleditor targets) are separate Makefile targets but follow the same rule: when you run them, they must pass; do not merge or ship with known failures. New suites and cases should be written as required gates, not as best-effort extras.

**Cross-platform:** `flc` and `flvm` are portable C; **`make test`** uses `run_tests`, which runs pipeline commands through a **POSIX shell** (e.g. `printf … | flvm`). On Windows, use **WSL** for the full suite, or compile/run bytecode manually. I/O test files under `test/cases/io/` are committed — regenerate only when changing the catalog: `make io_generate_tests` (`bin/io_generate_tests`, no Bash). Details: [doc_src/TOOLCHAIN.md — Portability](doc_src/TOOLCHAIN.md#portability-windows-shell-and-io-tests) · [test/README.md](test/README.md).

**Docs:** [doc_src/README.md](doc_src/README.md) · [TOOLCHAIN.md](doc_src/TOOLCHAIN.md) · [SPECIFICATION.md](doc_src/SPECIFICATION.md) · [LANGUAGE_TYPES.md](doc_src/LANGUAGE_TYPES.md) · [IO.md](doc_src/IO.md)

**Browser playground:** [docs/fleditor_site/](docs/fleditor_site/) — WASM compile/run in the browser; see [doc_src/FLEDITOR.md](doc_src/FLEDITOR.md).

## Roadmap

- [x] Toolchain skeleton + C test runner (`run_tests`)
- [x] `flvm` / `flc` — FGIR, `.fb`, opcodes through `OP_CALL_BLOCK`, `OP_JUMP`, `OP_FOR`
- [x] E2E suite (`make test`, 390+ cases)
- [x] Runtime values — STRING REF pool, NUMBER, BOOL, LIST, OBJECT ([LANGUAGE_TYPES.md](doc_src/LANGUAGE_TYPES.md))
- [x] Native modules (`io`, `str`, `math`, `json`, `file`, `vcx`, `list`, `obj`, …) + optional `.so` plugins on Unix
- [x] **v0.4** — user functions + executors, `.fmod`, `flatl`, diagnostics `--time`
- [ ] Block calls with arguments / `return expr` on plain `#block` graphs
- [ ] Multi-return user functions (`a, b = f(x)`)
- [ ] Runtime `module.load` / richer `.fmod` packaging (see [FMOD_MODULES.md](doc_src/FMOD_MODULES.md) §7)

## License

MIT — see [LICENSE](LICENSE).

## Author

[epsislow](https://github.com/epsislow)


Task Runtime / Channel architecture plans and the latest implementation checkpoint are maintained in [`plans/`](plans/). Current checkpoint: archive 308 (`flatL-main-308-termux-test-portability.zip`). Archive 297 adds module-level native thread-capability metadata, an optional plugin declaration symbol while preserving the existing initializer ABI, conservative rejection of legacy/unknown plugins in worker mode, capability freeze, and worker-policy preflight. Archive 298 adds per-module invocation gates and DOM teardown protection. Archive 299 adds cross-platform synchronization primitives and the MPMC ready-queue foundation; archive 300 integrates the runtime scheduler mutex and dispatch ownership; archive 302 connects the ready queue to the cooperative scheduler and replaces per-Channel spin gates with `FlatMutex`. DRAIN/CANCEL admission and cooperative shutdown status are implemented in archive 304; archive 305 adds the runtime-owned worker pool and explicit worker join. Lexbor remains `PENDING_THREAD_SAFETY_VALIDATION`, with `SERIALIZED` retained as fallback. See [`plans/06_WORKER_POOL_SPEC.md`](plans/06_WORKER_POOL_SPEC.md), [`plans/07_NATIVE_THREAD_SAFETY_AUDIT.md`](plans/07_NATIVE_THREAD_SAFETY_AUDIT.md), [`plans/08_EXTERNAL_DEPENDENCY_AUDIT.md`](plans/08_EXTERNAL_DEPENDENCY_AUDIT.md), [`plans/09_CANCELLATION_SHUTDOWN_SPEC.md`](plans/09_CANCELLATION_SHUTDOWN_SPEC.md), [`plans/10_NATIVE_SYNC_GATES.md`](plans/10_NATIVE_SYNC_GATES.md), [`plans/11_SCHEDULER_SYNC_FOUNDATION.md`](plans/11_SCHEDULER_SYNC_FOUNDATION.md), [`plans/12_RUNTIME_SCHEDULER_MUTEX.md`](plans/12_RUNTIME_SCHEDULER_MUTEX.md), [`plans/14_READY_QUEUE_CHANNEL_MUTEX_INTEGRATION.md`](plans/14_READY_QUEUE_CHANNEL_MUTEX_INTEGRATION.md), and [`doc_src/NATIVE_THREAD_CAPABILITIES.md`](doc_src/NATIVE_THREAD_CAPABILITIES.md).


Task Runtime archive 305 adds runtime-owned workers through `flat_runtime_workers_start()` and `flat_runtime_workers_join()`. Worker threads share the existing ready queue, scheduler mutex, dispatch ownership and DRAIN/CANCEL lifecycle. See `plans/17_RUNTIME_WORKER_POOL.md`.


Archive 304 adds cooperative runtime shutdown lifecycle (DRAIN/CANCEL), blocked-wait reporting, admission closure and cancellation escalation. Archive 305 adds the actual runtime-owned worker pool and explicit join. See [`plans/16_RUNTIME_SHUTDOWN_STATE_MACHINE.md`](plans/16_RUNTIME_SHUTDOWN_STATE_MACHINE.md) and [`plans/17_RUNTIME_WORKER_POOL.md`](plans/17_RUNTIME_WORKER_POOL.md).


The worker-mode task.wait/Channel contention audit is documented in [`plans/18_WORKER_TASK_WAIT_CHANNEL_AUDIT.md`](plans/18_WORKER_TASK_WAIT_CHANNEL_AUDIT.md).


Archive 307 adds `flvm[44]` for real-worker wait-graph cancellation and Channel waiter cleanup. It validates cycle rejection, middle-node cancellation/wakeup, Channel reference release, DRAIN/CANCEL and join; it does not change runtime synchronization semantics. See [`plans/19_WORKER_WAIT_GRAPH_CANCELLATION_AUDIT.md`](plans/19_WORKER_WAIT_GRAPH_CANCELLATION_AUDIT.md).


Archive 308 fixes four reported Termux test failures: standalone AM-NUM test locale linkage, the SCHM trailing-data corruption target, and timing-sensitive scheduler/cancellation test coordination. All four focused tests pass on the available POSIX host; Termux confirmation is still pending. See [`plans/20_TERMUX_TEST_PORTABILITY.md`](plans/20_TERMUX_TEST_PORTABILITY.md).
