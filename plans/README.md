# FlatL — Task Runtime & Channels plans

Acest director păstrează deciziile de arhitectură, direcția și ordinea de implementare pentru Task Runtime, Channel și viitoarea integrare UI. Scopul este ca lucrul să poată continua într-o conversație nouă fără să depindă de istoricul chatului.

**Ultima arhivă:** `flatL-main-354-dom-set-html.zip`. Arhiva 351 Task/Channel WASM este validată de utilizator atât prin `make fleditor-wasm-test` în Termux, cât și prin rularea manuală a celor trei exemple în browser. Arhiva 352 implementează `#panic.#target.any`, inclusiv caller-chain și child-task VM. Lucrul se oprește după etapa B; UI Runtime și validarea worker pool pe Windows rămân pentru o etapă viitoare.

## Documente

- [01 — Decizii și contracte de design](01_DECISIONS.md): ce am agreat explicit și ce nu trebuie schimbat accidental.
- [02 — Roadmap și fazare](02_ROADMAP.md): milestones v1.0, v1.5, v2.0 și v2.5, livrabile și criterii de acceptare.
- [03 — Audit inițial al runtime-ului](03_RUNTIME_AUDIT.md): constatările tehnice din arhiva 57 și limitele arhitecturii actuale.
- [04 — Stadiul implementării](04_IMPLEMENTATION_STATUS.md): ce există în arhiva curentă, ce nu este încă implementat și următorul pas.
- [05 — Roadmap original](05_SOURCE_ROADMAP.md): copia planului inițial furnizat pentru Task Channels, păstrată pentru trasabilitate.
- [06 — Worker Pool Architecture Specification](06_WORKER_POOL_SPEC.md): inventarul ownership-ului curent și specificația arhitecturii worker pool v2.0 și criteriile de validare.
- [07 — Native Module Thread-Safety Audit](07_NATIVE_THREAD_SAFETY_AUDIT.md): auditul stării globale a modulelor native, limitele de ownership și politica de capabilități pentru pluginuri.
- [08 — External Dependency Concurrency Audit](08_EXTERNAL_DEPENDENCY_AUDIT.md): evaluarea documentației upstream și a modului concret de utilizare PCRE2, Lexbor și libcurl în FlatL.
- [09 — Cancellation Request and Worker Shutdown Specification](09_CANCELLATION_SHUTDOWN_SPEC.md): contractul request-only cancellation, cleanup deținut de worker, DRAIN/CANCEL și join.
- [10 — Native Synchronization and Invocation Gates](10_NATIVE_SYNC_GATES.md): mutex abstraction multiplatformă, gates per modul, integrarea policy în dispatcher și protecția DOM teardown.
- [11 — Scheduler Synchronization Foundation](11_SCHEDULER_SYNC_FOUNDATION.md): primitive multiplatformă și MPMC ready queue introduse în arhiva 299; arhiva 302 integrează coada în schedulerul cooperativ.
- [12 — Runtime Scheduler Mutex Integration](12_RUNTIME_SCHEDULER_MUTEX.md): integrarea `FlatMutex` în `FlatRuntime`, sincronizarea task registry/wait graph și ownership-ul dispatch; arhiva 302 completează integrarea ready queue și mutex-ul Channel; fără worker threads.
- [13 — Build Target Source Parity](13_BUILD_TARGET_PARITY.md): corecțiile de build pentru testul HTTP `pkg-config` fallback și completarea surselor comune lipsă din `Makefile.wasm`.
- [14 — Ready Queue Integration and Channel Mutex Conversion](14_READY_QUEUE_CHANNEL_MUTEX_INTEGRATION.md): integrarea cozii FIFO/MPMC în schedulerul cooperativ și înlocuirea spin gate-ului Channel cu `FlatMutex`.
- [15 — Request-only Cancellation Runtime](15_CANCELLATION_REQUEST_RUNTIME.md): API request-only, safe-point cleanup deținut de scheduler și limitele pre-worker.
- [16 — Runtime DRAIN/CANCEL State Machine](16_RUNTIME_SHUTDOWN_STATE_MACHINE.md): admission closure, DRAIN/CANCEL lifecycle, blocked-wait status și escaladare.
- [17 — Runtime Worker Pool](17_RUNTIME_WORKER_POOL.md): pthread worker creation, shared scheduler dispatch, idle condition wait, startup rollback, shutdown wakeup and join.
- [18 — Worker Task Wait and Channel Contention Audit](18_WORKER_TASK_WAIT_CHANNEL_AUDIT.md): executes nested `task.wait()` and contended Channel transactions under four real workers, repeated across independent runtime lifecycles.
- [19 — Worker Wait-Graph and Cancellation Audit](19_WORKER_WAIT_GRAPH_CANCELLATION_AUDIT.md): validates chain/cycle behavior, cancellation of a middle waiter, and cleanup of a cancelled Channel waiter under real workers.
- [20 — Termux Test Portability](20_TERMUX_TEST_PORTABILITY.md): fixes locale helper linkage, targets malformed SCHM data correctly, and makes scheduler/cancellation regressions less timing-sensitive on mobile systems.
- [21 — Worker HTTP and DOM Lifecycle Audit](21_WORKER_HTTP_DOM_LIFECYCLE_AUDIT.md): real concurrent libcurl requests and DOM VM lifecycle/cancellation under the worker pool; DOM remains serialized pending broader validation.
- [22 — Termux Worker-Test Failure Diagnostics](22_TERMUX_WORKER_FAILURE_DIAGNOSTICS.md): corrects stale task/channel capability expectations and makes `run_tests` include captured output when an exec command fails; six Termux-specific failures remain to be diagnosed on device.
- [23 — Shell Capture Diagnostics](23_SHELL_CAPTURE_DIAGNOSTICS.md): captures stderr from complete compound commands and retains the actual final output tail on POSIX and Windows.
- [24 — Worker Test Stage Diagnostics](24_WORKER_TEST_STAGE_DIAGNOSTICS.md): adds explicit markers before FlatL compilation, C harness compilation, and harness execution for the seven Termux worker-test failures; `port[4]` verifies markers survive nonzero exits.
- [25 — Signal-aware Test Process Status](25_SIGNAL_AWARE_TEST_PROCESS_STATUS.md): preserves POSIX signal termination separately from ordinary exit codes; `port[5]` validates the signal result.
- [26 — VM Guard Stack Scratch](26_VM_GUARD_STACK_SCRATCH.md): moves large `OP_GUARD` scratch arrays from the worker thread stack into heap-backed VM state.
- [27 — Worker Task Cleanup Race](27_WORKER_TASK_CLEANUP_RACE_AUDIT.md): verifies cleanup ownership against concurrent cancellation requests.
- [28 — Native Plugin Loading Race Audit](28_NATIVE_PLUGIN_LOADING_RACE_AUDIT.md): covers concurrent first-use plugin loading and capability freeze.
- [29 — Worker Startup Failure Injection](29_WORKER_STARTUP_FAILURE_INJECTION.md): deterministic partial worker creation and join-failure rollback coverage.
- [30 — Multi-Runtime Worker-Mode Ownership](30_MULTI_RUNTIME_WORKER_MODE_OWNERSHIP.md): validates process-wide worker-mode reference ownership across independent runtimes.
- [31 — FlatThread Join Ownership](31_FLAT_THREAD_JOIN_OWNERSHIP.md): serializes same-runtime join ownership and preserves native handles across join failures.
- [32 — Worker Multi-Graph Cancellation Stress](32_WORKER_MULTI_GRAPH_CANCELLATION_STRESS.md): exercises independent wait graphs and simultaneous cancellation.
- [33 — Worker Channel Shutdown Stress](33_WORKER_CHANNEL_SHUTDOWN_STRESS.md): covers Channel contention and shutdown cleanup.
- [34 — Worker DAG Cancellation Stress](34_WORKER_DAG_CANCELLATION_STRESS.md): covers converging wait-graph cancellation.
- [35 — Worker DAG Staged Cancellation](35_WORKER_DAG_STAGED_CANCELLATION.md): covers staged cancellation at RUNNING/WAITING safe points.
- [36 — Worker Seeded DAG Stress](36_WORKER_SEEDED_DAG_STRESS.md): covers reproducible seeded wait-graph topologies.
- [37 — Worker Mixed Shutdown Soak](37_WORKER_MIXED_SHUTDOWN_SOAK.md): exercises concurrent DRAIN/CANCEL escalation and repeated runtime cleanup.
- [38 — Worker Failure Isolation Soak](38_WORKER_FAILURE_ISOLATION_SOAK.md): checks task-local failure across repeated worker-runtime lifecycles.
- [39 — Emscripten Single-Thread Worker Gate](39_EMSCRIPTEN_SINGLE_THREAD_WORKER_GATE.md): validates the no-pthreads synchronization branch and clean worker-start rejection.
- [62 — Block-scoped Panic Any](62_BLOCK_SCOPED_PANIC_ANY.md): defines and validates `#panic.#target.any`, caller-chain precedence, explicit PANIC and child-task VM recovery.
- [63 — DOM Construction and Mutation API](63_DOM_MUTATION_API.md): extends the Lexbor-backed DOM module with node creation, tree mutations, attribute/data/property writes and explicit document ownership semantics.
- [64 — DOM DocumentFragment](64_DOM_DOCUMENT_FRAGMENT.md): adds `dom.createFrag(page)` for detached subtree construction and ordered fragment insertion.
- [40 — Windows FlatSync / FlatThread Audit](40_WINDOWS_FLAT_SYNC_AUDIT.md): audits SRWLOCK/condition-variable semantics and FlatThread create/join ownership; adds a portable FlatThread contract regression while keeping real Windows validation open.
- [55 — FGIR Native-Unit Storage Audit](55_FGIR_NATIVE_UNIT_STORAGE_AUDIT.md): audits the remaining fixed `FgirProgram.native_units[32]` table and selects an isolated dynamic outer-table conversion.
- [56 — FGIR Dynamic Native-Unit Storage](56_FGIR_DYNAMIC_NATIVE_UNIT_STORAGE.md): converts only the outer native-unit table to owned dynamic storage and preserves inline per-unit declarations.
- [41 — VM Worker ASan/UBSan Regression](41_VM_WORKER_ASAN_REGRESSION.md): extends sanitizer validation to the multi-Channel worker/shutdown stress test and Emscripten single-thread rejection path; records the focused FLVM 1–55 sweep and remaining grouped-run timeout.
- [Native module thread capabilities](../doc_src/NATIVE_THREAD_CAPABILITIES.md): enum-ul de capabilități, declarația pluginurilor, fallback-ul conservator și enforcement-ul worker-mode.

