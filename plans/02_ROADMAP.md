# Task Runtime & Channels — roadmap de implementare

**Versiune document:** 1.0 (plan)  
**Data:** 2026-11-25  
**Status (archive 356):** archive 351 FLEditor WASM Task/Channel integration was validated by the user on Termux (`make fleditor-wasm-test`) and manually in the browser with all three examples. Archive 352 implements block-scoped panic wildcard handlers (`#panic.#target.any`) with caller-chain and task-VM coverage. Archives 353–356 add Lexbor DOM construction/mutation, `setHtml`, `createFrag` and shallow/deep `clone`. The user reports the complete default suite on archive 356 as `Passed: 1176 Failed: 0 Total: 1176`. The next proposed direction is documented, but not implemented, in [66_EVENT_DRIVEN_TASKS_AND_EVENT_SOURCES.md](66_EVENT_DRIVEN_TASKS_AND_EVENT_SOURCES.md). Browser execution remains single-threaded; it does not start OS workers or browser Web Workers. Windows worker execution and broader cross-platform validation remain open; this is not blanket thread-safety certification. Archives 306–307 add focused real-worker audits for task.wait/Channel contention and wait-graph/Channel-waiter cancellation; archives 309 and 315 extend HTTP/DOM lifecycle and task-cleanup race coverage. Archive 316 expands concurrent first-use loading coverage across distinct native plugin modules and tests plugin loading against capability freeze. Archive 317 adds deterministic worker-create and worker-join failure injection, covering every partial-start index and retryable join cleanup. Archive 318 validates process-wide worker-mode reference ownership across multiple runtimes. Archive 319 serializes same-runtime join ownership and clarifies POSIX/Windows native handle semantics. Archive 320 adds a repeated multi-graph worker cancellation stress case (`flvm[47]`) with eight concurrent cancellation callers and DRAIN/cleanup validation. Archive 321 adds multi-Channel transaction contention and concurrent cancellation across DRAIN completion plus post-join CANCEL cleanup (`flvm[48]`). Archive 322 adds converging DAG cancellation (`flvm[49]`), archive 323 adds live staged cancellation on deeper DAGs (`flvm[50]`), and archive 324 adds reproducible seeded DAG topology/cancellation coverage (`flvm[51]`), archive 325 adds mixed DRAIN/CANCEL escalation soak (`flvm[52]`), archive 326 adds task-failure isolation across 100 runtime lifecycles (`flvm[53]`), and archive 327 adds a host-compiler regression for the single-thread Emscripten synchronization branch and clean worker-start rejection (`flvm[54]`). Cross-platform validation remains open. This remains a staged concurrency validation effort, not blanket thread-safety certification.

**DOM module checkpoint (archive 356):** archives 353–356 add node construction/mutation, contextual HTML fragment replacement (`setHtml`), detached `DocumentFragment` construction (`createFrag`) and shallow/deep node cloning (`clone`). This is the Lexbor in-memory DOM, not a browser live-DOM bridge. Focused DOM coverage and final validation are recorded in `plans/65_DOM_NODE_CLONING.md`; see plans 63–65.

## Direcția generală

Implementarea se face în straturi: mai întâi un runtime cooperativ izolat, apoi Channel cu mutex și tranzacții, apoi worker pool cu thread-uri reale. UI Runtime este o etapă separată, dependentă de contractele Channel și de constrângerile platformei.

## F0 — Foundation (înainte de v1.0)

### F0.1 — Audit

- Audit `flat_vm_run_opts()` și durata de viață a `VmState`.
- Audit executor global, frame stack, panic drain.
- Audit VCX active runtime și call-instruction resolver.
- Inventar stare globală VM/native: RNG, locale, native runtime reset, stdio și module native.
- Documentarea dependențelor și a riscurilor înainte de refactor.

### F0.2 — Logger central

- Logger comun cu categorii TASKS și CHANNELS.
- CLI `--show-tasks-log`, `--show-channel-log`.
- Logging off implicit și fast path fără alocare/formatare.
- Output live `stdout`; buffer opțional cu limită și dropped counter.
- Sequence number monoton.
- Teste pentru flags, filtrare, output, buffering și limită.

### F0.3 — Execution context refactor

**Status în arhiva 254: implementat structural; validarea completă a suitei rămâne separată.** Binding-ul executorului, frame stack-ul, call-instruction stack-ul și ID-ul call-instruction sunt deținute de contextul execuției. VM-ul transmite explicit VCX-ul și ID-ul call-instruction către native calls.

- [x] Mutarea binding-ului executorului și frame stack-ului din globale în context explicit per execuție/task.
- [x] Mutarea bookkeeping-ului call instruction în `FlatExecutorContext`; resolver-ul primește explicit `VmState`.
- [x] Eliminarea VCX active runtime/resolver global; native calls folosesc `FlatNativeCtx.vcx` și `FlatNativeCtx.call_instr_id`.
- [x] Panic drain și stack cleanup sunt scoped la contextul VM curent.
- [x] Testele existente pentru nested calls, unique/global executor și panic unwind trec (`func`, `fmod`).

**Criteriu de ieșire F0:** contextul de execuție poate fi creat/curățat independent fără să afecteze alt context; logger-ul poate fi activat fără impact când este dezactivat. F0 nu livrează încă `task.spawn`, scheduler sau Channel.

### F0.4 — Global State Audit & Isolation

**Status în arhiva 261: auditul principal al stării runtime este aproape închis; rămâne verificarea transversală și contractul final pentru host/WASM.**

- [x] Inventarierea globalelor mutabile relevante pentru VM/runtime.
- [x] Mutarea RNG-ului din stare process-globală în `VmState` și transmiterea prin `FlatNativeCtx.rng`.
- [x] Test de izolare pentru două instanțe RNG.
- [ ] Clasificarea finală a globalelor după ownership: process / VM / task; verificarea WASM bridge și a staging-ului host.
- [x] Mutarea date run state în `VmState` și transmiterea prin `FlatNativeCtx.date`.
- [x] Separarea manifestului per VM, inițializarea embedded registry-ului o singură dată și sincronizarea lazy loading-ului opțional; handler-ul este invocat în afara lock-urilor.
- [x] Audit locale, stdio, path/env, memory streams și logger; logger-ul este sincronizat, iar staging env/path rămâne configurare de proces înainte de run.
- [x] Identificată limita WASM singleton pentru last-result getters; criteriile complete de freeze/sincronizare pentru worker pool rămân de definit.
- [x] Mutat compiler panic-site/schema emission state în context local per compilare.

F0.4 nu declară VM-ul reentrant sau thread-safe. Registry-ul native este sincronizat, dar environment/path staging și WASM result bridge au încă limite de proces/instanță. Nu începe schedulerul până când ownership-ul stării necesare execuției persistente este clar și cleanup-ul este testat.

## v1.0 — Cooperative Task Runtime

Scop: task-uri reale, cooperative, pe un singur thread OS.

**Progres archives 276–279:** task registry C, scheduler round-robin, wait/cancel și native `task.spawn`/`task.wait`/`task.state` cu argumente numite sunt implementate. Lista de mai jos păstrează separat funcționalitățile încă neimplementate.

Livrabile:

1. [x] Model C `FlatTask`, `FlatTaskId` și lifecycle (`CREATED`, `RUNNABLE`, `RUNNING`, `WAITING`, `COMPLETED`, `FAILED`, `CANCELLED`).
2. [x] VM și VCX separat pentru fiecare task.
3. [x] Scheduler cooperativ round-robin cu quantum per turn, inclusiv `WAITING` și wakeup.
4. [x] `task.spawn(block, ...)`, `task.wait(taskId)` și `task.state(taskId)`; parametrii block-ului sunt legați din argumentele spawn.
5. [x] Persistența stării VM necesare suspendării/reluării: instruction pointer, call stack, loop frames și state necesar native execution.
6. [x] Izolare failure între task-uri; un task FAILED nu oprește schedulerul. Cancellation la safe point este implementată.
7. [x] Extinderea generală a panic block handler cu `.any`, inclusiv forma `#panic.#worker.any`; ordinea include block-ul faulting, caller blocks și fallback global.
8. [x] Cleanup determinist VM/VCX și executor scopes; Channel transactions sunt rollback + unlock la terminarea task-ului.
9. [x] Logging real al lifecycle-ului și testare runtime prin `bin/run_tests`.
10. [x] `task.result(taskId)` pentru metadata terminală; nu este return value al block-ului.

Criterii de acceptare:

- două task-uri au VCX-uri independente;
- task-ul poate fi pornit, așteptat și observat în stările documentate;
- panic-ul unui task nu oprește alt task;
- handler-ul `.any` este rezolvat în ordinea documentată și poate face jump în același context task;
- nu rămân scope-uri, VCX-uri sau resurse abandonate după succes/eșec.

## v1.5 — Shared Channels & Transactions

Scop: partajare explicită a datelor între task-uri. Archive 280 implements the runtime-owned C API and storage/transaction semantics; the FlatL native surface and task-handle transfer remain next.

Livrabile:

1. [x] Pool de Channel-uri runtime-owned și handle NUMBER (`FlatChannelId`).
2. [x] Native `channel.create()` pentru crearea handle-urilor runtime-owned.
3. [x] C API și natives `channel.set` / `channel.var` cu deep-copy între pool-uri.
4. [x] Semantica de copiere pentru primitive, strings, LIST și OBJECT; VAL_FILE este respins.
5. [x] Gate atomic per Channel și sincronizare C1 pentru fiecare `set`/`var` (nu este încă mutex pthread).
6. [x] C API și natives `channel.lock`, `channel.unlock`, `channel.revert` pentru tranzacții C2.
7. [x] Ownership tranzacțional prin task ID numeric.
8. [x] Snapshot deep-copy, revert și commit explicit.
9. [x] Auto-revert + release la terminare, failure, cancellation sau destroy al task-ului.
10. [x] Native `channel.destroy()` pentru distrugere explicită după ce task-urile nu mai folosesc handle-ul.
11. [x] Transfer numeric al handle-urilor prin `task.spawn()` și acces comun din task-uri diferite.
12. [x] Prioritate scheduler pentru task-urile care dețin tranzacții, astfel încât quantum-ul să nu fragmenteze o tranzacție cooperativă.
13. [x] `--show-channel-log` emite evenimente Channel structurate; `channel.log()` rămâne helper de diagnostic neimplementat.
14. [x] Teste pozitive/negative, copy LIST/OBJECT, transaction, revert, cleanup la failure și handle sharing.
15. [x] Wait/retry pentru native Channel la contendență; wakeup la commit și terminal cleanup, fără wakeup la revert.
16. [x] `task.result(taskId)` pentru rezultate terminale.

Criterii de acceptare:

- fiecare operație individuală protejează structura Channel;
- read-modify-write multi-operație este corect doar în tranzacție, conform contractului;
- un task nu poate elibera lock-ul altui task;
- panic/terminare nu lasă lock activ;
- canalele diferite nu se blochează reciproc.

## v2.0 — Parallel Runtime / Worker Pool

Scop: execuție efectivă simultană pe mai multe thread-uri OS.

Livrabile:

1. [x] POSIX worker pool pthread and worker loop; fixed-size startup, barrier, partial-start rollback, condition-variable idle wait and join are implemented in archive 305. Windows and Emscripten behavior still require toolchain validation.
2. [x] Core scheduler/ready-queue/Channel synchronization and worker lifecycle are implemented; remaining module/dependency audits are tracked separately.
3. [x] Mutex real per Channel prin `FlatMutex` (POSIX pthread / Windows SRWLOCK / Emscripten single-thread no-op).
4. [x] Ownership logic bazat pe `flat_task_id`, niciodată exclusiv pe `pthread_t`.
5. [x] Principal global-state ownership audit and native worker capability preflight/gates implemented; per-module and dependency validation remains in progress.
6. [x] DRAIN/CANCEL shutdown, waiting-task blocked status, worker error escalation and join implemented; expanded wait-graph cancellation regression added in archive 307.
7. [ ] Broader stress matrix for multi-writer, arbitrary wait graphs, simultaneous cancellation, deadlock and cleanup. Archives 320–322 add independent/converging graphs and Channel contention (`flvm[47–49]`); archive 323 adds deterministic varied fan-out and staged cancellation at RUNNING/WAITING safe points (`flvm[50]`). Seeded reproducible DAG topologies are now covered by archive 324 (`flvm[51]`); mixed DRAIN/CANCEL escalation under randomized load and long-duration soak remain open.
8. [ ] Validate Windows worker execution and actual Emscripten build; [x] single-thread Emscripten worker startup rejection is covered by a host-compiler branch simulation in archive 327.

Criterii de acceptare:

- mai multe task-uri rulează efectiv în paralel;
- fără corupere a Channel sau runtime;
- panic-ul unui task nu afectează pool-ul;
- shutdown-ul eliberează toate resursele;
- testele relevante trec pe platformele suportate.

## v2.5 — UI Runtime (etapă separată)

Scop: reguli pentru UI thread și bridge între UI și logică.

Direcție propusă:

- un singur UI owner thread per proces/instanță WASM;
- apelurile `ui.*` din alt task produc o eroare precum `task.ui.wrong_thread`;
- workers nu apelează direct natives de randare/widget;
- Channel bridge pentru evenimente/stare UI ↔ logică;
- integrare cu suspendare/reluare VM în WASM (Asyncify și constrângerile browser threading);
- prototip în FLEditor: input UI → Channel → FlatL și actualizări înapoi.

Nu intră aici un toolkit UI complet, layout engine sau binding la React.

## v3.0 — Event-driven Task Dispatch & Host Event Sources (implementation starting)

Scop: asocierea unui block FlatL cu un event source prin `task.spawnFor()`, astfel încât fiecare eveniment să poată crea o execuție FlatL nouă, fără polling și fără a lega Task runtime de un anumit transport.

- [x] Definește primul contract runtime generic: `(sourceId, sourceKind, eventName)` și ownership prin deep-copy în cozi bounded (arhiva 359).
- [x] Implementează brokerul, dispatch-ul automat în scheduler și API-ul C pentru handler persistent (arhivele 359–363).
- [x] Expune `task.spawnFor(sourceId, sourceKind, eventName, #handler, ...args)` și `task.stopFor(subscriptionId)` în FlatL (arhiva 364).
- [x] Definește și implementează în API-ul C modurile `parallel` (implicit) și `serial`, cu FIFO și cel mult un handler activ pentru abonările seriale (arhiva 365).
- [x] Expune selectarea modului în native-ul FlatL (`task.spawnForMode`) și implementează `maxActive` prin `task.spawnForLimit` (arhiva 367).
- [ ] Implementează coada bounded configurabilă și overflow/backpressure.
- [ ] Definește failure isolation, panic, cancellation, source close și runtime shutdown.
- [ ] Implementează dispatch generic folosind un event source intern de test.
- [ ] Integrează un prim event source nativ/socket după alegerea backend-ului de event loop.
- [ ] Proiectează sesiunea persistentă FLEditor WASM; runner-ul actual `flat_runtime_run_scheduled()` este one-shot și distruge runtime-ul la final.
- [ ] Integrează browser WebSocket/SSE/timer și UI events după stabilirea capabilităților și a UI bridge-ului.

