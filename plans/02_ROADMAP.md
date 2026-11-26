# Task Runtime & Channels — roadmap de implementare

**Versiune document:** 1.0 (plan)  
**Data:** 2026-11-25  
**Status:** F0, stepping-ul persistent, scheduler-ul cooperativ, wait/cancel, `task.spawn`/`task.wait`, argumentele task, integrarea FlatL pentru Channels, contention wait/retry și `task.result` sunt implementate. Arhivele 284–285 au extins auditul wakeup/destruction; arhiva 286 interzice suspendarea unui task cât timp deține orice tranzacție Channel; arhiva 287 verifică owner multi-Channel și waiter-e independente, corectând și un test cu ID-uri stale; arhiva 288 verifică lanțuri mixte `task.wait` + Channel contention și wakeup-ul diferențiat. Worker pool-ul rămâne blocat de auditul cross-thread.

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
6. [x] Izolare failure între task-uri; un task FAILED nu oprește schedulerul. Cancellation la safe point este implementată. Task panic wildcard în forma extinsă rămâne de verificat/implementat.
7. Extinderea generală a panic block handler cu `.any`, inclusiv forma `#panic.#worker.any`.
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

1. Worker pool pthread și coadă de task-uri.
2. Mutex/condvar și sincronizare cross-thread pentru scheduler, task registry și pool-urile de handle-uri.
3. Mutex real per Channel (de ex. pthread mutex cu tip/error checking potrivit platformei).
4. Ownership logic bazat pe `flat_task_id`, niciodată exclusiv pe `pthread_t`.
5. Audit și eliminare a stării globale incompatibile cu VM-uri simultane.
6. Shutdown, task-uri waiting și erori worker.
7. Stress tests multi-writer, invariants, deadlock și cleanup.
8. Build/feature behavior documentat pentru platforme fără pthread sau cu limitări WASM.

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
