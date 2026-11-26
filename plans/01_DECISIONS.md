# Task Runtime & Channels — decizii de design

**Status:** decizii de proiect agreate în discuțiile din 2026-11-25; detaliile marcate „de definit” necesită mini-spec înainte de implementare.

## 1. Model de execuție

- Task este o unitate independentă de execuție, distinctă de `call`, `func` sau `def`.
- Fiecare task are propriul context de execuție și propriul VCX.
- Un task nu citește și nu modifică direct variabilele VCX ale altui task.
- Partajarea explicită a datelor se face prin Channel handle transmis task-ului.
- Handle-urile runtime pentru task, VCX și Channel sunt NUMBER (ID-uri în pool-uri interne), nu un tip VAL nou.
- Schedulerul inițial este cooperativ și rulează pe un singur thread OS. Worker pool-ul este o etapă ulterioară.

API țintă, cu semnături finale încă de aliniat la convențiile native existente:

```flat
ch = task.channel()
channel.set(ch, "counter", 10)
value = channel.var(ch, "counter")

id = task.spawn(#worker, ch, 10)  -- target API; arguments/Channel transfer are not in the first implementation
task.wait(id)
```

Nu introducem `send`/`recv`, message queues sau un model de streaming în acest design. Channel este un container de valori partajate, accesat prin `set` și `var`.

## 2. VCX și Channel sunt responsabilități diferite

- VCX oferă memorie privată task-ului.
- VCX nu este mecanism de sincronizare pentru date partajate.
- Channel este punctul de partajare și trebuie sincronizat.
- `channel.set()` și `channel.var()` urmează semantica de copiere FlatL; LIST/OBJECT trebuie să respecte regulile existente COW/deep-copy.
- Canalele diferite au mutex-uri independente.

## 3. Mutex și race conditions

**Mutex-ul face parte din Channel, nu este un add-on opțional.** Contractul de sincronizare există chiar dacă primul scheduler este cooperativ, pe un singur thread.

### C1 — sincronizare automată per operație

- Fiecare Channel deține propriul mutex.
- `channel.set()` și `channel.var()` obțin și eliberează automat mutex-ul.
- Operația individuală este atomică structural; protejează structura internă împotriva modificării simultane.
- Pentru scrieri concurente pe aceeași cheie, semantica este last-write-wins.
- Această protecție nu face atomică o secvență read-modify-write cu mai multe apeluri.

### C2 — tranzacții explicite

API:

- `channel.lock(ch)` — începe tranzacția exclusivă;
- `channel.unlock(ch)` — commit și eliberează lock-ul;
- `channel.revert(ch)` — anulează modificările tranzacției curente, dar păstrează lock-ul.

Reguli agreate:

- Lock non-reentrant; un al doilea `lock` de către același task este eroare.
- Ownership-ul este `flat_task_id`, nu `pthread_t`, deoarece mai multe task-uri cooperative pot rula pe același thread OS.
- La terminarea task-ului cu tranzacție activă se face automat revert și release, niciodată commit implicit.
- La panic în timpul tranzacției se face automat revert și release.
- Mutex-urile canalelor diferite sunt independente.
- Deadlock detection/timeout nu intră în MVP; lock ordering și evitarea deadlock-urilor trebuie documentate.
- Implementarea tranzacției poate începe cu snapshot și evolua ulterior spre working journal/COW, fără schimbarea contractului public.

## 4. Panic handling pentru block-uri și task-uri

FlatL are deja un mecanism `#panic` asociat block-urilor. Se dorește extinderea lui cu `.any` pentru un handler al oricărui panic produs de block-ul specificat.

Sintaxa agreată:

```flat
#worker
    -- codul de execuție al task-ului
    ...

#panic.#worker.any
    -- prinde orice panic al block-ului #worker
    -- poate inspecta eroarea
    jump #workerErrorHandler

#workerErrorHandler
    -- continuă în contextul aceluiași task
```

Semantica agreată:

1. `#panic.#worker.any` este un block separat asociat block-ului `#worker`, nu un sub-block indentat în `#worker`.
2. Indentarea închide block-urile FlatL; nu se folosește `#end` pentru închiderea unui block.
3. Handler-ul nu reia automat execuția la instrucțiunea care a provocat panic-ul.
4. Handler-ul poate inspecta diagnosticul și poate face `jump` la alt block.
5. Jump-ul de recuperare continuă în contextul aceluiași task și cu același VCX.
6. Dacă nu există handler `.any` aplicabil, se continuă mecanismul general de panic handling existent.
7. `.any` trebuie să fie o extensie generală a mecanismului block panic, nu o funcționalitate specială exclusiv task-urilor.

De verificat în mini-spec: comportamentul când panic-ul apare într-un block apelat din block-ul task-ului, identificarea block-ului de origine, ordinea handler-elor specifice și fallback-ul existent.

Un panic nepreluat marchează task-ul `FAILED`; nu oprește automat schedulerul sau celelalte task-uri.