Capabilitățile trebuie raportate explicit per platformă: TCP/UDP raw sunt disponibile numai pe backend-uri native; browser WASM poate expune WebSocket, SSE, Fetch completion, timer și UI events prin API-urile browserului. Nu se emulează raw TCP/UDP în browser.

Contractul conceptual stabilit pentru `task.spawnFor()` este `task.spawnFor(sourceId, sourceKind, eventName, #handler, arg1, ...)`: `sourceId` este NUMBER, iar `sourceKind` este explicit. Toate transporturile din `sockets` folosesc același kind `'socket'`; WebSocket/TCP/UDP rămân detalii ale adaptorului sockets. Nu introducem obiecte sau resurse OO. Ordinea și criteriile detaliate, inclusiv testarea exclusiv prin `bin/run_tests` și `test/cases/`, sunt în [66_EVENT_DRIVEN_TASKS_AND_EVENT_SOURCES.md](66_EVENT_DRIVEN_TASKS_AND_EVENT_SOURCES.md). Implementarea a început cu infrastructura generică runtime de înregistrare/livrare a evenimentelor; arhiva 364 expune deja API-ul FlatL `spawnFor`/`stopFor`. Semantica existentă `task.spawn`, `task.wait` și `task.result` rămâne neschimbată.

## Ordine tehnică de lucru

1. Audit înainte de cod.
2. Mini-spec pentru ownership, lifecycle, yield/wait, panic și erori.
3. Teste în `test/cases/` prin `bin/run_tests`.
4. Implementare incrementală, fără amestecarea schedulerului cooperativ cu pthread.
5. Diagnostic structurat pentru stări interne.
6. Teste negative și cleanup/fault paths.
7. Build, suite relevante, apoi regresie completă.
8. Actualizare documentație în același commit cu schimbarea.

Estimările din planul original sunt orientative și nu reprezintă angajamente calendaristice.

## Runtime readiness checkpoint — archive 262

The transversal audit has now covered the previously identified process globals and several hidden function-local caches. Environment/path staging snapshot operations, libcurl first-use, Windows QPC initialization, math-mdim debug initialization, native signature fallback text, WASM module-list formatting, and Windows temp-script sequencing have been made concurrency-safe at their respective boundaries. See [03_RUNTIME_AUDIT.md](03_RUNTIME_AUDIT.md) for the current ownership matrix and limitations.

This checkpoint is **not** a declaration of general thread safety. Before persistent tasks/scheduler work, still define the WASM bridge's instance model, keep host process environment immutable during VM runs, audit parser/linker shared state and plugin-owned globals, and specify VM persistence/cleanup semantics. The next engineering step should be a focused parser/linker audit and an explicit runtime host configuration object design, not a pthread worker pool.

## Runtime readiness checkpoint — archive 263

Parser/linker/compiler source paths have been audited for shared mutable state. Parser/linker workspaces are invocation-local; the larger import graph/module workspaces are now heap-owned per link call to avoid large worker-thread stack frames. FlatL syntax classification is ASCII-defined and no longer depends on process locale. A new `FlatHostConfig` can be independently initialized or captured from legacy staging and passed through `FlatVmOptions`; the VM snapshots it at run start. Existing callers that omit it retain legacy behavior.

Remaining constraints before scheduler design: relative paths still depend on a stable process working directory; source files must not be externally modified during linking; WASM remains a single-instance bridge with fixed compile paths and singleton result state; native plugins can have their own globals; process environment reads and process-wide services remain process-owned. The next step is to define the runtime-instance lifecycle and WASM/host ownership contract, then perform a final integration audit. Do not start the pthread worker pool yet.

## Runtime lifecycle checkpoint — archive 264

An opaque `FlatRuntime` now owns an immutable deep copy of `FlatHostConfig` and provides a run boundary that supplies this configuration to each VM. Runtime destruction is rejected while calls are active; callers must externally synchronize the start of new calls against destruction. VM state remains per invocation and is not yet persistent across yield/resume. The next step is a runtime resource ownership audit and explicit lifecycle/error semantics before introducing persistent VM/task state. Do not add a worker pool yet.

## Runtime resource ownership checkpoint — archive 265

The `FlatRuntime` resource audit is complete. `FlatRuntime` owns the immutable host configuration snapshot; mutable execution resources (VCX, executor, CSV/DOM/HTTP/IO/file/pools, RNG/date and VM env/path snapshots) remain per `VmState` invocation. Native plugin availability, loaded libraries, logger, locale, libcurl global initialization, and fallback memory-stream registry remain process-owned. Bytecode and VM output buffers remain caller-owned. `flvm[17]` now proves that a real VM execution error tears down invocation resources and that the same runtime can execute successfully again afterward.

The next step is to specify the persistent VM/task boundary before implementation: distinguish a reusable runtime from a persistent VM instance, define VM states and ownership of bytecode/options, specify cooperative yield points and resume invariants, and define panic/cancel/destroy behavior. Do not add pthread workers or Channels until the single-thread cooperative lifecycle is specified and tested.

## Persistent VM/task lifecycle specification — archive 266

The normative contract is now in `doc_src/TASK_RUNTIME_MODEL.md`. `FlatRuntime`, `FlatTask`, and `FlatVm` have separate roles. Task lifecycle states and transitions are fixed; cooperative yield is permitted only at completed bytecode-instruction boundaries; resume continues at the saved next IP; bytecode and scalar options are VM-owned; output pointers are never retained across yields; panic/failure/cancellation cleanup is task-local; and `task.wait` is scheduler-level rather than an OS blocking wait.

Archive 266 is intentionally a specification checkpoint, not a feature implementation. The next code milestone is to move the continuation state out of the stack-local `flat_vm_run_opts()` frame into a heap-stable opaque `FlatVm`, refactor the opcode loop to support bounded stepping, and preserve `flat_vm_run_opts()` as a run-to-completion wrapper. Do not expose `FlatTask` natives or add Channels/pthreads until bytecode ownership, resume equivalence, call/loop/executor/panic continuation, and cleanup tests pass.


## FlatVm implementation checkpoint — archive 267

An opaque `FlatVm` ownership handle is now implemented. It snapshots bytecode, scalar options, and host configuration at creation and provides a one-shot run-to-completion API with a value result. This validates ownership boundaries and removes caller-lifetime dependencies for these inputs. It does **not** yet move `VmState` or the instruction pointer out of `flat_vm_run_opts()` and does not provide yield/resume. The next milestone remains continuation-state extraction and bounded stepping; task scheduling remains blocked on that work.


## VM continuation implementation checkpoint — archive 268

`VmState` is now heap-allocated at a stable address during each `flat_vm_run_opts()` invocation, preventing its address from changing after VCX/executor pointers are bound into it. This is an intermediate ownership improvement only: IP, guard scratch arrays, and the opcode loop remain invocation-local, and teardown still occurs before returning. The next step is to move all continuation state into the persistent `FlatVm` handle and introduce bounded stepping/yield/resume.


## VM continuation ownership checkpoint — archive 269

The invocation's instruction pointer, entry-block index, and guard scratch arrays now live in `VmState` rather than as independent locals in `flat_vm_run_opts()`. Step/jump counters and call/loop frames were already part of `VmState`. This consolidates the interpreter continuation data model and removes the unused borrowed `FlatVmOptions *` field from `VmState`. It is still a per-invocation state object: `flat_vm_run_opts()` allocates it, initializes all resources, runs to terminal state, tears it down, and frees it. The next structural step is to make the opaque `FlatVm` own this initialized state across calls; no stepping/yield/resume is available yet.


## FlatVm lifecycle outcomes — archive 270

The public `FlatVmState` now distinguishes successful completion (`FLAT_VM_COMPLETED`) from execution failure (`FLAT_VM_FAILED`), and `flat_vm_is_terminal()` provides a common terminal-state check. `FLAT_VM_TERMINAL` remains a compatibility alias for successful completion. This is an API/lifecycle clarification only; persistent VM continuation and bounded stepping remain the next implementation milestone.

## FlatVm initialized-state ownership — archive 272

`FlatVm` now creates and owns the initialized `VmState` at `flat_vm_create()`. The state remains at a stable heap address through the READY lifecycle; `flat_vm_execute()` consumes that owned state, and `flat_vm_destroy()` releases it. A READY VM destroyed without execution tears down initialized runtime resources exactly once. The compatibility `flat_vm_run_opts()` uses the same internal state create/execute/destroy bridge, preserving its run-to-completion API. This is persistent ownership, not yet persistent execution: terminal execution tears down resources and there is still no step budget, `YIELDED`, or resume API.

Archive 272 validation: `make all -j2` PASS; `make docs` PASS (46 sections); `flvm[1-17]` PASS in the combined run and `flvm[18]` PASS separately; `func[1-37]` PASS (31 discovered cases); `vcx` started but timed out after 6 cases, so the suite is not declared PASS. The full default suite (1153 discovered tests) was not run.


## F0.6 — Persistent VM stepping (archive 273)

- [x] Add `flat_vm_step()` with an instruction budget and `FLAT_VM_YIELDED`.
- [x] Yield only between completed opcode dispatches; preserve the VM's complete heap-owned state for resume.
- [x] Preserve run-to-completion compatibility APIs.
- [x] Add a basic one-opcode-budget resume test.
- [ ] Expand equivalence coverage to calls, loops, executor scopes, panic handlers, and observable side effects before implementing scheduler/task abstractions.


## F0.6 — control-flow resume coverage (archive 274)

Added `test/cases/flvm/020_vm_stepping_control_flow`. The test compiles one program that combines nested block calls, a `for` loop, a `foreach` loop, nested unique executor scopes with `func:callVM 1`, and a jump-limit panic recovered by `#panic.any`. It runs the same bytecode once through `flat_vm_run_opts()` and once through repeated `flat_vm_step()` calls with rotating budgets of 1–3 opcodes. It checks that both executions complete with the same exit code and termination, and that the stepped execution yielded at least once. The program also checks loop results and nested function output before entering the recovered-panic path.

This closes the first control-flow continuation coverage milestone. It does not establish full output-side-effect equivalence, arbitrary native-call preemption, or task/scheduler readiness. Caller-owned output pointer lifetime across yields remains a separate acceptance test.

## Persistent FlatVm stepping checkpoint — archive 275

The opaque `FlatVm` now supports cooperative instruction-budget stepping and resume. Archive 275 strengthens the regression boundary: `flvm[20]` compares captured stdout byte-for-byte between the run-to-completion interpreter and stepped execution over nested calls, loops, executor scopes, and panic recovery. `flvm[21]` verifies logical `FAIL`, unhandled panic, terminal-step rejection, and destruction immediately after yield. The step budget remains distinct from the cumulative `max_steps` limit, and a native call is still non-preemptible.

The VM stepping contract is sufficiently covered to begin designing the `FlatTask` ownership/lifecycle API, but this checkpoint does not implement tasks or a scheduler. Before that implementation, define task ID allocation, runtime registry ownership, scheduling quantum/result propagation, panic isolation, wait dependency handling, and runtime-destroy behavior. Keep pthread workers and Channels out of the first cooperative task milestone.


## Cooperative task runtime checkpoint — archive 276

The first C-level task/scheduler layer is implemented in `include/flat_task.h` and `src/common/flat_runtime.c`. `FlatRuntime` owns task records; each task owns a persistent `FlatVm`, a monotonic runtime-local ID, lifecycle state, and terminal result. Tasks are explicitly started from `CREATED` to `RUNNABLE`. The scheduler is synchronous, single-threaded, and round-robins runnable tasks by ID, calling `flat_vm_step()` with a caller-supplied instruction quantum. Yielded tasks become runnable again; completed and failed tasks become terminal independently.

The host must destroy task records before destroying the runtime. Task IDs are not reused. Task registry/scheduler APIs are confined to the scheduler-driving host thread. This checkpoint does not implement `WAITING`/wakeup, cancellation, task natives (`task.spawn` / `task.wait`), Channels, or pthread workers.


### Cooperative tasks — archive 277

C-level wait/wakeup and safe-point cancellation are now available. Waiters resume when their target completes, fails, or is cancelled; targets cannot be destroyed while dependants are waiting. Cancellation is accepted only between scheduler turns for CREATED/RUNNABLE/WAITING tasks. Waiting-cycle detection, FlatL task natives, Channels, and worker threads remain future work.


### FlatL task natives — archive 279

The language-level task surface is available through `use native task`: `task.spawn(#block, ...)`, `task.wait(taskId)`, and `task.state(taskId)`. Spawn starts a child VM at a named block in the same bytecode and binds positional values to that block's declared parameter names. Scalars, strings, LISTs, and OBJECTs are deep-copied into the child's private VM pools; file handles are rejected. `.fb` v14 adds BPAR metadata for block parameter names, while the loader remains compatible with v3–v13. Wait is cooperative: it records a dependency, completes the native instruction, and yields the current task without blocking the scheduler thread. State query exposes the numeric lifecycle enum. Next work should define explicit Channel handle transfer and review wait-cycle detection; do not add pthread workers until the cooperative semantics and shared-data ownership are stable.


### Runtime Channels — archive 280

The C-level Channel foundation is implemented in `include/flat_channel.h` and `src/common/flat_channel.c`. `FlatRuntime` owns the Channel registry and monotone numeric IDs. Each Channel owns independent string/collection pools; `set` and `var` deep-copy supported values across VM pools, while file handles are rejected. C1 operations are serialized by a per-Channel C11 atomic gate. C2 transactions use a deep snapshot and nonzero task-ID ownership; `unlock` commits and `revert` restores the snapshot while keeping the logical lock. Non-owner access returns busy. A task's open Channel transactions are automatically rolled back and unlocked on completion, failure, cancellation, or destruction.

Archive 281 exposes `channel.create/set/var/lock/unlock/revert/destroy`. Channel IDs are ordinary NUMBER values and can be passed through `task.spawn`. The scheduler prioritizes runnable transaction owners until they release their locks; `task.wait()` is rejected while the current task owns a transaction. `channel.destroy()` is explicit and is not reference-counted. No pthread/VM thread-safety claim is made; the future worker-pool milestone must audit/replace the cooperative atomic gate as appropriate.

## Channel/task lifecycle checkpoint — archive 282

Archive 282 closes three cooperative-runtime gaps: the C task wait graph rejects a new edge that would create a cycle; direct numeric Channel IDs passed through `task.spawn()` are held as task references until terminal cleanup; and the existing `--show-channel-log` category now receives structured Channel operation events. The handle-lifetime rule is intentionally conservative because Channel IDs remain ordinary NUMBER values: a direct numeric spawn argument equal to a live Channel ID is treated as a handle. Nested LIST/OBJECT contents are not scanned. Non-owner transaction waiting and pthread worker safety remain future work.

### Cooperative contention and task results — archive 283

Channel transaction contention is now scheduler-aware for FlatL task natives. A contending `channel.lock`, `channel.set`, or `channel.var` suspends the task and retries the same native instruction after the transaction is released. The task keeps a Channel reference while waiting. `task.result(taskId)` exposes a terminal task execution summary as an OBJECT, and `flvm[29]` / `flvm[30]` cover contention and bounded runtime stress. The next milestone should audit remaining scheduler edge cases and expand regression coverage before any pthread worker pool is attempted.


## Cooperative scheduler correctness checkpoint — archive 284