- [61 — FLEditor WASM Task/Channel Runner](61_FLEDITOR_WASM_TASK_CHANNEL_RUNNER.md): wires the existing cooperative FlatRuntime task bridge into the browser WASM run API and adds runnable FLEditor examples for inventory transactions, rollback and task results.

## Reguli pentru continuarea lucrului

1. Citește acest director înainte de orice modificare majoră la Task Runtime.
2. Verifică sursele curente; documentele descriu intenția și constatările la momentul arhivei indicate, nu înlocuiesc auditul codului actual.
3. Păstrează API-urile și semantica marcate ca decizii agreate. Orice schimbare trebuie documentată aici.
4. Nu marca o fază completă doar pe baza build-ului: sunt necesare teste pozitive, negative, panic/cleanup și verificări de resurse.
5. Testele funcționale se integrează în `test/cases/` și se execută prin `bin/run_tests`.
6. Nu introduce Python-based tests sau runner separat.
7. Nu implementa worker threads înainte ca starea VM/executor/native runtime să fie izolată per execuție.

**Baseline inițial:** `flatL-main-251-task-runtime-foundation.zip`, derivată din `flatL-main(57).zip`.

**Ultima arhivă:** `flatL-main-354-dom-set-html.zip`.

**Ultimul checkpoint validat de utilizator:** arhiva 351 Task/Channel WASM este validată prin `make fleditor-wasm-test` în Termux și prin rularea manuală a celor trei exemple în browser. Arhiva 352 implementează block-scoped panic `.any`, cu fallback pe caller chain și test în child-task VM. Validarea focalizată este 9/9 pentru `panic[1-9]` și 1/1 pentru `flvm[57]`; `make all -j2` și `make docs` trec. Suita completă nu este declarată PASS. Lucrul se oprește după etapa B, conform deciziei utilizatorului.