## 5. Erori și cleanup

- Fiecare task păstrează statusul și panic reason propriu.
- `task.wait(id)` trebuie să permită detectarea și inspectarea eșecului task-ului așteptat.
- Eroarea originală nu trebuie suprascrisă de o eroare secundară apărută în cleanup; eroarea secundară se atașează ca diagnostic.
- Orice resursă deținută de task trebuie curățată determinist la `COMPLETED`, `FAILED` sau, dacă va exista, `CANCELLED`.
- Cleanup-ul unui task nu trebuie să curețe frame-urile, VCX-ul sau resursele altui task.
- Trebuie testate explicit panic în/înainte/după tranzacție, task terminat cu lock activ, panic în handler și eșec în timpul cleanup-ului.

Stări lifecycle țintă: `CREATED`, `RUNNABLE`, `RUNNING`, `WAITING`, `COMPLETED`, `FAILED`; `CANCELLED` rămâne opțional și trebuie specificat înainte de expunerea API-ului.

## 6. Runtime logging și observabilitate

Logging-ul este un mecanism central, opțional, dezactivat implicit.

### Categorii CLI agreate

```bash
flvm program.fl --show-tasks-log
flvm program.fl --show-channel-log
flvm program.fl --show-tasks-log --show-channel-log
```

Numele pot fi ajustate numai dacă se păstrează claritatea și compatibilitatea cu test runner-ul. Categoriile se activează independent.

### Format evenimente

- Format structurat și stabil, lizibil de oameni și ușor de parsat de test runner.
- Fiecare eveniment are `seq` monoton crescător în proces.
- Evenimentele task includ `taskId`; evenimentele Channel includ `chanId` și `taskId` când operația aparține unui task.
- Evenimentele trebuie să descrie lifecycle, scheduler transitions, set/read, lock/commit/revert, panic/handler/jump și cleanup.
- Exemple conceptuale:

```text
{seq: 1, taskId: 3, event: started, block: "#worker1"}
{seq: 2, chanId: 21, taskId: 3, event: set, var: "x", type: "list"}
{seq: 3, chanId: 21, taskId: 4, event: read, var: "x", type: "list"}
```

### Output și memorie

- Output live în `stdout` când opțiunea CLI este activată.
- Logging dezactivat implicit: fără formatare, alocări sau păstrare de istoric când categoria este dezactivată.
- Buffering este opțional și limitat; bufferul plin nu trebuie să crească nelimitat memoria. Se contorizează evenimentele eliminate.
- Bufferul nu este modul implicit; live output este modul implicit pentru `--show-*`.
- API-ul intern central configure/enable/disable trebuie să permită controlul categoriilor. Activarea dinamică în timpul programului este posibilă ca mecanism intern, dar nu este încă API FlatL decis.

### case.toml

Dorim ca testele să poată activa logurile și verifica evenimentele fără să depindă de mesaje arbitrare. Extinderea exactă a schemei este de decis după auditarea runner-ului. Propunere conceptuală:

```toml
[run]
show_tasks_log = true
show_channel_log = true
log_buffer_size = 4096

[expect.runtime]
tasks_completed = 2
active_channel_locks = 0
leaked_task_contexts = 0
```

Pentru verificări precise, preferăm evenimente structurate/snapshot de runtime decât parsarea fragilă a textului de diagnostic.

## 7. Diagnostic helpers țintă

- `task.log(taskId)` — status, block de intrare/curent dacă există, VCX, motiv WAITING, panic reason, lock deținut și cleanup.
- `channel.log(chId)` — ID, reference count, număr chei, lock owner, tranzacție activă, modificări pending și lifecycle.

Acestea sunt helpers de diagnostic propuse; nu sunt încă implementate și nu trebuie confundate cu opțiunile CLI de logging automat.

## 8. Teste

Toate testele funcționale se adaugă în `test/cases/` și se execută prin `bin/run_tests`. Nu se creează teste Python sau runner paralel.

Categorii obligatorii:

- task lifecycle și handle-uri invalide;
- VCX isolation;
- Channel create/set/var și cheie absentă;
- copy semantics pentru NUMBER/STRING/LIST/OBJECT;
- canale independente;
- lock/unlock/revert și read-modify-write;
- lock reentrant, unlock/revert fără ownership și owner greșit;
- panic handler specific, `.any` și fallback general;
- auto-revert și release la panic/terminare;
- un task FAILED nu afectează alte task-uri;
- resource lifecycle, reference count și handle-uri stale;
- worker pool stress/multi-writer după introducerea thread-urilor.

Testele trebuie să verifice rezultate și stări, nu doar exit code `0`. Testele de concurență verifică invariants, fără să presupună o ordine de scheduling fixă.

## 7. Persistent VM and cooperative task lifecycle (archive 266)

The normative lifecycle contract is documented in [TASK_RUNTIME_MODEL.md](../doc_src/TASK_RUNTIME_MODEL.md). Key decisions:

- `FlatRuntime`, `FlatTask`, and `FlatVm` are separate ownership/lifecycle objects; the direction is Runtime → Task → VM → VmState/resources.
- Task states are `CREATED`, `RUNNABLE`, `RUNNING`, `WAITING`, `COMPLETED`, `FAILED`, and `CANCELLED`; terminal states never become runnable again.
- A persistent VM yields only after a complete bytecode instruction. The saved IP points to the next instruction. Native calls are not preemptible in the first cooperative implementation.
- Persistent VM creation owns a deep bytecode copy and scalar VM options; it must not retain caller-owned bytecode, `FlatVmOptions *`, or output-buffer pointers across yields.
- Runtime destruction is busy while any VM/task remains registered, not merely while a synchronous run call is active.
- Unhandled panic/FAIL affects only the owning task. Cancellation is observed at an instruction safe point and performs deterministic cleanup.
- `task.wait` is scheduler-level suspension, never a blocking OS wait. Wait cycles are not detected in the first MVP.
- The C API shape remains opaque/provisional until the persistent VM refactor and its tests are implemented. No scheduler, task natives, Channels, or pthread workers are claimed in archive 266.


## First language-native milestone (archive 278)

The initial implemented subset is `use native task`, `task.spawn(#block)`, `task.wait(taskId)`, and `task.state(taskId)`. `task.spawn` currently accepts only a block label from the same bytecode and creates a child with a fresh private VM/VCX; it does not accept arguments or Channel handles yet. `task.wait` is cooperative and scheduler-level, never an OS-blocking wait. `task.state` returns the numeric `FlatTaskState` value. The broader argument/Channel API shown in the initial sketch remains a target, not current behavior.


## Spawn argument binding amendment — archive 279

`task.spawn(#block, arg1, ...)` is now implemented. The block header supplies the formal names (`#worker(price, items)`), and the number of expressions must match exactly. Argument expressions are evaluated in the parent and copied into the child's initial private VCX. NUMBER/BOOL/NULL and strings are copied; LIST/OBJECT are recursively deep-copied across VM-owned pools. File handles are rejected because they are bound to a VM-local file table. This is not shared state; use a future Channel API for explicit sharing. `.fb` v14 serializes block parameter names in BPAR while the loader keeps v3–v13 compatibility.


## 9. Cooperative Channel contention, retry și task results (archive 283; confirmat în 284)

### 9.1 `yield` versus `retry`

Acestea sunt două continuări diferite și nu trebuie unificate semantic:

- **Yield completed** (`task.wait`): instrucțiunea native s-a executat complet; IP-ul indică instrucțiunea următoare la reluare.
- **Retry pending** (contendență Channel): instrucțiunea native nu s-a executat; IP-ul rămâne pe instrucțiunea curentă, iar după wakeup VM-ul o execută din nou.

Un native care solicită retry nu trebuie să fi produs efecte externe/parțiale și nu trebuie să publice un rezultat provizoriu în VCX. La retry, native-ul poate fi invocat din nou; această proprietate trebuie să fie explicită pentru orice native viitor care folosește mecanismul. VM-ul nu avansează IP-ul și nu face bind la rezultatul intermediar.

`retry_requested` este semnalul semantic distinct; `yield_requested` indică predarea controlului schedulerului. Setarea ambelor la un native contended nu transformă retry în yield-completed.

### 9.2 Ownership și înregistrarea waiter-ului

- Un waiter are cel mult o dependență activă (`waiting_on`) și un singur `waiting_channel_id`.
- Pentru Channel contention, dependența este către task-ul care deține tranzacția, nu doar către Channel. Aceasta permite reutilizarea wait-graph-ului și detectarea ciclurilor.
- Înregistrarea re-verifică starea lock-ului după ce operația inițială a raportat busy. Dacă tranzacția s-a încheiat între timp, apelul este reîncercat imediat, fără suspendare.
- Înainte de a deveni WAITING, task-ul primește o referință la Channel. Referința rămâne până când task-ul ajunge terminal și cleanup-ul eliberează referințele; wakeup-ul singur nu o eliberează. Astfel, Channel nu poate fi distrus în fereastra suspendare/retry.
- Nu promitem FIFO pentru waiter-e. La unlock sunt trezite waiter-ele asociate Channel-ului; schedulerul cooperativ le rulează, iar operațiile care încă întâlnesc lock-ul se pot reînregistra.

### 9.3 Reguli wakeup

- `unlock`/commit eliberează tranzacția și trezește waiter-ele Channel-ului.
- `revert` restaurează snapshot-ul, dar păstrează lock-ul; nu trezește waiter-ele.
- Completion, failure sau cancellation a owner-ului fac rollback + release și trezesc dependențele task-ului owner.
- Un waiter trezit reia instrucțiunea originală; wakeup-ul nu presupune că operația va reuși fără o nouă verificare.
- Un waiter anulat își eliberează referința Channel prin cleanup-ul task-ului.

### 9.4 Limita API-ului host