- [x] Confirmat contractul distinct `yield completed` versus `retry pending`: task.wait avansează după native; Channel contention păstrează IP-ul pe instrucțiunea care trebuie reîncercată.
- [x] Confirmată re-verificarea lock-ului la înregistrarea waiter-ului, pentru a acoperi unlock-ul intervenit între busy și wait registration.
- [x] Confirmată păstrarea referinței Channel pe durata WAITING și eliberarea ei numai la cleanup terminal.
- [x] Extins `flvm[29]` pentru a verifica wakeup/retry după cancellation și failure a owner-ului, pe lângă unlock normal.
- [x] Rulate `flvm[29]` și `flvm[30]` individual; ambele PASS.

**Următorul pas:** audit focalizat al interacțiunii dintre wait graph, Channel references și task destruction; apoi regresie mai largă. Nu începe P1/pthreads.


## Cooperative lifecycle checkpoint — archive 285

- [x] Confirmed task destroy rejects targets with incoming wait dependencies.
- [x] Confirmed WAITING tasks must be cancelled before destroy and cancellation clears their outgoing dependency.
- [x] Confirmed Channel waiter references survive wakeup/transition to RUNNABLE and are released on cancellation/terminal cleanup.
- [x] Extended `flvm[29]` with owner-destroy, waiter-cancel, wake-before-retry, and Channel-destroy ordering checks.
- [x] Kept runtime implementation unchanged because the audited guards/cleanup paths satisfy these cooperative cases.

**Next:** continue the wait-graph and resource-lifetime audit across task cancellation, owner terminal cleanup, Channel destruction, and runtime destruction; then run a broader regression set. Do not start pthread workers until host-thread API confinement is replaced by an explicit synchronization design.


## Cooperative multi-Channel suspension guard — archive 286

- [x] Identified that a task holding a transaction on Channel A could otherwise enter WAITING after contention on Channel B.
- [x] Rejected Channel `lock` contention for a task that already owns any Channel transaction.
- [x] Applied the same guard to the shared retry-registration path used by contended `channel.set` and `channel.var`.
- [x] Added regression coverage for all three operations; each task fails rather than suspending, and terminal cleanup releases the transaction it held on Channel A.
- [x] Documented the invariant in `plans/01_DECISIONS.md`, this roadmap, the runtime audit, implementation status, and `TASK_RUNTIME_MODEL.md`.

**Validation:** `make all -j2` PASS; `flvm[29]` PASS with the three new multi-Channel cases. Broader regression results are recorded in `04_IMPLEMENTATION_STATUS.md`.

**Next:** audit the remaining task wait graph and Channel wakeup cases with multiple Channels and multiple waiters, then inspect runtime teardown and cleanup ordering. Keep pthread/worker-pool work blocked until the task registry, scheduler, VM/native state, and process services have an explicit cross-thread ownership/synchronization design.


## Cooperative multi-Channel wait graph checkpoint — archive 287

- [x] Verify one owner can hold transactions on two distinct Channels.
- [x] Verify a waiter on Channel A wakes on A's commit while a waiter on Channel B remains tied to the same owner until B is aborted during owner terminal cleanup.
- [x] Verify both waiters become RUNNABLE after the owner completes and independently retry/complete.
- [x] Repair the archive-286 no-suspend regression setup: after Channel 1 had been destroyed, the old test still attempted to lock Channel 1 and therefore failed before reaching the intended contention. The test now uses the actual live Channel IDs (3 and 4), so it proves lock ownership on Channel 3 before contending on Channel 4.
- [x] Keep the regression in `flvm[29]` and the existing `bin/run_tests` framework.

**Next:** audit chained task.wait + Channel-owner dependencies and task-target terminal wakeup ordering. Keep pthread workers blocked until the complete scheduler/task/Channel registry protocol is redesigned for concurrent host callers.


## Cooperative chained wait graph checkpoint — archive 288

- [x] Verify a Channel waiter can be the target of a separate `task.wait()` dependency.
- [x] Verify another task can wait directly for the transaction owner at the same time.
- [x] Verify Channel unlock wakes only the Channel waiter, leaving both `task.wait()` dependants WAITING.
- [x] Verify completion of the Channel waiter wakes its dependent, without waking the task waiting directly for the owner.
- [x] Verify owner completion later wakes the independent direct waiter.
- [x] Record why a mixed cycle through an active Channel owner is excluded by the existing no-suspend-while-owning-transaction invariant; do not overstate this as general deadlock freedom.
- [x] Keep the regression in `flvm[29]` and the existing `bin/run_tests` framework; runtime implementation unchanged.

**Next:** audit cancellation/failure at each point in a chained dependency (`owner`, Channel waiter, and downstream task waiter), then verify complete runtime teardown after all edges and Channel references have been cleared. Keep pthread workers blocked.

## Cooperative chained cancellation/failure checkpoint — archive 289

- [x] Verify cancellation of a Channel transaction owner wakes the Channel waiter and direct `task.wait()` dependants independently.
- [x] Verify a task waiting on a Channel waiter is woken when that intermediate waiter is cancelled.
- [x] Verify wakeup is not transitive: the dependent remains subject to its own next operation and may contend on the still-locked Channel until the owner releases it.
- [x] Verify a Channel waiter that wakes, retries its lock successfully, and then fails still wakes its downstream `task.wait()` dependent through terminal cleanup.
- [x] Keep the tests in `flvm[29]` and the existing `bin/run_tests` framework; runtime implementation unchanged.
- [x] Destroy all tasks in the new chains before `flat_runtime_destroy()` and verify teardown succeeds.

**Validation:** `flvm[23]`–`flvm[30]` PASS in individual runs; `native` 22/22 PASS; `native_use` 6/6 PASS; `make all -j2` PASS; `make docs` PASS (49 sections). Full default suite (1165 discovered tests) not run. No pthread/thread-safety claim.

**Next:** audit runtime teardown and resource cleanup with unresolved WAITING/RUNNABLE tasks, retained Channel references, and invalid destroy ordering. Keep pthread workers blocked.


## Cooperative runtime teardown checkpoint — archive 290

- [x] Verify `flat_runtime_destroy()` rejects a runtime with a CREATED task and registered Channel.
- [x] Verify a RUNNABLE task holding an active Channel transaction prevents runtime destruction; task cancellation rolls back the transaction before task/Channel removal.
- [x] Verify a WAITING Channel waiter prevents task/runtime destruction and keeps the Channel pinned.
- [x] Verify owner cancellation wakes the waiter but does not remove its task record or Channel reference; runtime and Channel destruction remain rejected until the waiter is cancelled/destroyed.
- [x] Verify terminal task records still keep the runtime alive until explicit `flat_runtime_task_destroy()`.
- [x] Verify successful teardown order: task cancellation/terminal cleanup → task record destruction → Channel destruction → runtime destruction.
- [x] Recreate a fresh runtime and verify task/Channel IDs and registries start independently.
- [x] Keep all regression coverage in the existing `bin/run_tests` framework (`flvm[29]`); no Python test runner or standalone test framework.

**Audit conclusion:** no runtime implementation change was needed. Existing counters and ownership guards enforce the explicit teardown contract in the tested cooperative cases. `flat_runtime_destroy()` does not force-cancel tasks or destroy Channels. The host must prevent new API calls from starting concurrently with destroy; this remains outside the supported thread-safety contract.

**Next:** perform a final cooperative-runtime readiness review: enumerate process-owned services still shared by VM invocations, classify which state can be per-worker versus process-wide, and write the worker-pool synchronization/ownership design before implementing pthread workers. Do not start pthread implementation in this checkpoint.


## Worker-pool architecture specification checkpoint — archive 291

- [x] Reinspect the archive-290 source rather than treating earlier chat summaries as proof.
- [x] Classify runtime/task/VM/Channel ownership and identify which current structures are not protected by a complete cross-thread protocol.
- [x] Specify worker ownership, queue/dispatch, wakeup races, cancellation safe points, Channel lock ordering, native/plugin policy, output behavior, shutdown and platform fallback in `plans/06_WORKER_POOL_SPEC.md`.
- [x] Record the concrete unsynchronized mutable HTTP multipart sequence in `src/lib/mod_http.c` as a pre-worker blocker.
- [ ] Complete the static mutable-state audit for every embedded native module and supported optional plugin.
- [ ] Resolve worker-mode cancellation API semantics and drain/cancel shutdown API before coding.
- [ ] Implement and test the runtime scheduler mutex/queue/worker ownership protocol.
- [ ] Add deterministic multi-worker race tests and sanitizer validation.

**Conclusion:** archive 291 is specification-only. It does not add pthread, change task API behavior, claim thread safety, or mark v2.0 complete. The current `FlatRuntime` atomic gate and per-Channel atomic gate protect selected critical sections only; task registry/state/wakeup/scheduler operations are not yet a complete concurrent protocol.

**Next:** continue the pre-implementation audit by enumerating mutable static state across all embedded native modules and optional plugin entry points, then decide whether unknown plugins are serialized per module or rejected in worker mode. Fix the confirmed HTTP multipart sequence race before any concurrent-native test is treated as safe.


## Native module thread-safety audit checkpoint — archive 292

- [x] Inventory mutable file-scope state in embedded native modules and built-in optional plugin entry points.
- [x] Classify native handler state as VM/context-owned, process-owned, runtime-shared, or externally owned.
- [x] Audit process-wide native registry/plugin host, environment staging, path default, locale, logger, memory-stream fallback, timer initialization, HTTP/curl initialization, RNG ownership and WASM bridge state.
- [x] Fix the HTTP multipart sequence race using a process-wide atomic sequence allocator.
- [x] Add `flvm[31]`, a pthread regression that concurrently allocates 40,000 HTTP boundary sequence values and verifies uniqueness/completeness.
- [x] Run the actual HTTP multipart cases `http[28-32]` after the refactor.
- [x] Record worker-mode plugin capability semantics: `THREAD_SAFE`, `SERIALIZED`, `UNSUPPORTED`; unknown third-party plugins default to `UNSUPPORTED` until explicitly declared.
- [ ] Verify external dependency thread-safety for Lexbor, PCRE2, libcurl and supported platform C runtimes.
- [ ] Implement plugin capability metadata and worker-mode enforcement.
- [ ] Add concurrent complete-VM tests after the worker scheduler is implemented.
- [ ] Run ThreadSanitizer and AddressSanitizer/UndefinedBehaviorSanitizer builds.

**Conclusion:** archive 292 removes one confirmed native-side race and documents the mutable process-owned state. It does not make the runtime scheduler, task APIs, Channel registry, wait graph, cancellation or teardown thread-safe. Worker implementation remains blocked.

**Next:** settle the exact public plugin capability declaration and host lifecycle/freeze API, then design the synchronization boundary for `task`/`channel` native bridges. Do not add pthread workers until the scheduler and runtime lifetime protocol are implemented and tested.


## External dependency concurrency audit checkpoint — archive 293

- [x] Review bundled PCRE2 10.49 configuration and compare FlatL per-call object ownership with upstream multithreading guarantees.
- [x] Review bundled Lexbor 3.0.0 ownership and global allocator setup; classify DOM as `PENDING_THREAD_SAFETY_VALIDATION` with `SERIALIZED` as the preferred fallback if independent concurrency cannot be certified.
- [x] Review libcurl easy-handle ownership, global initialization, signal behavior and resolver caveats.
- [x] Add `flat_http_prepare()` for host-side libcurl initialization before worker creation (WASM no-op).
- [x] Set `CURLOPT_NOSIGNAL=1L` on every native easy handle.
- [x] Document deployment checks for `CURL_VERSION_THREADSAFE`, resolver backend, and system-library variants.
- [ ] Add plugin capability metadata/enforcement and finalize worker output serialization policy.
- [ ] Settle public cancellation-request and shutdown/drain API semantics.
- [ ] Implement scheduler mutex/queue/worker ownership and task dispatch token.
- [ ] Add complete-VM concurrency tests for independent PCRE2, DOM and HTTP operations.
- [ ] Validate supported OS/C runtime and system dependency matrix under sanitizers.

**Conclusion:** archive 293 hardens HTTP worker preflight and narrows external dependency uncertainty. PCRE2 is a strong candidate under the bundled no-JIT build; libcurl is conditionally compatible when preinitialized and configured as documented; Lexbor's status is pending validation, not unsupported. Archive 294 records a process-wide serialized DOM fallback if independent-context concurrency cannot be certified. This is not permission to start pthread workers: scheduler/task/Channel synchronization remains the primary blocker.

**Next:** settle cancellation-request and shutdown semantics plus plugin capability metadata, then design the platform thread abstraction and runtime lock/lifetime protocol. Keep actual worker creation blocked until those contracts are implemented and tested.


## Lexbor worker-mode classification checkpoint — archive 294

- [x] Replace the ambiguous “DOM disabled/not certified” wording with the explicit audit status `PENDING_THREAD_SAFETY_VALIDATION` (not a runtime capability).
- [x] Preserve `THREAD_SAFE`, `SERIALIZED`, and `UNSUPPORTED` as the only eventual capability values.
- [x] Record `SERIALIZED` as the preferred fallback if independent per-VM Lexbor use cannot be certified.
- [x] Include Lexbor-backed document destruction in the serialization boundary; a handler-only gate would miss `lxb_html_document_destroy()` called from `flat_dom_runtime_destroy()` during VM teardown.
- [ ] Add deterministic concurrent complete-VM DOM tests for bundled Lexbor and each supported system-Lexbor build.
- [ ] Run DOM concurrency tests under ThreadSanitizer and AddressSanitizer/UndefinedBehaviorSanitizer.
- [ ] Decide `THREAD_SAFE` versus `SERIALIZED` from test results and dependency/source guarantees; reserve `UNSUPPORTED` for a demonstrated uncontainable constraint.

**Conclusion:** archive 294 is a documentation and architecture-decision checkpoint only. It does not add a DOM mutex, enable DOM in worker mode, or claim Lexbor thread safety. If the concurrent tests cannot establish safe independent-context use, the implementation fallback is one process-wide DOM gate around every Lexbor operation, including VM/DOM teardown.

**Next:** settle cancellation-request and drain/cancel shutdown contracts plus plugin capability metadata, then implement the runtime synchronization/lifetime protocol. DOM remains pending; do not start pthread workers yet.


## Lexbor independent-context concurrency checkpoint — archive 295

- [x] Add a pthread regression using the bundled Lexbor 3.0.0 amalgamation used by `mod_dom.c`.
- [x] Exercise independent document creation, HTML parsing, CSS parser/selector creation and lookup, attribute mutation/readback, HTML serialization, and document/parser/selector destruction.
- [x] Run eight concurrent threads for 2,000 document lifecycles.
- [x] Run the focused test under ThreadSanitizer and AddressSanitizer/UndefinedBehaviorSanitizer.
- [x] Re-run existing FlatL DOM integration regressions.
- [ ] Run equivalent tests against each supported system-Lexbor build; no system Lexbor package was available in this validation environment.
- [ ] Run complete-VM DOM concurrency tests after the worker scheduler and runtime lifetime protocol are implemented.
- [ ] Decide `THREAD_SAFE` versus `SERIALIZED` from complete-VM, build-matrix and sanitizer evidence.

