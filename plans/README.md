# FlatL — Task Runtime & Channels plans

Acest director păstrează deciziile de arhitectură, direcția și ordinea de implementare pentru Task Runtime, Channel și viitoarea integrare UI. Scopul este ca lucrul să poată continua într-o conversație nouă fără să depindă de istoricul chatului.

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
- [Native module thread capabilities](../doc_src/NATIVE_THREAD_CAPABILITIES.md): enum-ul de capabilități, declarația pluginurilor, fallback-ul conservator și enforcement-ul worker-mode.

## Reguli pentru continuarea lucrului

1. Citește acest director înainte de orice modificare majoră la Task Runtime.
2. Verifică sursele curente; documentele descriu intenția și constatările la momentul arhivei indicate, nu înlocuiesc auditul codului actual.
3. Păstrează API-urile și semantica marcate ca decizii agreate. Orice schimbare trebuie documentată aici.
4. Nu marca o fază completă doar pe baza build-ului: sunt necesare teste pozitive, negative, panic/cleanup și verificări de resurse.
5. Testele funcționale se integrează în `test/cases/` și se execută prin `bin/run_tests`.
6. Nu introduce Python-based tests sau runner separat.
7. Nu implementa worker threads înainte ca starea VM/executor/native runtime să fie izolată per execuție.

**Baseline inițial:** `flatL-main-251-task-runtime-foundation.zip`, derivată din `flatL-main(57).zip`.

**Ultima arhivă:** `flatL-main-313-signal-aware-shell-capture.zip`.

**Ultimul checkpoint:** arhiva 305 adaugă primul worker pool pthread deținut de runtime: startup cu barrier/rollback, execuție concurentă a quantum-urilor, wait pe condition variable cu wake epoch, integrare DRAIN/CANCEL și join. `flvm[42]` validează 4 workers și 96 task-uri, inclusiv admission preflight pentru task-uri noi. 100 de execuții plain consecutive au trecut; 5 execuții TSAN au trecut pe forma anterioară a testului, dar rebuild-ul TSAN pentru forma finală nu a putut fi terminat în limita de execuție. Aceasta nu certifică încă întregul set de native modules/dependențe în worker mode. Arhiva 304 adaugă lifecycle-ul DRAIN/CANCEL, iar arhiva 303 adaugă `flat_runtime_task_cancel_request()` și cleanup la safe point. Arhiva 302 conectează MPMC ready queue la `FlatRuntime` și convertește gate-ul Channel la `FlatMutex`; `flat_vm_step()` rămâne în afara mutex-ului. Arhiva 301 repară fallback-ul de link pentru `flvm[32]` și completează sursele runtime/native din `Makefile.wasm`, dar build-ul Emscripten real trebuie confirmat într-un mediu cu `emcc`. Lexbor rămâne `PENDING_THREAD_SAFETY_VALIDATION`, cu `SERIALIZED` drept fallback. Orice decizie nouă se consemnează în `01_DECISIONS.md` și roadmap înainte de implementare.


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