Retry/wait este disponibil numai când există un task FlatL și o continuare VM care poate fi suspendată. Apelurile C host directe nu au un waiter VM; ele continuă să raporteze `busy` și nu blochează thread-ul apelant.

### 9.5 `task.result(taskId)`

`task.result` este disponibil numai pentru task-uri terminale și întoarce un OBJECT cu `state`, `exitCode`, `termination` și `diagnostic`. Nu reprezintă un return value arbitrar al block-ului și nu introduce convenție de return-value pentru block-uri FlatL. Diagnosticul din obiectul FlatL este limitat la inline-string limit; API-ul C păstrează `FlatVmResult` complet.

Aceste reguli sunt contracte ale runtime-ului cooperativ. Ele nu implică fairness FIFO, mutex pthread, reentrancy generală sau thread safety.


## 10. Task destruction, wait dependencies și Channel references (archive 285)

- `flat_runtime_task_destroy()` acceptă numai task-uri `CREATED` sau terminale (`COMPLETED`, `FAILED`, `CANCELLED`). Un task `RUNNING`, `RUNNABLE` sau `WAITING` nu poate fi distrus direct.
- Un task target nu poate fi distrus cât timp există task-uri `WAITING` cu `waiting_on == target_id`. Host-ul trebuie să lase dependența să se rezolve sau să anuleze waiter-ele înainte de destroy.
- Anularea unui waiter elimină dependența outgoing (`waiting_on` și `waiting_channel_id`) și eliberează toate referințele Channel deținute de task. Acest lucru este valabil atât pentru waiter-ul de `task.wait`, cât și pentru waiter-ul de Channel contention.
- Wakeup-ul mută waiter-ul din `WAITING` în `RUNNABLE`, dar nu eliberează referința Channel și nu face task-ul destroyable. Referința rămâne până la completion/failure/cancellation; un waiter RUNNABLE trebuie să fie rulat sau anulat înainte de destroy.
- Distrugerea unui task `CREATED` care deține o tranzacție Channel face abort/rollback și eliberează referințele task-ului. Dacă există waiter-e către owner, destroy-ul este respins înainte de cleanup; după anularea waiter-elor, owner-ul poate fi distrus.
- Channel destroy este permis numai după ce tranzacția nu mai este activă și nu mai există referințe de la alte task-uri. Testele verifică explicit această ordine.
- Aceste reguli sunt validate în runtime-ul cooperativ single-thread. Nu definesc sincronizare între host threads și nu fac sigură concurența dintre `schedule`, task APIs și `runtime_destroy`.


## 11. Channel transaction ownership across cooperative suspension (archive 286)

A task that owns any Channel transaction must not enter `WAITING` on another Channel. This rule applies uniformly to all scheduler-aware Channel natives:

- `channel.lock()` contention must fail rather than register a waiter when the caller already owns a transaction.
- Contended `channel.set()` and `channel.var()` must apply the same rule through the shared Channel-wait registration path.
- The check covers transactions on any Channel, not only the Channel involved in the contended operation.
- A rejected operation fails the current task normally; terminal cleanup rolls back every transaction owned by that task and releases its Channel references.

Rationale: the scheduler gives runnable transaction owners priority to preserve cooperative transaction atomicity. Suspending such an owner while it holds another transaction would strand that transaction across an arbitrary wait and can create cross-Channel dependency cycles or starvation. This is the same no-suspend-while-owning rule already applied to `task.wait()`.

This remains a cooperative single-thread contract. It is not a cross-thread lock-ordering or deadlock-prevention guarantee for pthread workers.


## 12. Multiple Channel transactions per owner and independent waiter wakeup (archive 287)

- Un task poate deține simultan tranzacții pe mai multe Channel-uri; fiecare tranzacție păstrează ownership-ul aceluiași `flat_task_id`, dar are lifecycle și commit/revert independente.
- Un task waiter are în continuare o singură dependență activă (`waiting_on`) și un singur `waiting_channel_id`; task-uri diferite pot aștepta același owner prin Channel-uri diferite.
- `unlock(chA)` trezește waiter-ele asociate numai cu `chA`. Dacă owner-ul continuă să dețină `chB`, waiter-ele pentru `chB` rămân WAITING.
- La completion/failure/cancellation/destroy al owner-ului, runtime-ul face abort pentru toate tranzacțiile încă deținute de acel task, apoi wakeup-ul dependențelor owner-ului trezește și waiter-ele Channel aferente. Fiecare waiter reia instrucțiunea sa originală și re-verifică lock-ul.
- Interdicția de suspendare cât timp task-ul deține orice tranzacție rămâne valabilă; această regulă se aplică owner-ului, nu waiter-elor independente.
- Acestea sunt reguli cooperativ-single-thread și nu definesc wakeup atomic între pthread-uri.


## 13. Chained task.wait and Channel dependencies (archive 288)