**Conclusion:** bundled Lexbor's independent document/parser/selector contexts passed the focused pthread stress test and both sanitizer configurations. This supports, but does not complete, the `THREAD_SAFE` qualification. The test directly exercises Lexbor rather than concurrent FlatL VMs; scheduler, runtime, native bridge, and VM teardown concurrency remain untested. Keep DOM `PENDING_THREAD_SAFETY_VALIDATION` and retain `SERIALIZED` as the fallback.

**Next:** continue resolving the pre-worker contracts (plugin capability metadata, cancellation-request API, shutdown drain/cancel modes, and runtime/task/Channel synchronization). Do not start worker creation until those contracts are implemented. Complete-VM DOM testing belongs after that scheduler milestone.

## Cancellation and shutdown contract checkpoint — archive 296

- [x] Separate cancellation request, worker-owned cancellation execution, and terminal `CANCELLED` publication.
- [x] Define safe-boundary behavior: a running VM is never cancelled or destroyed by the requesting thread; cancellation waits until `flat_vm_step()` returns at the next scheduler-visible safe boundary, normally the configured quantum boundary.
- [x] Define cancellation cleanup order across Channel rollback, outgoing wait-edge removal, Channel-reference release, VM cleanup, terminal publication and dependent wakeup.
- [x] Define DRAIN and CANCEL pool lifecycle modes, including CREATED-task behavior, new-submission rejection, unresolved WAITING detection, escalation and worker join.
- [x] Record a future worker acceptance matrix for request races, cancellation cleanup, DRAIN/CANCEL, join/destroy and sanitizer stress.
- [x] Preserve the current cooperative API unchanged; no asynchronous cancellation API or pthread implementation is introduced.

**Archive 296 result:** specification-only milestone. The existing `flvm[23]` and `flvm[29]` regressions cover cooperative cancellation and cleanup; selected regressions are rerun for this checkpoint. Future implementation must add real worker tests for the matrix in `09_CANCELLATION_SHUTDOWN_SPEC.md`.

**Next:** settle plugin capability metadata and enforcement (`THREAD_SAFE`, `SERIALIZED`, `UNSUPPORTED`, with unknown plugins denied by default), then design the scheduler mutex/condition-variable and task dispatch ownership protocol. Only after those contracts are implemented should pthread workers be created. Complete-VM Lexbor testing remains a post-scheduler gate.


## Native module capability metadata checkpoint — archive 297

- [x] Add explicit module capability enum: `THREAD_SAFE`, `SERIALIZED`, `UNSUPPORTED`, plus audit-only `PENDING_THREAD_SAFETY_VALIDATION`.
- [x] Add conservative built-in module defaults, including `dom` pending→serialized and `task`/`channel` unsupported.
- [x] Preserve the existing plugin initializer ABI and add the optional `flat_native_plugin_thread_capability()` symbol.
- [x] Mark bundled optional plugin shared objects explicitly; legacy external plugins without metadata default to `UNSUPPORTED`.
- [x] Add metadata freeze API and worker preflight that rejects unsupported modules and returns the effective execution policy.
- [x] Add `flvm[34]` for policy mapping, unknown-module rejection, capability freeze and concurrent metadata reads.
- [ ] Implement per-module mutex gates for `SERIALIZED` modules and integrate policy preflight into the worker dispatcher.
- [ ] Cover all Lexbor calls including `flat_dom_runtime_destroy()` with the DOM gate if DOM remains serialized.
- [ ] Run complete-VM concurrency tests for each built-in module/dependency before upgrading any pending or conditional classification.

**Archive 297 result:** capability metadata and worker-admission preflight are implemented, but there is still no worker pool. The preflight returns the effective policy; it does not yet serialize handler execution. The next milestone should implement the platform synchronization abstraction and module gates together with the worker dispatcher, rather than adding a spinlock around potentially blocking native handlers.

## Native synchronization and invocation gates — archive 298

- [x] Add `FlatMutex` platform abstraction: POSIX pthread mutex, Windows SRWLOCK, single-thread Emscripten no-op.
- [x] Add independent process-wide invocation gates per canonical native module.
- [x] Integrate effective `SERIALIZED`/`PENDING` policy at the common `flat_native_invoke()` boundary; keep `THREAD_SAFE` calls ungated.
- [x] Preserve cooperative compatibility: `UNSUPPORTED` modules are still callable in the current cooperative runtime and remain rejected by worker preflight.
- [x] Protect Lexbor-backed DOM destruction with the same gate as DOM native handlers.
- [x] Add `flvm[36]` for serialized exclusion, independent module gates, and DOM teardown protection.
- [ ] Add complete-VM concurrent DOM tests after scheduler/VM lifetime synchronization; keep DOM pending until then.
- [ ] Implement worker admission enforcement at the scheduler boundary, including preflight of all natives reachable by a task.
- [ ] Implement scheduler mutex/condition variables, ready queue, worker ownership and wake-pending protocol.
- [ ] Implement cancellation request/safe-point cleanup, DRAIN/CANCEL shutdown, worker join and runtime teardown.
- [ ] Validate Windows and Emscripten pthread builds and run sanitizer stress on complete VMs.

**Archive 298 result:** `SERIALIZED` is now enforced by per-module mutex gates at native invocation, and DOM teardown shares the DOM gate. This is not a worker pool and does not certify all `THREAD_SAFE` declarations. `UNSUPPORTED` remains a worker admission restriction, not a restriction on the existing cooperative runtime.

**Next:** implement the worker admission boundary and scheduler synchronization protocol. Do not create workers until task/VM ownership, cancellation and shutdown are implemented. The native invocation gates are now active, but the worker scheduler still has to perform worker-policy preflight before dispatch.

## Scheduler synchronization foundation checkpoint — archive 299

- [x] Extend `flat_sync.h` with mutex init/destroy, condition variable wait/signal/broadcast, and thread create/join abstractions for POSIX and Windows; provide explicit unsupported blocking/thread behavior for single-thread Emscripten.
- [x] Add a dynamically growing MPMC ready queue of task IDs with predicate-loop pop, close/broadcast, and drain-after-close behavior.
- [x] Add a dispatch ownership state machine with `queued`, `worker_owned`, `wake_pending`, and monotonically advancing dispatch tokens.
- [x] Verify wakeup while a worker owns the VM is deferred and materialized as exactly one requeue on release.
- [x] Verify stale worker tokens cannot release a later ownership claim.
- [x] Add `flvm[37]` to the existing `bin/run_tests` framework; no Python test runner.
- [ ] Integrate a real scheduler mutex into all task registry/state/wait-graph operations.
- [ ] Add dispatch ownership to runtime task records and connect queue publication/pop to task transitions.
- [ ] Implement worker-owned cancellation request, DRAIN/CANCEL, join, and runtime teardown.
- [ ] Convert Channel registry/Channel internal synchronization to the documented lock protocol.
- [ ] Add complete-VM multi-worker stress and sanitizer coverage.

**Conclusion:** archive 299 is a synchronization foundation, not a worker-pool implementation. The MPMC queue and dispatch state machine are independently tested, but they are not connected to `FlatRuntime`. The existing atomic runtime gate and cooperative task API remain unchanged; no general runtime thread safety is claimed.

**Next:** integrate the scheduler mutex and dispatch state into `FlatRuntime` in a separate, carefully audited change. `flat_vm_step()` and native handlers must remain outside the scheduler critical section. Do not start worker threads until task registry lifetime, wait graph, Channel references, cancellation, and shutdown are protected by one consistent protocol.


## FlatRuntime scheduler mutex integration checkpoint — archive 300

- [x] Replace the runtime's C11 atomic spin gate with `FlatMutex scheduler_mutex`.
- [x] Protect task ID reservation/publication, task registry/state/result operations, wait-graph transitions, runtime counters, and Channel registry/reference operations with the scheduler mutex.
- [x] Embed `FlatTaskDispatchState` in each runtime task record and claim/release one dispatch token per cooperative VM quantum.
- [x] Preserve the no-lock-across-VM rule: `flat_vm_step()` and native handlers execute outside the scheduler mutex.
- [x] Reject overlapping `flat_runtime_schedule()` calls; preserve one scheduler-driving host thread.
- [x] Reject synchronous cancellation/destruction while a task remains dispatch-owned, including the transient WAITING-before-step-return window.
- [x] Add `flvm[38]` for concurrent state queries, task starts during scheduling, overlapping scheduler rejection, and RUNNING-task cancellation rejection.
- [ ] Connect the MPMC ready queue to runtime enqueue/pop. Current selection remains a registry scan with dispatch queued-state tracking.
- [ ] Convert Channel's internal gate to the platform synchronization abstraction and complete lock-order audit.
- [ ] Implement request-only cancellation, DRAIN/CANCEL, worker condition wait, worker creation/join and worker shutdown.
- [ ] Integrate native worker preflight at dispatch admission and validate complete-VM multi-worker execution.

**Archive 300 result:** runtime task registry/state operations now use a real scheduler mutex, and dispatch ownership is integrated into the cooperative scheduler. The VM quantum remains outside the critical section. `flvm[38]` demonstrates concurrent task queries and starts while one scheduler runs, and confirms that overlapping scheduler calls and cancellation of a running task are rejected. No worker threads are started; the MPMC ready queue remains a standalone primitive. Channel still uses its existing per-Channel atomic gate.

**Archive 302:** Channel lock conversion and ready-queue integration are implemented; see `14_READY_QUEUE_CHANNEL_MUTEX_INTEGRATION.md`. Archive 303 subsequently implements request-only cancellation and scheduler-owned safe-point cleanup; see `15_CANCELLATION_REQUEST_RUNTIME.md`.

**Next:** implement DRAIN/CANCEL lifecycle and shutdown signaling, blocked-DRAIN reporting, escalation and join semantics. Only after those lifecycle contracts are implemented and tested should the pthread worker loop be added.


## Build target parity checkpoint — archive 301

- [x] Make `flvm[32]` tolerate missing/non-executable `pkg-config` by preferring valid libcurl pkg-config flags and falling back to `-lcurl`.
- [x] Reconcile `Makefile.wasm` with the current runtime/native source graph: locale helpers, runtime logger, Channel implementation, scheduler dispatch helpers, VM handles, native capability metadata, task/channel native handlers and shared HTTP boundary implementation.
- [x] Guard dynamic-plugin staging helpers with `FLAT_HAVE_DLOPEN`, avoiding unused-function warnings in static-plugin WASM builds.
- [x] Verify the HTTP test both with normal pkg-config and with a deliberately non-executable pkg-config shim.
- [x] Run the affected FLVM regressions and check the generated Emscripten source command.
- [ ] Run the actual `make fleditor-wasm V=1` link with the supported Emscripten SDK; `emcc` was unavailable in the archive validation environment.

**Conclusion:** archive 301 is a build integration/portability fix only. It does not alter runtime scheduling, Channel locking or worker-pool behavior. The native HTTP initialization test now has a reliable dynamic-libcurl fallback. The WASM source list now contains the implementations missing from the current runtime graph, but a real Emscripten link remains to be confirmed on a machine with `emcc`.

**Archive 302 follow-up:** the Channel mutex conversion, lock-order audit and ready-queue integration were completed independently of the pending Emscripten verification. The WASM build remains a separate portability check; it is not a prerequisite for documenting or validating this POSIX runtime work. See `14_READY_QUEUE_CHANNEL_MUTEX_INTEGRATION.md`.


## Request-only cancellation checkpoint — archive 303

- [x] Add `flat_runtime_task_cancel_request()` with explicit idempotent/terminal/invalid return semantics.
- [x] Keep the existing synchronous `flat_runtime_task_cancel()` API and behavior separate.
- [x] Make the request caller publish intent only under `scheduler_mutex`; it never calls `flat_vm_cancel()` or performs VM/Channel cleanup.
- [x] Claim pending requests for CREATED/RUNNABLE/WAITING tasks in the scheduler before normal dispatch.
- [x] Have the current dispatch owner observe RUNNING-task cancellation only after `flat_vm_step()` returns.
- [x] Preserve completion/failure if the VM reaches a terminal result before cancellation is observed.
- [x] Add `flvm[40]` for all four task states, idempotency, unknown/terminal IDs, and a blocked DOM native call proving safe-point ownership.
- [ ] Replace the cooperative registry scan with a dedicated worker cleanup queue when the worker loop is introduced.
- [ ] Implement DRAIN/CANCEL lifecycle, shutdown request, worker join and runtime teardown.

**Conclusion:** request-only cancellation is now implemented and tested without introducing worker threads. The current single scheduler is the cleanup executor. This provides the ownership/safe-point contract needed by worker mode, but it is not yet a worker pool.

**Next:** implement pool admission/lifecycle and shutdown signaling (`RUNNING`, `DRAINING`, `CANCELLING`, `STOPPED`), including blocked-DRAIN reporting and DRAIN-to-CANCEL escalation. Keep actual pthread worker creation gated on the lifecycle and join tests.


## Runtime shutdown lifecycle checkpoint — archive 304

- [x] Add runtime lifecycle states `RUNNING`, `DRAINING`, `CANCELLING`, `STOPPED`.
- [x] Add `flat_runtime_shutdown_request()` with idempotent DRAIN/CANCEL requests and DRAIN-to-CANCEL escalation; reject downgrade.
- [x] Close new task creation/start, native `task.spawn`, and direct runtime runs after shutdown begins.
- [x] Preserve CREATED tasks during DRAIN and allow explicit cleanup; include CREATED in CANCEL.
- [x] Report quiescent unresolved WAITING tasks as blocked DRAIN.
- [x] Add lifecycle/status query APIs and guard runtime destruction until requested shutdown reaches STOPPED.
- [x] Add `flvm[41]` covering DRAIN, blocked wait, escalation, no downgrade, CREATED retention and CANCEL cleanup.
- [ ] Implement real worker creation, worker condition-variable wait loop, wake/broadcast, partial-start rollback, worker exit and join.
- [ ] Add timed/interruptible join policy for a blocked DRAIN and prove no worker outlives runtime destruction.
- [ ] Run complete-VM concurrency and sanitizer stress under multiple workers before enabling worker mode.

**Archive 304 result:** cooperative shutdown state machine implemented; no pthread worker or join. The host drives `flat_runtime_schedule()` and inspects status. See `16_RUNTIME_SHUTDOWN_STATE_MACHINE.md`.

**Next:** implement the worker loop and join against the lifecycle contract, not before.

## Runtime worker-pool checkpoint — archive 305

- [x] Add runtime-owned worker creation and a fixed worker count (1–256).
- [x] Add an all-workers startup barrier and partial-start rollback with join of every created thread.
- [x] Run multiple worker scheduler turns concurrently while preserving single-task dispatch ownership and VM execution outside `scheduler_mutex`.
- [x] Add condition-variable idle wait and wake-epoch handling for queue publication, cancellation and shutdown.
- [x] Keep host-driven `flat_runtime_schedule()` exclusive and reject it while worker mode is active.
- [x] Require lifecycle STOPPED and zero active scheduler turns before worker join; require join before runtime destruction.
- [x] Integrate DRAIN/CANCEL exit and request-only cancellation wakeups.
- [x] Enforce native module worker capability at dispatch: reject UNSUPPORTED/UNKNOWN while any worker pool is active; retain SERIALIZED gates.
- [x] Promote built-in `task` and `channel` policies to SERIALIZED based on scheduler/Channel mutex protection.
- [x] Add `flvm[42]` for four-worker dispatch, 96 tasks, DRAIN/join, CANCEL/join and unsupported-native rejection.
- [x] Add multi-worker task.wait/Channel contention tests and verify transaction retry/wakeup ordering (`flvm[43]`, archive 306).
- [x] Add concurrent HTTP/DOM lifecycle tests in worker mode and validate all dependency initialization constraints (`flvm[45-46]`).
- [x] Add worker startup failure injection and test each partial thread-creation rollback point, including retry after a transient join failure (`flvm[42]`, archive 317).
- [ ] Validate Windows worker implementation and run supported Emscripten build (single-thread worker creation must fail cleanly).
- [ ] Add timed shutdown/join policy only if a real caller requirement emerges; do not detach workers or destroy runtime while any worker can access it.