**Checkpoint archive 302:** queue publication/pop este integrat în runtime, cu `try_pop` nonblocking, eliminare FIFO a ID-urilor pentru WAITING/cancel și claim atomic sub `scheduler_mutex`. Channel folosește acum `FlatMutex`. `flvm[39]` validează accesul concurent la un Channel comun (8 thread-uri × 2.000 set/read), iar testele 037/039 trec sub ThreadSanitizer. Worker pool-ul este introdus în arhiva 305.


**Checkpoint archive 303:** request-only cancellation is published under `scheduler_mutex`; the scheduler cleans unowned requests and the current dispatch owner cleans RUNNING tasks after `flat_vm_step()` returns. `flvm[40]` verifies CREATED/RUNNABLE/WAITING/RUNNING, idempotency and terminal-result precedence.

**Checkpoint archive 304:** adds `flat_runtime_shutdown_request()`, lifecycle/status queries, DRAIN admission closure, CANCEL of all nonterminal tasks, blocked-DRAIN detection, and DRAIN-to-CANCEL escalation. `flvm[41]` verifies cooperative lifecycle behavior. Archive 305 subsequently adds the actual worker pool and join.


**Checkpoint archive 305:** `flat_runtime_workers_start()` creează pool-ul fix, iar `flat_runtime_workers_join()` cere lifecycle STOPPED. Worker turns folosesc schedulerul existent cu `schedule_active` ca numărător; apelul public `flat_runtime_schedule()` rămâne exclusiv și este respins cât timp pool-ul este activ. Native worker mode respinge UNSUPPORTED la dispatch; task/channel sunt SERIALIZED. Vezi `17_RUNTIME_WORKER_POOL.md`.