- `waiting_on` remains the single wait-graph edge for both `task.wait(target)` and Channel contention. `waiting_channel_id` is supplementary metadata used for Channel-specific wakeup and lifetime handling; it does not create a second graph edge.
- A Channel waiter may itself be the target of another task's `task.wait()`. This creates a valid chain such as `dependent -> Channel waiter -> transaction owner`.
- A separate task may simultaneously wait directly for that transaction owner. The graph can therefore contain multiple incoming edges of different semantic kinds to one owner, and a chain through a Channel waiter.
- Channel unlock wakes only waiters registered for that Channel. It does not wake tasks waiting for the Channel waiter to finish, nor tasks waiting directly for the transaction owner.
- Completion/failure/cancellation of the Channel waiter wakes its `task.wait()` dependants. Completion/failure/cancellation of the transaction owner independently wakes its own dependants.
- The current no-suspend-while-owning-transaction rule means a task that owns a Channel transaction cannot itself add an outgoing wait edge. Consequently a Channel-owner edge terminates at a transaction owner under the cooperative contract; mixed cycles cannot be constructed through a valid active transaction owner. This is an invariant, not a general deadlock-detection guarantee for future synchronization primitives.
- These semantics are cooperative and single-threaded. They do not establish atomic wait-graph updates or wakeups across host threads.

## 14. Cancellation and failure propagation through chained dependencies (archive 289)

- A Channel waiter may have incoming `task.wait()` dependants while it is itself WAITING on a transaction owner. Cancelling that intermediate waiter clears its outgoing Channel dependency/reference and transitions it to `CANCELLED`; the runtime then wakes tasks whose `waiting_on` targets that waiter.
- Cancelling or failing a transaction owner performs rollback/release of its Channel transactions and wakes both Channel waiters and direct `task.wait()` dependants through their respective dependency metadata.
- Wakeup is not transitive. When an intermediate task becomes RUNNABLE, its dependants remain WAITING until that intermediate task itself reaches a terminal state.
- A dependent that is woken by intermediate cancellation may subsequently execute an operation on the same still-locked Channel and become WAITING on the owner. Tests must therefore distinguish the immediate wakeup transition from the dependent's later execution/retry result.
- If a Channel waiter wakes, retries its original native instruction successfully, and then fails, terminal cleanup must wake its own `task.wait()` dependants. The original Channel transaction must also be released by failure cleanup.
- These guarantees are tested in the cooperative single-thread scheduler only. They do not establish concurrent cancellation safety or pthread worker readiness.


## 15. Explicit runtime teardown and resource ownership (archive 290)

- `flat_runtime_destroy()` is a refusal-based finalizer, not a force-cleanup API. It returns busy/failure while `active_runs`, registered task records, or registered Channels remain.
- Task records remain runtime-owned even after `COMPLETED`, `FAILED`, or `CANCELLED`; reaching a terminal state releases task-owned Channel transactions/references and resolves waiters, but does not remove the task record. The host must still call `flat_runtime_task_destroy()`.
- A task in `RUNNABLE` or `WAITING` must first be allowed to resolve or be cancelled. A WAITING task must be cancelled/resolved before destruction; a woken RUNNABLE waiter still owns its Channel reference until terminal cleanup/cancellation.
- `flat_runtime_channel_destroy()` rejects a Channel with an active transaction or references held by other tasks. Cancelling/completing/failing/destroying the referencing task releases its references; the host then explicitly destroys the Channel.
- Correct teardown order is therefore: resolve/cancel tasks and wait dependencies; destroy terminal/CREATED task records; destroy unlocked, unreferenced Channels; then destroy the idle runtime.
- `flat_runtime_destroy()` checks active VM/scheduler runs, but the host must prevent new API calls from racing with destroy. The runtime lock is not a lifetime-safe concurrent API gate. Task registry/scheduler APIs remain confined to the scheduler-driving host thread.
- A newly created runtime has independent registries and starts its monotone task/Channel ID sequences from their initial values. This does not imply process-global VM/native services are reset or thread-safe.

These are explicit cooperative lifecycle rules, not pthread synchronization guarantees.


## 16. Worker-pool ownership and synchronization design baseline (archive 291)

Archive 291 adds `06_WORKER_POOL_SPEC.md`. The document distinguishes verified archive-290 source facts from the proposed v2.0 design. It is a specification checkpoint, not a pthread implementation or thread-safety declaration.

The design baseline is:

- A `FlatRuntime` owns its worker pool, task registry, ready/cancel queues, wait graph and Channel registry. Task records remain runtime-owned until explicit destruction.
- A RUNNING task has exactly one worker owner. Its `FlatVm` may be stepped, cancelled or destroyed by only one thread at a time. A task may migrate between workers only between completed VM quanta.
- A runtime scheduler mutex protects task state/registry, queue membership, wait graph, lifecycle and Channel registry references. A per-Channel mutex protects Channel storage and logical transaction fields. The permitted nested order is scheduler mutex → Channel mutex; Channel code must not call back into scheduler code while holding the Channel mutex.
- No runtime/Channel mutex may be held while stepping a VM, invoking native/plugin code, performing blocking IO or calling host callbacks.
- Wakeup racing with the end of a worker quantum requires an internal dispatch-ownership / wake-pending protocol so a task is enqueued exactly once.
- Cancellation in worker mode requires a request/safe-point protocol. The current synchronous cooperative cancellation semantics must not be silently changed.
- Channel transaction ownership remains based on `FlatTaskId`, not `pthread_t`; the logical transaction lock is not a pthread mutex held across task turns. The no-suspend-while-owning-any-Channel-transaction invariant remains in force.
- Unknown native plugins are not assumed thread-safe. Handler-table synchronization does not protect handler internals. The unsynchronized HTTP multipart sequence is a confirmed audit item to fix or serialize.
- Shutdown must stop admission, drain or request cancellation, wait for safe-point cleanup, join all workers, and only then release task/Channel/runtime resources. `pthread_cancel()` is excluded.
- Cooperative fallback remains available on platforms without pthread; WASM worker support is a separate design.

The exact public API for worker startup, cancellation requests, shutdown modes and plugin concurrency declarations remains open until the implementation plan is reviewed. See `06_WORKER_POOL_SPEC.md` for the full inventory, lock ordering and acceptance gates.


## 17. Native module concurrency classification and HTTP boundary sequence (archive 292)

- Native handler-table synchronization and native handler execution safety are separate concerns. The registry locks protect handler publication/lookup and plugin preparation; they do not protect handler internals or host resources used by a handler.
- Worker mode must classify each handler/module as `THREAD_SAFE`, `SERIALIZED`, or `UNSUPPORTED`. `THREAD_SAFE` requires independent VM state and thread-safe dependencies. `SERIALIZED` uses a per-module invocation gate held only around the handler call. `UNSUPPORTED` is rejected in worker mode.
- Until explicit capability metadata exists, unknown third-party plugins default to `UNSUPPORTED`; they are not silently treated as safe or serialized. Plugin authors must explicitly declare a capability before worker mode can invoke them.
- Built-in `task` and `channel` handlers remain blocked until the runtime scheduler/task/Channel synchronization protocol is implemented. Other built-ins are candidates only after their process-wide host and external dependency constraints are met and multi-VM tests pass.
- The HTTP multipart boundary sequence is process-wide allocation state and must be atomic. Archive 292 replaces the unsynchronized function-static `seq++` with a dedicated `atomic_uint_fast64_t` allocator in `flat_http_boundary.c`. The boundary string itself remains request-slot-owned.
- Environment mutation, plugin search-path changes, native registration/unload, logger reconfiguration, and legacy host path staging are setup-only operations and must be frozen while workers execute. Caller-owned streams, callbacks and handles remain single-owner unless separately synchronized.
- The native source audit is not a general thread-safety certification. External library guarantees and complete-VM concurrency tests remain acceptance gates.


## 18. External dependency and HTTP worker-preparation contract (archive 293)

- A dependency's upstream thread-safety statement applies only to its documented handle/context ownership model; it does not certify FlatL wrappers or shared VM/runtime state.
- PCRE2 bundled by FlatL is 10.49, 8-bit, without JIT. `mod_re.c` creates pattern and match-data objects per call and does not share mutable match state. It is a worker-mode candidate, pending complete-VM concurrency tests.
- Lexbor documents independent document creation but does not provide an explicit general concurrent-use guarantee in the reviewed documentation. FlatL must keep each DOM runtime/document separate and must not call `lexbor_memory_setup()` while workers execute. `dom` remains not certified until concurrent-context tests pass.
- Native libcurl calls use one easy handle and request-local callback/response state per request; handles are never shared concurrently. Every easy handle sets `CURLOPT_NOSIGNAL=1`.
- `flat_http_prepare()` is the explicit host hook for native `curl_global_init()` before worker threads are created. WASM implements it as a no-op. A worker-pool startup implementation must call it before spawning workers and must not rely on first-use lazy initialization inside a worker.
- Native HTTP worker eligibility remains conditional on the deployment libcurl build/runtime and resolver backend. `CURLOPT_NOSIGNAL` avoids process signal-handler manipulation but may limit DNS timeout enforcement when no asynchronous/threaded resolver is available.
- These dependency findings do not change the current rule: no pthread worker pool is permitted until the runtime scheduler, task registry, wait graph, Channel registry and cancellation/lifetime protocol are synchronized.


## 19. Lexbor worker-mode status and serialized fallback (archive 294)