**Archive 305 result:** a fixed pthread-backed worker pool is now implemented on the POSIX build and shares the runtime's ready queue, scheduler mutex, dispatch ownership and DRAIN/CANCEL lifecycle. `fl_vm_step()` remains outside the scheduler lock. Worker mode freezes native capability metadata and rejects unsupported native calls at invocation time. See `17_RUNTIME_WORKER_POOL.md`. This is the first real worker-pool implementation, not yet a complete-VM concurrency certification.


## Worker concurrency audit checkpoint — archive 306

- [x] Run the existing nested `task.spawn()` / `task.wait()` Channel-native program under the real four-worker pool.
- [x] Exercise two child tasks contending for the same Channel transaction while the parent waits for both.
- [x] Verify the transaction-owner priority/retry path completes without duplicate ownership or deadlock in this scenario.
- [x] Verify a task cannot suspend via `task.wait()` while it owns a Channel transaction.
- [x] Repeat worker startup, nested task execution, DRAIN, join and runtime teardown 20 times in one process.
- [x] Add wait-graph chain/cycle and cancellation regression, including a cancelled Channel waiter (`flvm[44]`, archive 307).
- [x] Add worker-mode HTTP/DOM lifecycle tests and plugin-load race tests (archives 309, 316).
- [x] Add deterministic worker-creation failure injection and validate every partial-start rollback point (archive 317).
- [ ] Validate Windows worker execution and the actual Emscripten build on supported toolchains.

**Archive 306 result:** no runtime synchronization semantics changed. `flvm[43]` reuses `027_channel_native/program.fl` and executes it with four runtime workers; two children contend on one Channel, the parent waits for both, and the transaction-held wait attempt fails as designed. Twenty independent runtime lifecycles pass in one process. This validates this concrete wait/Channel interaction only; it does not certify all wait-graph or native-module concurrency.

**Next:** extend stress coverage to multiple wait-graph chains and cancellation races before changing worker scheduling or lock behavior.


## Worker wait-graph/cancellation checkpoint — archive 307

- [x] Exercise a four-task wait chain under the real worker pool and reject a reverse edge that would create a cycle.
- [x] Cancel a middle waiter and verify its dependent wakes while the remaining chain completes.
- [x] Cancel a task parked on Channel contention while cancelling its transaction owner; verify task/Channel cleanup and post-join destruction.
- [x] Repeat the dedicated regression 100 times without TSAN and six times under GCC ThreadSanitizer.
- [ ] Continue with worker-mode HTTP/DOM lifecycle, plugin preparation/load races and deterministic partial-start failure injection.
- [ ] Validate Windows worker execution and actual Emscripten linking.

**Archive 307 result:** adds `flvm[44]` and `plans/19_WORKER_WAIT_GRAPH_CANCELLATION_AUDIT.md`; no runtime synchronization behavior changed. `flvm[40-44]` passed individually, `make all -j2` and `make docs` passed. The full 1179-case suite and ASan/UBSan were not run.


## Termux test-portability checkpoint — archive 308

- [x] Link `flat_locale.c` in the standalone AM-NUM regression test so `flat_strtod` and `flat_snprintf` resolve.
- [x] Change the SCHM trailing-garbage regression to insert a byte at the SCHM/JBMX boundary and adjust `jbmx_off`, instead of appending a byte to BPAR.
- [x] Make `flvm[38]` hold a known task in a DOM native call while testing RUNNING-state API rejection, avoiding dependence on observing a short VM instruction.
- [x] Increase `flvm[40]`'s blocked-native wait deadline for slower mobile devices.
- [ ] Re-run all four focused cases on Termux with verbose output; inspect any remaining `flvm[38]` / `[40]` failure diagnostics.
- [ ] Resume worker-mode HTTP/DOM lifecycle, plugin-load race, and partial worker-start failure testing.

**Archive 308 result:** all four focused cases pass on the available POSIX host; both FLVM test binaries pass five consecutive executions. `make all -j2` and `make docs` pass. No runtime implementation behavior changed. The supplied Termux run is not reproducible in this environment, so device confirmation remains required. See `20_TERMUX_TEST_PORTABILITY.md`.


## Worker native dependency audit — archive 309

- [x] Add real libcurl worker-mode integration with 32 requests and four runtime workers (`flvm[45]`).
- [x] Verify worker startup preflight initializes curl before task execution and each request uses an independent easy handle.
- [x] Add DOM worker-mode document isolation and cancellation/teardown test (`flvm[46]`).
- [x] Retain DOM's PENDING declaration and SERIALIZED effective policy.
- [x] Audit optional native plugin load/preflight races and runtime-wide worker-mode guard transitions (archive 316).
- [x] Add deterministic failure injection for worker creation and verify partial startup rollback (archive 317).
- [ ] Complete platform/sanitizer matrix: Windows, actual Emscripten, TSAN final test shape, ASan/UBSan.

**Archive 309:** HTTP and DOM lifecycle tests pass on the available POSIX host; see `21_WORKER_HTTP_DOM_LIFECYCLE_AUDIT.md`.


## Shell-capture diagnostics checkpoint — archive 311

- [x] Redirect stderr for the complete POSIX compound command, including earlier failing stages of `&&` chains.
- [x] Drain the full command output and preserve the last 4095 bytes instead of retaining only the prefix.
- [x] Apply equivalent rolling-tail retention to the Windows process capture.
- [x] Add `port[2]` for compound-command stderr and `port[3]` for output larger than the capture buffer.
- [ ] Re-run the grouped worker test selection on Termux and use the now-complete tail to identify the seven reported failures.

**Archive 311 result:** `port[2-3]` pass; each `flvm[34-46]` case passed individually on the available POSIX host. A grouped `flvm[34-46]` invocation did not finish within the command limit, so grouped completion is not claimed. No runtime scheduling or worker semantics changed.


## Termux worker-test diagnosis — archive 312

- [x] Add stage markers to `flvm[38]`, `[40]`, `[42]`, `[43]`, `[44]`, `[45]`, and `[46]` so empty-output failures reveal the last completed command stage.
- [ ] Re-run the focused tests on Termux and capture the last `[stage:...]` marker for every failure.
- [ ] Fix only the identified failing compile/harness/runtime stage; do not change scheduler synchronization or timeouts without evidence.


## Signal-aware shell status checkpoint — archive 313

The Termux rerun of archive 312 reached `[stage:run-harness]` in all seven failing tests. The POSIX capture layer previously flattened `pclose()` signal termination into exit code 1, obscuring whether a harness returned normally or died from a signal. Archive 313 preserves signal termination as a negative signal number and formats it explicitly in `run_tests`. This is diagnostic-only; no runtime or worker semantics changed. The next Termux run must establish whether the harnesses exit normally or are terminated by a signal.


## VM stack and worker cleanup ownership checkpoint — archives 314–315

- [x] Confirm the Termux worker-thread crash with ASan: `vm_state_execute()` overflowed the secondary thread stack due to fixed-size `OP_GUARD` scratch arrays.
- [x] Move `OP_GUARD` scratch arrays into heap-backed `VmState` storage sized by `var_slots_max`; release them in `vm_teardown()` (archive 314).
- [x] User validated `flvm[38-46]` and `port[2-5]` on Termux in one invocation: 13/13 selected tests PASS; 1185 tests were discovered, not all executed.
- [x] Audit worker-mode VM destruction and cleanup ownership; keep VM teardown outside `scheduler_mutex` and retain the task record until teardown completes.
- [x] Reject a cancellation request for a nonterminal task whose cleanup is already owned by synchronous destruction/cancellation, while preserving idempotent scheduler-owned cancellation (archive 315).
- [x] Extend `flvm[46]` with a worker-mode destroy-vs-cancel-request race blocked on the serialized DOM gate.
- [ ] Re-run `flvm[46]` on Termux and rerun the `flvm[38-46]` / `port[2-5]` selection after archive 315.
- [x] Complete the plugin preflight/load race audit (archive 316) and deterministic partial worker-start failure injection (archive 317).
- [ ] Validate Windows worker execution and actual Emscripten linking.

**Archive 315 result:** `make all -j2`, `make docs`, `flvm[46]`, and 10 consecutive direct executions of the final worker DOM harness passed on the available Linux host. A combined 13-test validation invocation exceeded the command time limit after `flvm[40]`; it is not recorded as a complete pass. Termux validation of archive 315 remains pending.


## Deterministic worker startup failure injection — archive 317

- [x] Add test-only wrappers around runtime worker `flat_thread_create()` and `flat_thread_join()`; injection hooks are compiled only into the `flvm[42]` harness with `FLAT_RUNTIME_TESTING` and are not part of the public API or normal build.
- [x] Fail worker creation at every index in a four-worker startup (first, intermediate positions, and final position). Verify startup returns failure and runtime destruction succeeds after rollback.
- [x] Inject a transient join failure during partial-start rollback. Verify the runtime retains worker state/handles, rejects destruction, and completes cleanup through a subsequent `flat_runtime_workers_join()` retry.
- [x] Inject a transient join failure after successful startup and shutdown. Verify already-joined workers are not joined twice, the failed handle is retained, and a retry completes cleanup.
- [x] Keep production worker synchronization and lifecycle behavior unchanged; the change is a narrow test seam and regression coverage.

**Archive 317 validation (local POSIX host):** `make all -j2` and `make docs` passed (50 sections). `flvm[13]`, `flvm[34-36]`, and `flvm[38-46]` passed in completed individual runs; `flvm[42]` passed three consecutive additional runs with the injected failure matrix. One grouped command exceeded the execution limit after `flvm[38]`, and a later loop command timed out after `flvm[41]` before reaching `flvm[42]`; these incomplete grouped commands are not counted as suite passes. `flvm[43-46]` subsequently passed individually. The full 1185-test discovery set, Windows worker execution, and actual Emscripten linking remain unverified.


## Multi-runtime worker-mode ownership — archive 318

- [x] Audit the process-wide `g_worker_mode_users` atomic reference count against independently owned runtime worker pools.
- [x] Verify partial startup rollback in a second runtime releases only its own worker-mode reference while the first runtime remains active.
- [x] Verify joining one of two active runtimes does not disable process-wide worker-mode restrictions while the other pool remains active.
- [x] Verify concurrent joins on two distinct stopped runtimes return the process-wide worker-mode count to inactive.
- [x] Keep production locking and lifecycle behavior unchanged; add regression coverage for the existing atomic ownership contract.
- [x] Review `FlatThread` join-error semantics across POSIX and Windows, and explicitly document same-runtime concurrent join/destroy limitations (archive 319).

**Archive 318 validation (local POSIX host):** `make all -j2` and `flvm[42]` passed with the multi-runtime regressions. The repeat batch before the concurrent-join extension completed four repetitions and was interrupted during the fifth by the execution limit; it is not counted as a complete 10-run stress pass. The expanded final test has one completed PASS. Full 1185-test suite, sanitizers on the final expanded harness, Windows worker execution, and actual Emscripten linking remain unverified. See `30_MULTI_RUNTIME_WORKER_MODE_OWNERSHIP.md`.


## FlatThread join ownership — archive 319

- [x] Review POSIX `pthread_join()` ownership: preserve `FlatThread.valid` on failure and clear it only after success; do not treat every native error as guaranteed retryable.
- [x] Review Windows wait/close as separate operations; report `CloseHandle()` failure and retain the handle value unless close succeeds.
- [x] Add a per-runtime `workers_joining` claim under `scheduler_mutex`; reject concurrent `flat_runtime_workers_join()` callers for the same runtime.
- [x] Release the join claim after an error while retaining unjoined handles, allowing a later retry.
- [x] Prevent runtime destruction while the join operation owns the worker handles.
- [x] Add a four-caller same-runtime concurrent-join regression to `flvm[42]`; exactly one caller may complete the join.
- [x] Document that concurrent join on distinct runtimes is supported by the existing ownership model, while concurrent destroy/API entry remains the host's responsibility.
- [ ] Validate Windows worker execution and actual Emscripten linking on supported toolchains.

**Archive 319 validation (local POSIX host):** `make all -j2` passed; `flvm[42]` passed, including 20 consecutive executions of the dedicated harness. The full 1185-test suite was not run. Windows worker execution and actual Emscripten linking remain unverified. See `31_FLAT_THREAD_JOIN_OWNERSHIP.md`.

**Next:** continue the v2.0 roadmap's broader worker stress matrix (multiple independent wait graphs, simultaneous cancellation and cleanup interleavings), while keeping Windows/Emscripten toolchain validation as a separate open platform gate. Do not move to UI Runtime v2.5 until the v2.0 worker acceptance criteria and platform boundaries are explicitly reviewed.


## Multi-graph cancellation stress — archive 320

- [x] Add `flvm[47]`: eight independent four-task wait graphs in one runtime.
- [x] Release eight host-thread cancellation requests simultaneously through a test barrier.
- [x] Start a four-worker pool, DRAIN, join, verify A/B/C/D terminal states, and destroy every task/runtime.
- [x] Repeat five rounds per harness execution (40 graphs / 160 task instances per invocation).
- [ ] Continue the broader stress matrix: varied/random graph shapes, mixed DRAIN-to-CANCEL escalation under load, and multi-writer Channel transactions with cancellation.

**Archive 320 validation (local POSIX host):** `make all -j2`, `make docs`, `flvm[39]`, `flvm[40-47]` completed as PASS in individual runs; `flvm[47]` also passed five consecutive completed invocations, ASan, and UBSan. A grouped `flvm[42-47]` command was stopped by the execution limit after reporting through `flvm[45]`; tests 46 and 47 passed separately. The full 1186-test suite, Windows worker execution, and actual Emscripten linking remain unverified. See `32_WORKER_MULTI_GRAPH_CANCELLATION_STRESS.md`.

**Next:** continue the v2.0 stress matrix with mixed DRAIN/CANCEL escalation and Channel transaction contention under concurrent cancellation. Keep Windows and Emscripten toolchain validation as explicit platform gates. Do not begin UI Runtime v2.5 until v2.0 acceptance criteria and platform boundaries are reviewed.


## Worker Channel / shutdown stress checkpoint — archive 321