**Checkpoint archive 306:** `flvm[43]` runs the existing `027_channel_native` program through a four-worker runtime pool. It validates two child tasks contending for one Channel transaction, parent `task.wait()` completion, rejection of suspension while holding a transaction, DRAIN/join, and teardown. The complete lifecycle is repeated 20 times in one process and the same 20-run test passes under GCC ThreadSanitizer. This is a focused regression, not a general worker-mode thread-safety certification. See `18_WORKER_TASK_WAIT_CHANNEL_AUDIT.md`.


**Checkpoint archive 307:** `flvm[44]` verifies a four-task wait chain, cycle rejection, cancellation of a middle waiter (waking its dependent), and cancellation/cleanup of a task parked on Channel contention while its transaction owner is cancelled. `flvm[40-44]` passed individually; the dedicated test passed 100 consecutive plain runs and six GCC ThreadSanitizer runs. No runtime semantics changed. Full suite (1179 discovered), ASan/UBSan, Windows workers and actual Emscripten linking remain unvalidated. See `19_WORKER_WAIT_GRAPH_CANCELLATION_AUDIT.md`.


**Checkpoint archive 308:** fixes the four test cases reported failing by the user on Termux. `types[108]` now links `flat_locale.c`; `types[72]` inserts malformed data at the SCHM/JBMX boundary rather than corrupting BPAR; `flvm[38]` uses a DOM gate hold point for a stable RUNNING-state check; `flvm[40]` has a 30-second mobile-friendly deadline. All four focused tests pass on the available POSIX host; the exact Termux failures for `flvm[38]` and `[40]` still require confirmation with verbose output on device. No runtime behavior changed. See `20_TERMUX_TEST_PORTABILITY.md`.


## Worker HTTP/DOM checkpoint — archive 309

- [x] Exercise 32 real libcurl GET requests from 4 FlatRuntime workers against an ephemeral loopback server; assert response status/body and observe at least two in-flight requests (four observed).
- [x] Ensure the HTTP test bypasses `FLAT_HTTP_MOCK_FILE` and relies on worker-start manifest preflight for `flat_http_prepare()`.
- [x] Execute 48 independent DOM VM/document lifecycles under 4 workers with per-task content assertions.
- [x] Cancel a task while its DOM native invocation is blocked at the serialized module gate; verify safe-point cancellation, VM teardown, worker join and destruction.
- [x] Preserve `dom` as `PENDING_THREAD_SAFETY_VALIDATION` with effective `SERIALIZED` policy.
- [ ] Add plugin loading/preparation race tests under concurrent worker startup and post-start admission.
- [ ] Add deterministic worker creation failure injection for each partial-start rollback point.
- [ ] Validate Windows worker execution, final TSAN/ASan/UBSan matrix and actual Emscripten build.

**Archive 309 result:** `flvm[45]` and `[46]` pass; each dedicated test binary passed 10 consecutive executions. The HTTP loopback server observed four simultaneous connections. HTTP and DOM each passed 10/10 final GCC TSAN runs; HTTP ASan/UBSan passed, while DOM ASan/UBSan compilation exceeded the command limit. DOM remains serialized; no capability promotion is made. Every selected regression `flvm[38-46]` passed individually; the grouped run hit the execution limit. See `21_WORKER_HTTP_DOM_LIFECYCLE_AUDIT.md`.