- Lexbor's worker-mode status is `PENDING_THREAD_SAFETY_VALIDATION`, not `UNSUPPORTED`. This is an audit status, not a fourth runtime capability value; the eventual capability remains one of `THREAD_SAFE`, `SERIALIZED`, or `UNSUPPORTED`.
- First attempt to qualify DOM for `THREAD_SAFE`: each VM owns an independent `FlatDomRuntime` and documents; run concurrent complete-VM DOM regressions on the bundled Lexbor version and supported system-Lexbor variants, then run ThreadSanitizer and AddressSanitizer/UndefinedBehaviorSanitizer. A pass is necessary but must be reconciled with upstream guarantees and source findings.
- If independent concurrent use cannot be certified, the preferred fallback is `SERIALIZED`, not `UNSUPPORTED`: all Lexbor entry points must pass through one process-wide DOM invocation gate. This includes every `dom.*` native handler and Lexbor-backed document destruction during `flat_dom_runtime_destroy()`/VM teardown. Protecting handler calls alone is insufficient because teardown calls `lxb_html_document_destroy()` outside a native handler.
- The DOM gate must not be held across a VM quantum, task suspension, blocking host I/O, or unrelated module calls. It is acquired only around a bounded Lexbor operation or teardown operation. It must never be nested with scheduler or Channel locks; scheduler/Channel locks are released before invoking native code or destroying a VM.
- `lexbor_memory_setup()` and any other process-global Lexbor configuration are setup-only. They must be completed before workers start and remain immutable until every worker and DOM runtime has stopped. FlatL currently does not call `lexbor_memory_setup()`.
- `UNSUPPORTED` is reserved for a demonstrated constraint that cannot be made safe through independent contexts or the process-wide DOM gate. Do not classify DOM as unsupported merely because the upstream docs reviewed do not state a blanket thread-safety guarantee.
- No DOM worker capability is enabled by archive 294. The status remains pending until the specified concurrency and sanitizer acceptance tests exist and pass.


## 20. Lexbor independent-context concurrency evidence (archive 295)

- Archive 295 adds a focused pthread regression against the exact bundled Lexbor 3.0.0 amalgamation used by `mod_dom.c`. Eight threads independently create, parse, CSS-select, mutate, serialize and destroy 2,000 total HTML documents.
- The focused regression passes normally and under ThreadSanitizer and AddressSanitizer/UndefinedBehaviorSanitizer in the validation environment. Existing FlatL DOM integration cases also pass.
- This is evidence for independent Lexbor document/parser/selector contexts with the bundled build and default allocator. It is not a complete-VM test, does not exercise `FlatRuntime`/`FlatVm` concurrently, does not validate the system-Lexbor build, and is not a blanket upstream thread-safety guarantee.
- Keep the audit status `PENDING_THREAD_SAFETY_VALIDATION` until complete-VM DOM tests can run after the scheduler/lifetime protocol exists and supported system-Lexbor variants are tested. The evidence makes `THREAD_SAFE` a stronger candidate; it does not yet select that capability.
- Keep `SERIALIZED` as the fallback if later integration evidence identifies a race or cannot establish safe independent contexts. The process-wide gate must cover every Lexbor call, including document destruction during `flat_dom_runtime_destroy()`/VM teardown. Do not implement the gate pre-emptively in the current single-thread runtime.
- `lexbor_memory_setup()` remains setup-only and must not be called or changed while DOM operations are running. FlatL's current source does not call it.

## 21. Cancellation request and worker shutdown contract (archive 296)

Archive 296 separates the cooperative cancellation API that exists today from the request/cleanup protocol required by worker mode. The behavioral contract is recorded in [`09_CANCELLATION_SHUTDOWN_SPEC.md`](09_CANCELLATION_SHUTDOWN_SPEC.md); no asynchronous API or worker shutdown implementation is added in this archive.

### 21.1 Cancellation request is not cancellation completion

- Worker mode must introduce a request-only operation (proposed name: `flat_runtime_task_cancel_request`). The caller publishes cancellation intent; it must not call `flat_vm_cancel()`, destroy VM state, rollback Channel transactions, release VM resources, or publish `CANCELLED`.
- A task's cancellation request is separate from `FlatTaskState`. Repeated requests are idempotent. A terminal task keeps its already-committed result.
- The worker that owns a RUNNING task is the only thread allowed to step/cancel/destroy its VM. It observes cancellation at the next scheduler-visible safe boundary after `flat_vm_step()` returns (normally the configured quantum boundary); a long-running native call delays that boundary.
- CREATED/RUNNABLE/WAITING tasks that are not worker-owned are claimed as cancellation-cleanup work. Cleanup still runs through the worker/scheduler ownership protocol, not on an arbitrary request caller.
- `CANCELLED` is published only after Channel rollback, wait-edge removal, Channel-reference release, VM cleanup, and terminal result publication. Dependents wake after cleanup.
- Cancellation is cooperative and best-effort. A long-running/blocking native call delays cancellation; `pthread_cancel()` and arbitrary C-stack unwinding are forbidden.
- If a task reaches a terminal VM result before its owner observes the request, that terminal outcome wins. A request must never rewrite a completed/failed result.

### 21.2 Shutdown modes