- [x] Exercise eight transaction writers on each of four independent Channels under four real workers; assert the final read-modify-write counter on every Channel (`flvm[48]`).
- [x] Hold four Channels with CREATED owner tasks and park one started waiter on each Channel transaction.
- [x] Verify worker-mode DRAIN reports BLOCKED while the Channel waiters remain unresolved.
- [x] Publish four request-only cancellation operations concurrently from host threads; verify DRAIN completes and leaves CREATED owners untouched.
- [x] Join the stopped worker pool, escalate to CANCEL, and complete CREATED-task cleanup through the cooperative scheduler.
- [x] Verify all task terminal states and release every Channel reference before destruction.
- [ ] Continue broader/randomized wait-graph and mixed dependency stress; validate Windows worker execution and actual Emscripten linking.

**Archive 321 result:** `flvm[48]` passes on the local POSIX host, including five consecutive direct harness executions and ASan, UBSan and TSAN runs. No production scheduler or Channel change was required. The test explicitly preserves the DRAIN contract: CREATED tasks are outside the DRAIN set; after DRAIN reaches STOPPED, join workers before post-DRAIN CANCEL cleanup is driven cooperatively. See `33_WORKER_CHANNEL_SHUTDOWN_STRESS.md`. The full 1187-test suite, Windows and Emscripten remain unverified.

## Converging DAG cancellation stress — archive 322

- [x] Add `flvm[49]` with converging wait graphs (multiple waiters sharing one target), deterministic topology variants, concurrent cancellation, DRAIN, join and cleanup.
- [ ] Expand deterministic graph families further, including deeper fan-in/fan-out and varied cancellation timing during execution.
- [ ] Validate Windows worker execution and actual Emscripten linking; run complete-VM sanitizer matrix on supported platforms.

**Archive 322:** `flvm[49]` exercises eight six-node DAGs over five rounds, with eight simultaneous cancellation requests per round. A waiter has only one outgoing wait edge under the existing API. See `34_WORKER_DAG_CANCELLATION_STRESS.md`.


## Staged wait-graph cancellation — archive 323

- [x] Add `flvm[50]`: four independent 10-task DAGs with seven leaves split across two intermediate nodes converging on a sink; fan-out varies deterministically by round and graph.
- [x] Start eight workers and wait until each sink is observably RUNNING before cancellation begins.
- [x] Exercise cancellation of RUNNING sinks, RUNNING intermediate nodes, a RUNNING leaf, and a WAITING intermediate node; leave one graph untouched as a normal DRAIN control.
- [x] Assert all 40 terminal task states, join workers, destroy all tasks and destroy the runtime; repeat three rounds per invocation.
- [ ] Continue longer-duration randomized/seeded stress and mixed shutdown escalation; validate Windows worker execution and actual Emscripten linking.

**Archive 323 validation (local POSIX host):** `make all -j2` and `make docs` passed (50 sections). `flvm[47–50]` passed individually. The `flvm[50]` harness passed six consecutive direct executions and passed ASan, UBSan and TSAN. No production scheduler change was required. The full 1189-test suite, Windows workers and actual Emscripten linking remain unverified. See `35_WORKER_DAG_STAGED_CANCELLATION.md`.


## Seeded worker DAG stress — archive 324

- [x] Add `flvm[51]` with 16-task DAGs generated by a fixed xorshift32 seed; edges point only toward higher node indices, guaranteeing acyclicity while varying fan-in and disconnected components.
- [x] Select four distinct cancellation targets from the same deterministic PRNG stream and publish their request-only cancellation concurrently from host threads.
- [x] Run six seeds twice each and compare topology/cancellation signatures to verify reproducibility.
- [x] Start six real workers, DRAIN, assert exact terminal states for all 16 tasks, join workers and destroy all task/runtime resources.
- [ ] Add broader mixed DRAIN-to-CANCEL escalation under concurrent randomized load and longer-duration soak.
- [ ] Validate Windows worker execution and actual Emscripten linking.

**Archive 324 validation (local POSIX host):** `flvm[51]` passed via `bin/run_tests`, three additional direct harness repetitions, ASan and UBSan. The dedicated TSAN harness binary passed directly; the `bin/run_tests` TSAN invocation exceeded the command limit and is not counted. `flvm[47-49]` passed in a grouped invocation before timeout; `[50]` and `[51]` passed separately. `make all -j2` and `make docs` passed (50 sections). User-reported Termux validation for archive 320: all 47 `flvm` tests passed (1186 discovered overall, 7:40.819); this does not cover later cases 48–51. Windows, actual Emscripten linking and the full default suite remain unverified. See `36_WORKER_SEEDED_DAG_STRESS.md`.


## Worker failure isolation and lifecycle soak — archive 326

- [x] Add `flvm[53]`: 100 fresh runtime cycles per invocation, each with eight tasks and four real workers; two tasks intentionally fail while six complete normally.
- [x] Assert FAILED/COMPLETED task states and `FlatVmResult` values independently; require DRAIN completion, worker join, explicit task destruction and runtime destruction every round.
- [x] Validate `flvm[53]` under ASan, UBSan and TSAN on the local POSIX host.
- [ ] Continue broader randomized shutdown/failure interleavings and longer soak beyond 100 cycles.
- [ ] Validate Windows worker execution and actual Emscripten linking; the single-thread Emscripten worker-startup rejection remains a platform gate.

**Archive 326 validation (local POSIX host):** `make all -j2` and `make docs` passed (50 sections). `flvm[53]` passed once plus four additional completed runner invocations; a fifth repeat attempt was interrupted by the outer command time limit and is not counted. ASan, UBSan and TSAN runs of `flvm[53]` passed. `flvm[51]` and `[52]` passed separately. A grouped `[47-53]` run passed `[47-50]` before timing out; remaining tests were run individually. The full 1192-test suite, Windows worker execution, actual Emscripten linking and Termux validation of archives 321–326 remain open. See `plans/38_WORKER_FAILURE_ISOLATION_SOAK.md`.


## Emscripten single-thread worker gate — archive 327

- [x] Audit `include/flat_sync.h`'s `__EMSCRIPTEN__ && !__EMSCRIPTEN_PTHREADS__` branch: no-op mutex operations, rejected blocking condition wait, rejected thread create/join, and `flat_threads_supported() == 0`.
- [x] Confirm `flat_runtime_workers_start()` rejects unsupported thread mode before mutating runtime state or allocating worker resources.
- [x] Add `flvm[54]`, compiling the complete runtime source set with the host compiler and `-D__EMSCRIPTEN__` (without `-pthread`) to exercise the single-thread branch and verify clean runtime destruction after rejected worker startup.
- [ ] Build `Makefile.wasm` with a real Emscripten SDK and run generated WASM smoke tests.
- [ ] Validate Windows worker execution on a Windows host.

**Archive 327 validation:** `test-emscripten-no-pthreads` and `flvm[54]` passed using the host compiler to select the Emscripten single-thread preprocessor branch. This is a branch/contract simulation, not a WASM build. `emcc`/`emcmake` and a Windows toolchain are unavailable in the current environment, so the actual Emscripten and Windows gates remain open. No production runtime code changed. See `39_EMSCRIPTEN_SINGLE_THREAD_WORKER_GATE.md`.

## Windows FlatSync / FlatThread audit — archive 328

- [x] Audit the Windows `SRWLOCK` / `CONDITION_VARIABLE` wrappers and ready-queue predicate loops at source level.
- [x] Make failed `flat_thread_create()` leave a safely non-joinable wrapper on both Windows and POSIX, including a null function argument.
- [x] Remove the Windows-only trampoline write into `FlatThread`; the trampoline now owns only its `{fn,arg}` start record and discards the function result, matching POSIX join semantics.
- [x] Add `flvm[55]` for failed-create state, condition-variable wakeup, successful join and repeated-join rejection.
- [ ] Compile and execute `flvm[37]`, `[42]` and `[55]` with a real Windows SDK/toolchain; verify the configured `_WIN32_WINNT` is at least `0x0600`.
- [ ] Build and execute the real Emscripten target with `emcc`/`emcmake`.

**Archive 328 validation (local POSIX host):** `make all -j2`, `make docs` (50 sections), `flvm[37]`, `flvm[42]`, and `flvm[55]` passed individually. The dedicated `flvm[55]` harness also passed under ASan and UBSan. Windows compilation/execution was not possible because no Windows SDK, MinGW/MSVC toolchain or runtime is available. Emscripten remains unverified because `emcc`/`emcmake` are unavailable. The Windows source audit identifies the required Vista-or-newer API target but does not silently set a process-wide `_WIN32_WINNT` value. See `40_WINDOWS_FLAT_SYNC_AUDIT.md`.


## Test runner expanded-command capacity — archive 329

- [x] Increase the `case.toml` command field and line parser capacity so placeholder-expanded commands can exceed 1 KiB.
- [x] Make placeholder expansion fail explicitly on overflow instead of silently executing a truncated command.
- [x] Increase POSIX/Windows shell-capture command buffers to accommodate the expanded command and wrapper prefixes.
- [x] Use the existing `flvm[48]` case as a regression for long `{root}` / `{tmp}` placeholder expansion on Termux.
- [x] Validate `flvm[48]` through `bin/run_tests` on Termux.
- [ ] Run the broader FLVM/default suite on the current snapshot.

**Archive 329 scope:** fixes a test-runner command-buffer truncation that caused long `case.toml` `cmd` values to be silently truncated after placeholder expansion. No VM, Channel, or worker-pool behavior is changed.

**Archive 329 validation (local POSIX host):** `make all -j2`, `make docs`, `port` (5/5), and isolated `flvm[48]` runs passed. The full `flvm` suite exceeded the available command window after reporting 17/55 cases, so it is not counted as complete. One multi-repeat shell invocation also timed out while starting its third repetition; a subsequent isolated `flvm[48]` run passed.

**Archive 329 follow-up validation (archive 330):** the user confirmed `bin/run_tests 'flvm[48]' -v -e` on Termux: PASS (1/1, 18,092 ms). On the local POSIX host, the clean-source `flvm[48]` harness passed three consecutive AddressSanitizer runs with leak detection enabled and three consecutive UndefinedBehaviorSanitizer runs. `flvm[54]` also passed once under AddressSanitizer. See `41_VM_WORKER_ASAN_REGRESSION.md`. The complete `flvm` suite, Windows worker execution and actual Emscripten linking remain unverified.


## Focused FLVM regression sweep — archive 331

- [x] Run all cases `flvm[1-55]` through completed grouped or isolated invocations on the local POSIX host; each case passed at least once.
- [x] Repeat the `flvm[43]` direct harness 10 times (200 runtime lifecycle rounds); all passed.
- [x] Repeat the `flvm[43]` runner case three times in isolated invocations; all passed.
- [ ] Complete one full `flvm[1-55]` invocation, or identify and resolve why some larger grouped selections exceed the outer execution limit even though the affected cases pass in isolation.

**Archive 331 result:** all 55 focused FLVM cases have passing evidence on the archive-329 source snapshot, but not as one completed suite invocation. Several larger grouped commands exceeded the outer execution limit after partial progress; completed cases passed, and isolated reruns passed. The full-suite/grouped-run gate remains open. Detailed results are in `41_VM_WORKER_ASAN_REGRESSION.md`.


## Worker DAG test determinism and bytecode cleanup — archive 332

- [x] Make `flvm[50]`'s RUNNING-state observation robust by using a longer controlled workload and a worker quantum appropriate to this test's purpose.
- [x] Emit the per-graph task-state matrix when DRAIN does not complete.
- [x] Release the loaded `FlatBytecode` on both success and failure paths in the `flvm[50]` harness.
- [x] Verify `flvm[50]` with direct repeated runs, AddressSanitizer with leak detection, and UndefinedBehaviorSanitizer.
- [ ] Complete a full `flvm[1-55]` run and the Windows/Emscripten platform gates.

**Archive 332 result:** the previous 5,000-iteration workload could complete between 1 ms state polls, making a transient RUNNING-state assertion flaky. The test now uses 50,000 iterations and worker quantum 16; its direct harness passed eight consecutive repetitions, and the runner case passed. The first ASan pass exposed a 5,909-byte leak because the test harness did not free its loaded `FlatBytecode`; adding `flat_fb_free()` to both exit paths removed the leak, and final ASan/UBSan runs passed. No production runtime code changed. See `42_WORKER_DAG_TEST_DETERMINISM.md`.

## Worker harness bytecode ownership audit — archive 333

- [x] Audit worker DAG/shutdown harnesses for `FlatBytecode` ownership after `flat_fb_load_file()`.
- [x] Add `flat_fb_free()` on success and every post-load failure path in `flvm[47]`, `[49]`, `[51]`, and `[52]`.
- [x] Add `flvm[050]` lifecycle-stage diagnostics around worker join, task destruction, and runtime destruction.
- [x] Run the focused worker regression group `flvm[47-55]` as one invocation.
- [x] Verify the seeded DAG harness `flvm[51]` under ASan with leak detection across five consecutive repetitions.
- [ ] Complete a full `flvm[1-55]` invocation and the Windows/Emscripten platform gates.

**Archive 333 result:** the first ASan run of `flvm[51]` exposed a 5,908-byte test-harness bytecode ownership leak. The harnesses `flvm[47]`, `[49]`, `[51]`, and `[52]` now free their loaded bytecode on success and post-load failure paths. `flvm[51]` passed five consecutive ASan/leak-detection repetitions. `flvm[47-55]` passed as one grouped runner invocation (9/9, 1:14.220), followed by `flvm[42-55]` (14/14, 1:55.645). `make all -j2` and `make docs` also passed. A source scan found no FLVM C harness loading bytecode without a corresponding free call. No production runtime code changed. See `plans/43_WORKER_HARNESS_BYTECODE_OWNERSHIP.md`.


## Worker lifecycle sanitizer sweep — archive 334

- [x] Run every focused `flvm[1-55]` case on the current snapshot in completed groups: 20/20, 20/20, and 15/15.
- [x] Run `flvm[47]`, `[49]`, `[50]`, `[52]`, and `[53]` harnesses under combined ASan/UBSan with leak detection and halt-on-error.
- [x] Run the mixed DRAIN/CANCEL soak `flvm[52]` under GCC ThreadSanitizer.
- [ ] Complete one single-process `flvm[1-55]` invocation; the three completed groups establish per-case PASS, not single-invocation completion.
- [ ] Validate Windows worker execution and a real Emscripten SDK build/link/run.

**Archive 334 result:** all 55 focused FLVM cases passed in three completed groups. ASan/UBSan passed for the multi-graph, converging-DAG, staged-DAG, mixed-shutdown, and failure-isolation harnesses; the mixed-shutdown harness also passed TSAN. The first `flvm[50]` sanitizer command hit the outer execution limit during its third round, while a subsequent bounded rerun completed all three rounds and cleanup without a sanitizer report. No source/runtime changes were required; this is a validation/documentation checkpoint. See `plans/44_WORKER_LIFECYCLE_SANITIZER_SWEEP.md`.


## WASM API and filesystem/CSV lifecycle tests — archive 335

- [x] Add a Node.js headless test for the exported WASM C API: compile, run, invalid source/bytecode, logical FAIL vs API failure, entry compilation, disassembly, termination/panic accessors, and 40 repeated runs in one module instance.
- [x] Correct the CSV reader example so directory creation is explicit through FlatL `fs.mkdir()`; stage the CSV fixture at project root, copy it with `file`, then pass the nested file to `csv.reader()`. The CSV module does not create directories.
- [x] Add `fleditor-wasm-api-smoke` and aggregate `fleditor-wasm-test` Make targets; all tests are `.mjs` executed by Node.js.
- [x] Run all nine headless WASM smoke tests against the archived Emscripten module with Node.js.
- [ ] Rebuild the module and rerun `make fleditor-wasm-test` on the user's Linux/Termux Emscripten SDK, because `emcc` is not installed in this execution environment.
- [ ] Extend API lifecycle checks for run-limit/error recovery and observable memory-growth behavior without treating linear-memory high-water size as proof of a leak.