**Checkpoint archive 310:** corrects stale assertions in `flvm[34-35]`: `task` and `channel` are `SERIALIZED`, not `UNSUPPORTED`. The exec-case runner now includes the captured output tail on unexpected exit, so Termux failures identify the failing stage. `flvm[34-35]` and the previously failing `[38]`, `[40]`, `[42]`, `[43]`, `[45]`, `[46]` pass individually on the available POSIX host. The six Termux-specific failures remain unverified pending the improved diagnostics from the device; no runtime behavior changed. See `22_TERMUX_WORKER_FAILURE_DIAGNOSTICS.md`.


**Checkpoint archive 311:** fixes shell-capture diagnostics left incomplete in archive 310. POSIX capture redirects stderr for the whole compound command, drains all output and retains the final 4095 bytes; Windows capture also retains the final bytes. New `port[2-3]` regressions validate early-stage stderr and large-output tail retention. All `flvm[34-46]` cases passed individually on the available POSIX host, but the full grouped invocation did not complete within the command limit. Termux confirmation is still required. See `23_SHELL_CAPTURE_DIAGNOSTICS.md`.


**Checkpoint archive 312:** `flvm[38]`, `[40]`, `[42]`, `[43]`, `[44]`, `[45]`, and `[46]` now emit explicit stderr markers before each command stage (FlatL compile, C harness build, harness execution). This is diagnostic-only: no runtime behavior changed. On the available POSIX host, `make all -j2`, `port[2-4]`, and all seven focused worker tests passed individually. The Termux rerun remains pending; the last stage marker should distinguish compilation from harness/runtime failure. See `24_WORKER_TEST_STAGE_DIAGNOSTICS.md`.


**Checkpoint archive 313:** the Termux output shows the seven failures reach `[stage:run-harness]` but emit no harness diagnostic. POSIX `pclose()` signal termination was being collapsed to exit code 1. The shell-capture API now returns negative signal numbers and the test runner reports `terminated by signal N`; this is diagnostic only and does not change runtime code. Termux rerun remains required. See `25_SIGNAL_AWARE_TEST_PROCESS_STATUS.md`.

- [31 — FlatThread Join Ownership Audit](31_FLAT_THREAD_JOIN_OWNERSHIP.md): contractul join pe POSIX/Windows, ownership exclusiv al join-ului per runtime și regresia pentru apelanți concurenți.
- [32 — Worker Multi-Graph Cancellation Stress](32_WORKER_MULTI_GRAPH_CANCELLATION_STRESS.md): stres cu grafuri independente, cereri simultane de anulare și DRAIN/cleanup în worker pool.

- [33 — Worker Channel / Shutdown Stress](33_WORKER_CHANNEL_SHUTDOWN_STRESS.md): four-Channel multi-writer transactions, concurrent waiter cancellation, DRAIN completion and post-join CANCEL cleanup.

- [34 — Worker DAG Cancellation Stress](34_WORKER_DAG_CANCELLATION_STRESS.md): converging wait-graph DAGs with deterministic topology variants and simultaneous cancellation.
- [35 — Worker DAG Staged Cancellation Stress](35_WORKER_DAG_STAGED_CANCELLATION.md): deeper fan-out/fan-in graphs with cancellation staged at RUNNING and WAITING states under real workers.

- [35 — Worker DAG Staged Cancellation Stress](35_WORKER_DAG_STAGED_CANCELLATION.md): deeper fan-out/fan-in graphs with cancellation staged at RUNNING and WAITING states under real workers.

- [36 — Worker Seeded DAG Stress](36_WORKER_SEEDED_DAG_STRESS.md): topologii DAG reproductibile cu seed fix, anulări concurente și verificarea cleanup-ului după DRAIN.

- [38 — Worker Failure Isolation and Lifecycle Soak](38_WORKER_FAILURE_ISOLATION_SOAK.md): 100 runtime lifecycle cycles per run, with task-local failures isolated from healthy workers and complete DRAIN/join/cleanup checks.

- [44 — Worker Lifecycle Sanitizer Sweep](44_WORKER_LIFECYCLE_SANITIZER_SWEEP.md): completed `flvm[1-55]` coverage in three groups and extends ASan/UBSan/TSAN validation across worker cancellation, mixed shutdown and failure-isolation harnesses.
- [45 — WASM API and Lifecycle Testing](45_WASM_API_LIFECYCLE_TESTING.md): Node.js headless coverage of the exported WASM C API, repeated execution lifecycle, and explicit `fs.mkdir()` setup before CSV reading.