- Worker-pool lifecycle is `UNINITIALIZED -> RUNNING -> DRAINING|CANCELLING -> STOPPED`.
- `DRAIN` closes task submission and `task.spawn`, but allows already-started work to reach terminal states. It does not auto-start `CREATED` tasks; their records remain host-owned cleanup obligations.
- If DRAIN reaches quiescence with unresolved WAITING tasks and no possible progress, it reports a blocked/deadlocked shutdown condition rather than silently discarding waiters or claiming success.
- `CANCEL` closes submissions and requests cancellation for every nonterminal task. Workers remain alive until all cleanup jobs finish.
- `CANCEL` may escalate `DRAIN`; downgrade is forbidden. Repeated same/stronger requests are idempotent.
- Proposed API split: nonblocking `flat_runtime_shutdown_request(runtime, mode)` followed by `flat_runtime_join(runtime)`. Exact status/timeout reporting for blocked DRAIN remains to be settled before the API is exposed.
- Worker join must complete before runtime/task/VM destruction. `flat_runtime_destroy()` remains the final host operation after workers are joined and all task records and Channels have been explicitly destroyed.

### 21.3 Scope and implementation status

These are the worker-mode design contracts, not current runtime capabilities. Existing `flat_runtime_task_cancel()` remains a cooperative host-thread API and the scheduler/task registry remain host-thread-confined. Archive 296 adds no pthread worker, cancellation-request flag/queue, shutdown state, or join implementation. Future implementation must pass the acceptance matrix in `09_CANCELLATION_SHUTDOWN_SPEC.md` before worker mode is enabled.


## 22. Native module capability metadata and worker admission (archive 297)

- Capability declarations are module-level, not handler-level. Handlers in one native module share dependencies and process-owned state; per-handler declarations could falsely imply isolation.
- The eventual capability values are `THREAD_SAFE`, `SERIALIZED`, and `UNSUPPORTED`. `PENDING_THREAD_SAFETY_VALIDATION` is an audit status, not a fourth capability; its conservative worker policy is `SERIALIZED`.
- Unknown modules and external plugins without an explicit capability declaration are `UNSUPPORTED` in worker mode. The existing cooperative plugin ABI remains compatible: `flat_native_plugin_init(reg)` is unchanged, and a plugin may additionally export `flat_native_plugin_thread_capability()`. Missing/invalid metadata is denied for worker admission.
- `flat_native_runtime_worker_policy()` is the worker preflight: it prepares the module, rejects `UNSUPPORTED`, and returns the effective `THREAD_SAFE` or `SERIALIZED` policy. The future scheduler must enforce the returned policy before dispatch.
- Capability metadata is process-wide setup state. It may be declared/overridden before worker startup and is frozen with `flat_native_runtime_freeze_worker_capabilities()` before the first worker is created. After freeze, lazy loading of a not-yet-loaded plugin is rejected and native search-directory changes are ignored. Plugin unloading and search-path changes are prohibited during worker lifetime.
- Built-in defaults follow the source audit: context-owned modules are candidate `THREAD_SAFE`; `io` is `SERIALIZED`; `dom` remains `PENDING_THREAD_SAFETY_VALIDATION` and maps to `SERIALIZED`; `task` and `channel` remain `UNSUPPORTED` until scheduler/Channel synchronization is implemented. HTTP remains conditional on pre-initialized libcurl and supported resolver configuration.
- A `SERIALIZED` module requires one process-wide per-module invocation gate around the entire native handler call. The gate must be released before scheduler/VM logic resumes. DOM's serialized boundary must additionally include Lexbor-backed document destruction during VM teardown. The actual gates and dispatcher integration are not implemented in archive 297.

## 23. Native synchronization abstraction and per-module invocation gates (archive 298)

- Use a small platform abstraction for native module gates: POSIX `pthread_mutex_t`, Windows `SRWLOCK`, and a no-op mutex for single-threaded Emscripten. Do not expose pthread types in public FlatL APIs.
- Each canonical native module owns an independent process-wide invocation gate. Do not use one global native mutex: a serialized `io` call must not block `dom` or another serialized module.
- The common `flat_native_invoke()` boundary consults the effective module policy. `SERIALIZED` and audit `PENDING` (effective serialized) acquire the module gate around the complete native dispatch; `THREAD_SAFE` does not.
- `UNSUPPORTED` remains executable in the current cooperative runtime for backward compatibility. Worker-mode preflight rejects it before dispatch; invocation gates are not a substitute for worker admission.
- The DOM gate covers both `dom.*` native dispatch and `flat_dom_runtime_destroy()`/VM teardown. A handler-only gate is insufficient because teardown calls Lexbor directly.
- Never hold scheduler or Channel locks while acquiring a module gate. Never hold a module gate across a VM quantum, task suspension, or scheduler wait. Native handlers must remain synchronous while holding the gate.
- Capability declarations must be frozen before worker creation. Plugin loading/unloading remains setup-only while worker execution is active. A serialized handler must not recursively dispatch into the same module while holding its non-recursive gate.
- Archive 298 adds synchronization/gate infrastructure only. It does not add worker creation, condition variables, task scheduling locks, cancellation requests, or worker shutdown.