**Archive 335 result:** all nine Node.js WASM tests passed against the archived `flatl.js`/`flatl.wasm`. The CSV reader now creates its nested project path using FlatL `fs.mkdir()` before writing and reading the CSV; the Node.js harness only stages root-level fixtures in MEMFS. `make fleditor-wasm-test` was added but could not be invoked end-to-end here because the recursive Emscripten build attempted to call unavailable `emcc`. No production C/VM code changed. See `plans/45_WASM_API_LIFECYCLE_TESTING.md`.


## WASM Node.js test runner correction — archive 336

- [x] Separate the Node executable used by headless WASM tests (`WASM_NODE`, default `node`) from the FLEditor server/UI `NODE` selection policy.
- [x] Change all `fleditor-wasm-*` smoke recipes to invoke `.mjs` files through `$(WASM_NODE)` rather than attempting direct execution when `NODE` is empty.
- [x] Preserve non-executable source-file permissions; no chmod workaround is needed.
- [x] Validate all nine Node.js WASM tests, the focused API target, and the CSV target against the archived WASM module.
- [ ] User reruns `make fleditor-wasm-test` and `make fleditor-wasm-api-smoke` after their successful `make rebuild`.

**Archive 336 result:** the user's native `make test` passed 1,151/1,151 and `make rebuild` succeeded, including `fleditor-wasm`. Their two WASM test targets then failed before Node started because the shared `NODE` Make variable was empty under the Linux-Node detection policy. The WASM targets now use independent `WASM_NODE ?= node`, leaving FLEditor server behavior unchanged. All nine WASM scripts passed through Node.js v22.16.0 in the available environment; the complete Make targets also passed while reusing the archived WASM artifacts (`make -o fleditor-wasm ...`). A fresh Emscripten rebuild was not possible in this environment because `emcc` is unavailable. See `plans/46_WASM_NODE_RUNNER_FIX.md`.


## WASM run-limit, panic recovery and memory validation — archive 337

- [x] Add Node.js API tests for explicit `vm.jump_limit` and `vm.step_limit` failures, including `FLAT_TERM_PANIC` and diagnostic accessors.
- [x] Verify successful execution after both limit panics and after an unhandled native panic; ensure `last_panic` is cleared by the next successful run.
- [x] Force a bounded Emscripten linear-memory growth and verify execution continues after growth.
- [x] Run 100 repeated compile/run cycles and assert the linear-memory high-water mark remains bounded after warm-up.
- [ ] Reduce the WASM compile-time memory working set without introducing platform-specific FlatL/FGIR limits.
- [ ] Rebuild and run the expanded ten-test WASM suite on the user's Emscripten SDK.

**Archive 337 result:** all ten headless WASM tests pass in Node.js against the archived Emscripten artifacts. Run-limit, native panic, recovery and explicit memory-growth assertions pass. The first compile expands WASM linear memory from 25,559,040 to 845,021,184 bytes; the high-water mark remains stable over 100 subsequent runs. Source inspection points to the fixed-size `FgirProgram` layout (native `sizeof(FgirProgram)=418,121,328`) as the dominant working-set contributor. This is not classified as a per-run leak, but it is a significant browser/mobile memory issue and becomes the next optimization target. No production C/VM code changed. See `47_WASM_LIMITS_RECOVERY_MEMORY.md`.


## FGIR linker peak-memory reduction — archive 338

- [x] Reconcile the user's rebuilt Termux WASM measurement: 1,263,337,472-byte compile high-water, unchanged after 100 later runs.
- [x] Trace the no-import linker peak to the output `FgirProgram`, an unconditional module scratch `FgirProgram`, and a separate entry `FgirProgram`.
- [x] Parse no-import entries directly into the output program; reuse one scratch program as the entry program when imports exist.
- [x] Add a Node.js WASM regression ceiling of 600 MiB for a simple no-import compile, without changing language limits.
- [x] Build native targets and run focused link/import suites.
- [ ] Rebuild WASM with Emscripten and confirm the new no-import high-water ceiling on Linux/Termux.
- [ ] Run the complete default test suite after the linker allocation change.
- [ ] Design a capacity-managed FGIR block/instruction representation; one `FgirProgram` still has a large fixed-capacity footprint.

**Archive 338 scope:** reduces the no-import link path from three simultaneously live fixed-size `FgirProgram` objects to one, and the import path from three to two by reusing the module scratch object for the entry. Native build passed and the focused `fmod`, `native`, and `native_use` suites passed 46/46. Fresh WASM validation remains pending because `emcc` is unavailable in this environment. See `plans/48_FGIR_LINKER_PEAK_MEMORY.md`.


## FGIR dynamic storage checkpoint — archive 339

- [x] Replace embedded `FgirProgram.blocks[1024]` with an owned, capacity-managed block array.
- [x] Replace embedded `FgirBlock.instr[128]` with an owned, capacity-managed instruction array.
- [x] Preserve the existing 1,024-block and 128-instruction language limits on every platform.
- [x] Add explicit FGIR free/reserve/deep-clone helpers and update parser, linker merge, lowering, CLI and WASM ownership paths.
- [x] Add a native regression that forces multiple block/instruction capacity expansions.
- [x] Tighten the Node.js WASM memory ceiling to 128 MiB and add a generated dynamic-FGIR workload.
- [x] Complete the full default `make test` run (1,152 selected tests; 1,152/1,152 PASS).
- [ ] Rebuild and run the expanded WASM suite with the user's Emscripten SDK; confirm the new memory ceiling.

**Archive 339 implementation checkpoint:** `sizeof(FgirProgram)` is reduced from 418,121,328 to 2,737,792 bytes and `sizeof(FgirBlock)` from 405,648 to 672 bytes on the native ABI. The focused compiler/linker suites pass 113/113; the new 65-block/17-instruction regression passes and passed three ASan runs with leak detection. The complete default test run passes 1,152/1,152 in 4:30.190. A fresh Emscripten rebuild remains pending. See `plans/49_FGIR_DYNAMIC_STORAGE.md`.


## FGIR parser and fixed-metadata audit — archive 340

- [x] Confirm the current compiler has no separate AST: `flat_parse_unit()` parses source lines directly into FGIR; `LocalBlockFrame` is parser nesting state, not an AST.
- [x] Measure the remaining fixed FGIR metadata on the native ABI: `FgirProgram=2,737,792 B`, with `schemas[64]=1,577,216 B` and `functions[256]=1,008,640 B`.
- [x] Audit schema parser/linker/catalog consumers and schema value-copy semantics.
- [x] Audit function metadata indexed readers, parser/linker append paths, and broader copy-site surface.
- [x] Select one isolated next change: dynamic outer `FgirProgram.schemas` array while keeping each `FgirSchemaDecl` self-contained.
- [ ] Implement archive 341 schema-array reserve/free lifecycle, focused parser/linker tests, and ASan/UBSan. Do not combine with dynamic schema fields or function storage.

**Archive 340 result:** source-only audit; no C implementation or language behavior changed. The compiler has no AST layer to optimize. The schema array is the recommended first target because it is the largest remaining unconditional FGIR reservation (about 1.58 MB) and has a substantially smaller call-site/ownership surface than the function table. See `plans/50_FGIR_PARSER_STORAGE_AUDIT.md`.


## FGIR dynamic schema storage — archive 341

- [x] Replace embedded `FgirProgram.schemas[64]` with an owned, capacity-managed schema declaration array.
- [x] Keep `FgirSchemaDecl` self-contained; field keys/specifications remain inline and schema value copies remain safe.
- [x] Preserve `FGIR_SCHEMA_MAX == 64` and existing parser/linker limit diagnostics.
- [x] Add checked geometric reserve, cleanup in `flat_fgir_free()`, and allocation before parser/linker append.
- [x] Add native tests for reserve/free/repeated lifecycle, 64-schema success, 65-schema rejection, and schema preservation through entry link merge.
- [x] Run focused schema/type, linker, fmod and native-use tests; run parser/linker boundary cases under ASan/UBSan.
- [x] Run a complete `make test` to completion after the schema-storage change; the user confirmed 1,156/1,156 PASS in 11:07.401.
- [x] Rebuild WASM with the user's Emscripten SDK and rerun `make fleditor-wasm-api-smoke` and `make fleditor-wasm-test`; the user confirmed both pass and compile high-water remains 42,729,472 bytes after 100 runs.

**Archive 341 result:** `FgirProgram.schemas` is now an owned pointer with `schema_count` and `schema_capacity`. Growth is geometric and capped at 64; `flat_fgir_free()` releases the allocation. The native `sizeof(FgirProgram)` decreases from 2,737,792 bytes to 1,160,584 bytes (a reduction of 1,577,208 bytes) while `sizeof(FgirSchemaDecl)` remains 24,644 bytes. Focused tests passed: `flc[1-9]` (8 selected; IDs 1–4 and 6–9), `types` (251/251), `fmod` (18/18), and `native_use` (6/6). The new reserve/free harness passed ASan/UBSan with leak detection; sanitized `flc` builds passed the 64-schema success case, 65-schema rejection case, and schema-through-link case without sanitizer reports. `make all -j2` and `make docs` passed (50 sections). The initial local full-suite attempt stopped at 280/1,156 due to the outer execution limit; the user subsequently completed `make test` on Termux with 1,156/1,156 PASS in 11:07.401. The user also rebuilt WASM and confirmed both WASM targets pass; compile high-water remained 42,729,472 bytes after 100 runs. See `plans/51_FGIR_DYNAMIC_SCHEMA_STORAGE.md`.

## FGIR dynamic function-table audit — archive 342

- [x] Confirm the user's archive 341 validation: full native `make test` passed 1,156/1,156 in 11:07.401; WASM API/lifecycle and all WASM Node.js smoke tests passed; compile high-water remained 42,729,472 bytes after 100 runs.
- [x] Measure `FgirFunction` and the remaining fixed table on the native ABI: `sizeof(FgirFunction)=3,940 B`, `functions[256]=1,008,640 B`, `sizeof(FgirProgram)=1,160,584 B`.
- [x] Audit all parser append paths, linker merge paths, metadata readers, compiler serialization and runtime boundary.
- [x] Confirm `FgirFunction` contains only inline fields, so current value-copy operations remain safe without a deep-clone helper.
- [x] Identify the `def` sugar pointer-invalidation hazard: reserve capacity before obtaining both the prototype and destination function pointers.
- [x] Select only the outer function table for the next implementation; keep the 256-function limit and the separate `exports[256]` table unchanged.
- [ ] Implement checked dynamic function reserve/free and parser/linker append updates in the next checkpoint.
- [ ] Add lifecycle, 256/257 boundary, def-sugar-growth, module/entry link and sanitizer tests.
- [ ] Run complete native and user-side Emscripten/Node.js suites after implementation.

**Archive 342 result:** audit-only checkpoint; no C implementation or language behavior changed. The next candidate is the fixed `FgirProgram.functions[256]` table, occupying 1,008,640 bytes. `FgirFunction` has no pointer members, so its current assignment/copy semantics remain safe. The parser's sugar-clause path must reserve before taking either the prototype or destination pointer to avoid invalidation during `realloc`. The independent `exports[256]` table is explicitly out of scope. See `plans/52_FGIR_FUNCTION_STORAGE_AUDIT.md`.


## FGIR dynamic function-table implementation — archive 343

- [x] Replace the embedded `FgirProgram.functions[256]` table with an owned pointer, count and capacity.
- [x] Add checked geometric growth capped at 256, zero-initialize new capacity and free/reset it in `flat_fgir_free()`.
- [x] Reserve before both parser append paths; in particular, reserve before taking the `def` sugar prototype pointer.
- [x] Reserve in both fmod-module and entry-program linker merge paths while preserving the existing 256-limit diagnostics.
- [x] Keep `FgirFunction` self-contained and retain safe value-copy semantics. Keep `exports[256]` unchanged.
- [x] Add reserve/free lifecycle, 256/257 parser, 256/257 cross-module linker and forced `def`-sugar-growth regression tests.
- [x] Run focused function, fmod and native-use tests and the new parser/linker boundary cases under ASan/UBSan with leak detection.
- [x] Complete the full native `make test`; the user confirmed 1,160/1,160 PASS in 10:28.253.
- [x] Run `make docs` (50 sections).
- [x] Rebuild WASM with Emscripten and rerun the Node.js WASM suites; the user confirmed all WASM tests pass and compile high-water remains 42,729,472 bytes after 100 runs.

**Archive 343 implementation:** `FgirProgram.functions` is now dynamic and allocated on demand. Native `sizeof(FgirProgram)` decreases from 1,160,584 B to 151,960 B (1,008,624 B saved); `sizeof(FgirFunction)` remains 3,940 B. The 256-function language limit and independent fixed `exports[256]` table are unchanged. Focused tests passed: `flc[1-12]` (all 11 discovered cases), `func[1-9]` / `func[19-27]` and `func[38]`, `fmod` (18/18), and `native_use` (6/6). The new lifecycle harness, 256/257 parser boundary and 256/257 linker boundary passed ASan/UBSan with leak detection. The documentation build passed (50 sections). The user subsequently completed the full native `make test` on Termux with 1,160/1,160 PASS in 10:28.253. The user also rebuilt WASM and confirmed the full WASM suite passes; compile high-water remains 42,729,472 bytes after 100 runs. See `plans/53_FGIR_DYNAMIC_FUNCTION_STORAGE.md`.


## Native test harness object reuse — archive 344

- [x] Audit the slow native FLVM integration cases and their `make test-*` recipes; confirm repeated compilation of the same `FB_SRC`, `VM_SRC` and `PORT_SRC` source lists.
- [x] Add a configuration-keyed shared object cache for the 34 native C integration harness targets.
- [x] Track header dependencies with compiler-generated `.d` files and isolate different compiler/feature/sanitizer flag configurations.
- [x] Keep `FLAT_RUNTIME_TESTING` isolated in a dedicated `flat_runtime_testing.o`; retain the Emscripten no-pthreads and small standalone harnesses on their existing paths.
- [x] Preserve every test case, assertion, stress iteration and timeout; no runtime behavior changed.
- [x] Build all native tools and run the focused `flvm[13-53]` selection cold-cache and warm-cache (41/41 PASS in 28.652 s and 20.975 s respectively on the available x86-64 host).
- [x] Run `make docs` (50 sections).
- [ ] Complete the full default native test suite after the Makefile optimization; the local attempt reached 320/1,160 before the 300-second execution limit.
- [ ] Have the user validate the cold-cache and warm-cache behavior on Termux/Android.