**Checkpoint arhiva 335:** adaugă testarea headless a API-ului WASM și lifecycle-ului în Node.js, precum și un scenariu FlatL `fs.mkdir()` + `file` + `csv`; toate cele nouă teste Node.js trec cu artefactele WASM din snapshot. Rebuild-ul prin Emscripten SDK a fost ulterior confirmat de utilizator pe Termux în arhivele 336–339. Arhiva 334 confirmă PASS pentru toate cele 55 de cazuri `flvm` în trei invocări grupate și validare ASan/UBSan pentru testele worker selectate, plus TSAN pentru `flvm[52]`. O singură invocare completă `flvm[1-55]` și execuția workerilor pe Windows rămân porți deschise.

- [46 — WASM Node.js Test Runner Fix](46_WASM_NODE_RUNNER_FIX.md): separă `WASM_NODE` de selecția `NODE` a serverului FLEditor și repară invocarea testelor `.mjs`.
- [47 — WASM Limits, Recovery and Memory](47_WASM_LIMITS_RECOVERY_MEMORY.md): testează jump/step limits, panic recovery, creșterea memoriei WASM și consemnează working set-ul mare al compilării FGIR.

**Checkpoint arhiva 337:** extinde testele WASM în Node.js cu jump/step limits, panic recovery, creștere de memorie și 100 de execuții repetate; toate cele zece teste trec pe artefactele WASM arhivate. Snapshot-ul arhivat a măsurat 845 MB high-water. După rebuild pe Termux, utilizatorul a raportat 1,263,337,472 bytes, stabili după warm-up. Diferența dintre artefactele WASM este consemnată în planul 47; layout-ul fix `FgirProgram` rămâne cauza structurală investigată.

- [48 — FGIR Linker Peak Memory](48_FGIR_LINKER_PEAK_MEMORY.md): traces the three full-size FGIR allocations on the no-import WASM link path and removes redundant scratch programs without changing language limits.

**Checkpoint arhiva 338:** reduce peak-ul linkerului pentru programe fără importuri de la trei `FgirProgram` simultane la unul, iar pentru programe cu importuri reutilizează scratch-ul și elimină a treia structură. Utilizatorul a confirmat ulterior pe Termux că arhivele 339 și 338 trec testele WASM, iar FGIR dinamic din arhiva 339 reduce compile high-water la 42.729.472 bytes.

- [49 — FGIR Dynamic Storage](49_FGIR_DYNAMIC_STORAGE.md): dynamic capacity-managed FGIR block and instruction arrays, deep-clone ownership, lifecycle cleanup, and WASM memory regression coverage.
- [50 — FGIR Parser Storage Audit](50_FGIR_PARSER_STORAGE_AUDIT.md): confirms there is no separate AST, measures remaining fixed FGIR metadata, and selects dynamic outer schema storage as the next isolated change.
- [51 — FGIR Dynamic Schema Storage](51_FGIR_DYNAMIC_SCHEMA_STORAGE.md): dynamic capacity-managed outer schema declaration array, preserving inline schema fields and the 64-schema language limit.
- [54 — Native Test Harness Object Cache](54_NATIVE_TEST_HARNESS_OBJECT_CACHE.md): reuses configuration-keyed FB/VM/platform object files across native C integration harnesses without reducing test coverage.
- [52 — FGIR Function Storage Audit](52_FGIR_FUNCTION_STORAGE_AUDIT.md): audits the remaining fixed function metadata table, confirms inline-only function records and safe value-copy semantics, and documents parser `def`-sugar pointer invalidation risks before dynamic conversion.

**Checkpoint archive 342:** audit-only follow-up after the user's complete archive 341 validation (1,156/1,156 native tests; WASM API and aggregate WASM suite passed with 42,729,472-byte compile high-water). Measures the fixed function table at 1,008,640 bytes and identifies parser/linker reserve points. No C/runtime changes. See `plans/52_FGIR_FUNCTION_STORAGE_AUDIT.md`.
- [57 — FGIR Export Storage Audit](57_FGIR_EXPORT_STORAGE_AUDIT.md): measures and audits the fixed 256-entry export manifest, its `.fmod` consumption boundary, and the isolated dynamic-storage conversion.
- [58 — FGIR Dynamic Export Storage](58_FGIR_DYNAMIC_EXPORT_STORAGE.md): converts the fixed export manifest to owned geometric storage while preserving `.fmod` behavior and the 256-symbol limit.
- [59 — FGIR Optional-Native Storage Audit](59_FGIR_OPTIONAL_NATIVE_STORAGE_AUDIT.md): audits the remaining fixed FGIR optional-native union, its relationship to per-unit declarations and the separate bytecode FNAT manifest, and selects an isolated dynamic conversion.