**Archive 344 implementation:** repeated native integration harness compilation is replaced with configuration-specific reusable objects under `bin/test-obj/`. On the available host, the first harness paid a one-time 7.476 s shared-object build; the other 40 harnesses reused those objects. The cold 41-case selection passed in 28.652 s and the warm selection in 20.975 s. A later full `make test` attempt reached 320/1,160 before the 300-second outer execution limit; this is incomplete, not a pass. `make docs` passed (50 sections). The 34 affected Makefile targets continue to compile their own harness source and link the common runtime objects. The runtime-worker-pool test uses a separately compiled `flat_runtime_testing.o` because it requires `FLAT_RUNTIME_TESTING`. No runtime or test semantics changed. See `plans/54_NATIVE_TEST_HARNESS_OBJECT_CACHE.md`.


## FGIR native-unit storage audit — archive 345

- [x] Measure the remaining native-unit, optional-native and export metadata on the native ABI.
- [x] Audit all FGIR native-unit append, initialization, reader, bytecode conversion and cleanup paths.
- [x] Confirm `FgirNativeUnit` and `FlatOptionalNativeDecl` are self-contained inline records and no whole-`FgirProgram` shallow-copy path exists.
- [x] Select only the outer `FgirProgram.native_units[32]` table for the next implementation; keep the per-unit declaration array and all bytecode structures unchanged.

**Archive 345 result:** audit-only checkpoint; no C implementation or runtime behavior changed. The fixed `native_units[32]` table occupies 131,200 bytes (`sizeof(FgirNativeUnit)=4,100 B`) out of the 151,960-byte `FgirProgram`. See `plans/55_FGIR_NATIVE_UNIT_STORAGE_AUDIT.md`.

## FGIR dynamic native-unit storage — archive 346

- [x] Replace only the outer `FgirProgram.native_units[32]` table with an owned pointer, count and capacity; keep each unit's `decls[32]` inline.
- [x] Add checked geometric reserve capped at 32, with an initial capacity of one large record, and free/reset lifecycle.
- [x] Route both `flat_native_use_add_unit()` and linker `prepare_unit_decls()` through the reserve/append path.
- [x] Preserve the 32-unit limit, existing overflow diagnostic, native aliases, FNUN serialization and bytecode representation.
- [x] Add lifecycle and linker 32/33 boundary regressions.
- [x] Run focused FGIR, fmod and native-use tests (37/37 PASS).
- [x] Run lifecycle and 32/33 linker boundary tests under ASan/UBSan with leak detection.
- [x] Run `make all -j2` and `make docs` (50 sections).
- [x] Complete the full default native test suite and user-side Emscripten/WASM validation.

**Archive 346 implementation:** `sizeof(FgirProgram)` decreases from 151,960 B to 20,776 B (131,184 B saved). `FgirNativeUnit` remains 4,100 B; its outer table starts with one record and grows geometrically to the unchanged 32-unit maximum. The focused `flc[1-14]`, `fmod` and `native_use` selections pass 37/37. The lifecycle harness and 32/33 linker boundary both pass ASan/UBSan with leak detection. The implementation environment did not complete the full suite, but the user subsequently validated the archive on Termux: `make test` passed 1,162/1,162 in 2:16.493 and the complete `make fleditor-wasm-test` suite passed. WASM linear memory remained stable at 42,729,472 bytes after 100 runs. See `plans/56_FGIR_DYNAMIC_NATIVE_UNIT_STORAGE.md`.

## FGIR export-table storage audit — archive 347

- [x] Measure the fixed export table and remaining `FgirProgram` size on the native ABI.
- [x] Audit every export-table reader/writer, `.fmod` preparation, linker merge boundary, and `FgirProgram` cleanup/reset path.
- [x] Confirm no program shallow-copy/clone path or retained export-table pointers require clone/invalidation handling.
- [x] Select only the outer export-name table for dynamic storage; keep the 256-symbol limit and 64-byte fixed-width names unchanged.
- [ ] Implement checked reserve/free lifecycle and parser duplicate/empty/256/257 boundary tests in the next archive.
- [ ] Run fmod semantics, ASan/UBSan leak checks, full native tests, and user-side WASM validation.

**Archive 347 result:** audit-only checkpoint; no C implementation or language behavior changed. On the measured native ABI, `FgirProgram` is 20,776 B and `exports[256][64]` occupies 16,384 B. A pointer plus capacity field is projected to reduce `sizeof(FgirProgram)` to 4,408 B (16,368 B saved) before allocating export records. Exports are read/written only in `flat_func.c`; `.fmod` preparation consumes the manifest before linker merge, and `flat_fgir_free()` is the common lifecycle endpoint. `optional_natives[32]` remains separate and out of scope. See `plans/57_FGIR_EXPORT_STORAGE_AUDIT.md`.


## FGIR dynamic export-table storage — archive 348

- [x] Replace only the fixed export-name table with an owned pointer and capacity; preserve fixed-width 64-byte records.
- [x] Add checked geometric reserve starting at one record and capped at 256; free/reset with the common FGIR lifecycle.
- [x] Preserve empty-symbol no-op, duplicate suppression, symbol truncation, 256-symbol maximum, and existing 257th-symbol diagnostic.
- [x] Add reserve/free lifecycle, parser 256/257/duplicate/empty, and `.fmod` 256-export/missing-definition regressions.
- [x] Run focused FGIR, fmod and native-use tests; run lifecycle and parser/fmod boundary checks under ASan/UBSan.
- [x] Run `make all -j2` and `make docs` (50 sections).
- [x] Complete the full default native suite and user-side Emscripten/WASM validation.

**Archive 348 implementation:** `sizeof(FgirProgram)` decreases from 20,776 B to 4,408 B (16,368 B saved). The export array starts with one 64-byte record and grows geometrically to the unchanged 256-symbol maximum; programs with no exports allocate no export records. Focused `flc`, `fmod` and `native_use` tests pass 39/39, and the additional maximum `.fmod` test passes independently. ASan/UBSan with leak detection passes the direct lifecycle harness; an ASan/UBSan `flc` build passes parser 256/257, max-256 `.fmod`, missing-export-definition and all 18 existing fmod cases (20/20). `make docs` passes with 50 sections. The user validated the archive on Termux: `make test` passed 1,165/1,165 in 2:36.745 and the complete `make fleditor-wasm-test` suite passed; WASM memory remained stable at 42,729,472 bytes after 100 runs. See `plans/58_FGIR_DYNAMIC_EXPORT_STORAGE.md`.


## FGIR optional-native union storage audit — archive 349

- [x] Measure `FgirProgram.optional_natives[32]`, record size, offsets, and projected pointer-plus-capacity layout on the native ABI.
- [x] Trace union construction, core-module filtering, duplicate suppression, first-seen ordering, alias handling, compiler serialization, and separate bytecode/runtime manifest ownership.
- [x] Audit `FgirProgram` initialization, reset, cleanup, linker failure paths, and shallow-copy/pointer-retention risks.
- [x] Select only the FGIR outer optional-native union for dynamic storage; keep per-unit declarations and `FlatBytecode.optional_natives[32]` unchanged.
- [x] Implement checked reserve/free lifecycle and union builder allocation-failure distinction.
- [x] Add lifecycle, dedup/order/core-filter, 32/33 boundary, FNAT serialization and decode regressions.
- [x] Run focused ASan/UBSan lifecycle and union checks, `make all -j2`, and `make docs`.
- [ ] Complete the full default native suite and user-side WASM validation.

**Archive 349 result:** audit-only checkpoint; no C implementation or runtime behavior changed. On the measured native ABI, `FgirProgram` is 4,408 B and its fixed optional-native table occupies 4,096 B (`FlatOptionalNativeDecl` is 128 B). A pointer plus capacity field projects to `sizeof(FgirProgram) == 320 B`, saving 4,088 B before allocating any manifest entries. The FGIR union is built once after linking; it excludes core modules, deduplicates by module name in first-seen order, and writes empty aliases. Source aliases remain in per-unit declarations. The separate `FlatBytecode.optional_natives[32]` table and FNAT bytecode representation remain out of scope. See `plans/59_FGIR_OPTIONAL_NATIVE_STORAGE_AUDIT.md`.

**Archive 350 implementation:** `FgirProgram.optional_natives` is now an owned dynamic array, allocated on the first unique non-core module and grown geometrically to 32. `sizeof(FgirProgram)` decreases from 4,408 B to 320 B (4,088 B saved). The union builder preserves core filtering, module-only deduplication, first-seen order, empty aliases, and commits the count only after successful completion. It distinguishes 33rd-module overflow (`-1`) from allocation failure (`-2`), and the linker emits separate diagnostics. `FlatBytecode.optional_natives[32]`, FNAT/FNUN layouts and VM behavior are unchanged. New `flc[18-19]` cover lifecycle/union boundaries and a compiler-to-bytecode decode round-trip. Focused `flc[1-19]`, `fmod`, and `native_use` pass 42/42; the direct lifecycle/union harness passes ASan/UBSan with leak detection. `make all -j2` passes. Full native and user-side WASM validation remain pending. See `plans/60_FGIR_DYNAMIC_OPTIONAL_NATIVE_STORAGE.md`.


## FLEditor WASM Task/Channel execution — archive 351

- [x] Reuse the existing `FlatRuntime` / `FlatTaskNativeBridge`; do not implement a JavaScript task emulator.
- [x] Add one-shot `flat_runtime_run_scheduled()` that creates a root task, drives the cooperative scheduler and returns the root result.
- [x] Route `flat_wasm_run()` through the scheduled runtime while preserving existing WASM C/JS API signatures and exit/termination behavior.
- [x] Ensure runnable children finish and leftover waiters are cancelled; release task VMs, task-held Channel references, open transactions and Channel records before runtime destruction.
- [x] Keep FLEditor WASM single-threaded; do not start pthreads or browser Web Workers.
- [x] Add three runnable examples in both `examples/task_channel/` and the FLEditor example library: parallel inventory, transaction rollback, and task result inspection.
- [x] Add `flvm[56]` for native one-shot runner result propagation and repeated cleanup; add a Node.js WASM smoke test for all three examples.
- [x] Update Task/Channel native docs and runtime model to distinguish direct VM execution, native FlatRuntime hosts and the FLEditor WASM adapter.
- [ ] Run the complete native suite and `make fleditor-wasm-test` on Termux after applying archive 351.

**Archive 351 implementation:** `Makefile.wasm` already contained the runtime, Channel, scheduler and native Task/Channel sources. The missing bridge was that `flat_wasm_run()` invoked `flat_vm_run_opts()` directly. It now uses `flat_runtime_run_scheduled()`, which owns a one-shot runtime/root task and performs deterministic task/Channel cleanup. The three FLEditor examples are included in `manifest.json`. `make all -j2` and `flvm[56]` pass locally; `emcc` is unavailable here, so the actual WASM rebuild and Node.js WASM smoke remain pending Termux validation. See `plans/61_FLEDITOR_WASM_TASK_CHANNEL_RUNNER.md`.


## Archive 352 — block-scoped panic `.any` and Task/Channel closeout

- Implemented `#panic.#target.any` as a general block-scoped panic handler, not a task-only feature.
- Runtime precedence: exact fault-site handler; `.any` for the faulting block; `.any` handlers for active caller blocks from nearest to outermost; global `#panic.any`.
- Explicit `PANIC` compilation now resolves the block-scoped `.any` before global fallback.
- Added regressions for a native panic in the owning block, a panic in a nested called block recovered by the caller, explicit `PANIC`, and a panic recovered inside a spawned child-task VM.
- FLEditor WASM Task/Channel integration from archive 351 is user-validated: `make fleditor-wasm-test` passed on Termux, and the user confirmed all three examples run in the browser.
- Archive 352 closes the planned v1.0 panic `.any` item. Archive 356 is the current DOM checkpoint. Event-driven task dispatch is a proposed future direction only; see plan 66. v2.0 cross-platform worker validation and v2.5 UI Runtime remain separate open work.


### Event-driven task dispatch update (archive 360)

The event broker now exposes per-subscription pending depth and wakes the runtime ready-queue wait primitive after successful event publication. This is notification plumbing only; automatic event-to-task dispatch and FlatL `task.spawnFor()` remain pending.

- Event-driven task dispatch: archive 361 adds the runtime primitive that turns one queued event into a normal runnable handler task with event-first positional binding; public `task.spawnFor()` and automatic subscription scheduling remain pending. See `66_EVENT_DRIVEN_TASKS_AND_EVENT_SOURCES.md`.


### Event-driven Task dispatch (archive 368 checkpoint)

- [x] Unified FlatL subscription configuration in `task.spawnFor(..., options?, ...boundArgs)`.
- [x] Per-subscription `mode`, `maxActive`, `queueCapacity` and `overflow` options with documented defaults.
- [x] Implement `wait` backpressure, `dropOldest`, `dropNewest` and `error` overflow behavior in the runtime event broker.
- [x] Preserve C compatibility APIs and keep `task.spawnForMode()` / `task.spawnForLimit()` as compatibility helpers.
- [ ] Add host source adapters (sockets first), source-close/error lifecycle, and persistent FLEditor WASM sessions.


### Event-driven Task / WebSocket progress (archive 371)

- [x] Add a native WebSocket background reader and bridge lifecycle/frame events into the runtime broker using the shared `socket` source kind.
- [x] Preserve text/binary opcode in `{ opcode, data }` payloads; answer ping frames with pong; retry `wait` backpressure without entering the VM.
- [ ] Expose native FlatL `sockets.wsConnect()`, `sockets.wsSend()` and `sockets.wsClose()` over the host adapter and numeric socket ID registry.
- [ ] Validate a live handshake on a libcurl build that advertises `ws` and `wss`; the current environment's libcurl 8.10.1 does not advertise these protocols.
- [ ] Add the browser WebSocket host bridge and persistent FLEditor WASM runtime session.

- [x] Runtime-owned numeric WebSocket registry and lifecycle API (archive 372).
- [ ] Expose WebSocket connect/send/close through the FlatL `sockets` native module.

- [x] Expose initial FlatL WebSocket operations through the runtime task bridge (`sockets.wsSupported`, `wsConnect`, `wsSend`, `wsClose`).
- [ ] Validate a real WebSocket handshake and end-to-end `task.spawnFor` receive flow on a libcurl build with `ws`/`wss` enabled.


### WebSocket native API follow-up (archive 374)
- [x] Add binary WebSocket frame sending through `sockets.wsSendBinary(socketId, bytes)`.
- [x] Make connect timeout and inbound maximum frame size configurable in `sockets.wsConnect(url, connectTimeoutMs?, maxFrameBytes?)`.
- [ ] Add a live WebSocket round-trip test on a build with libcurl `ws`/`wss` support.


### Event-driven sockets — archive 375

- [x] Retain and replay the initial `socket/open` event when `wsConnect()` returns before `task.spawnFor()` subscribes.
- [x] Extend the loopback WebSocket integration harness to cover late open subscription plus text/binary client-server round trips.
- [ ] Execute the live loopback handshake and text/binary round trip on a libcurl build advertising `ws` and `wss`; current linked libcurl does not provide these protocols.
- [ ] Add bounded pre-subscription buffering for `receive` events if the API should guarantee delivery when peers send immediately after the handshake.
- [ ] Add browser WebSocket bridge and persistent FLEditor WASM runtime session.


- [x] WebSocket event delivery before subscription: retain the one-shot `open` event and a bounded FIFO of early `receive` frames; apply explicit overflow behavior and surface hard failures as `socket/error`. See archive 376 in `66_EVENT_DRIVEN_TASKS_AND_EVENT_SOURCES.md`.
