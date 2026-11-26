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

**Ultima arhivă:** `flatL-main-308-termux-test-portability.zip`.

**Ultimul checkpoint:** arhiva 305 adaugă primul worker pool pthread deținut de runtime: startup cu barrier/rollback, execuție concurentă a quantum-urilor, wait pe condition variable cu wake epoch, integrare DRAIN/CANCEL și join. `flvm[42]` validează 4 workers și 96 task-uri, inclusiv admission preflight pentru task-uri noi. 100 de execuții plain consecutive au trecut; 5 execuții TSAN au trecut pe forma anterioară a testului, dar rebuild-ul TSAN pentru forma finală nu a putut fi terminat în limita de execuție. Aceasta nu certifică încă întregul set de native modules/dependențe în worker mode. Arhiva 304 adaugă lifecycle-ul DRAIN/CANCEL, iar arhiva 303 adaugă `flat_runtime_task_cancel_request()` și cleanup la safe point. Arhiva 302 conectează MPMC ready queue la `FlatRuntime` și convertește gate-ul Channel la `FlatMutex`; `flat_vm_step()` rămâne în afara mutex-ului. Arhiva 301 repară fallback-ul de link pentru `flvm[32]` și completează sursele runtime/native din `Makefile.wasm`, dar build-ul Emscripten real trebuie confirmat într-un mediu cu `emcc`. Lexbor rămâne `PENDING_THREAD_SAFETY_VALIDATION`, cu `SERIALIZED` drept fallback. Orice decizie nouă se consemnează în `01_DECISIONS.md` și roadmap înainte de implementare.


**Checkpoint archive 302:** queue publication/pop este integrat în runtime, cu `try_pop` nonblocking, eliminare FIFO a ID-urilor pentru WAITING/cancel și claim atomic sub `scheduler_mutex`. Channel folosește acum `FlatMutex`. `flvm[39]` validează accesul concurent la un Channel comun (8 thread-uri × 2.000 set/read), iar testele 037/039 trec sub ThreadSanitizer. Worker pool-ul este introdus în arhiva 305.


**Checkpoint archive 303:** request-only cancellation is published under `scheduler_mutex`; the scheduler cleans unowned requests and the current dispatch owner cleans RUNNING tasks after `flat_vm_step()` returns. `flvm[40]` verifies CREATED/RUNNABLE/WAITING/RUNNING, idempotency and terminal-result precedence.

**Checkpoint archive 304:** adds `flat_runtime_shutdown_request()`, lifecycle/status queries, DRAIN admission closure, CANCEL of all nonterminal tasks, blocked-DRAIN detection, and DRAIN-to-CANCEL escalation. `flvm[41]` verifies cooperative lifecycle behavior. Archive 305 subsequently adds the actual worker pool and join.


**Checkpoint archive 305:** `flat_runtime_workers_start()` creează pool-ul fix, iar `flat_runtime_workers_join()` cere lifecycle STOPPED. Worker turns folosesc schedulerul existent cu `schedule_active` ca numărător; apelul public `flat_runtime_schedule()` rămâne exclusiv și este respins cât timp pool-ul este activ. Native worker mode respinge UNSUPPORTED la dispatch; task/channel sunt SERIALIZED. Vezi `17_RUNTIME_WORKER_POOL.md`.



**Checkpoint archive 306:** `flvm[43]` runs the existing `027_channel_native` program through a four-worker runtime pool. It validates two child tasks contending for one Channel transaction, parent `task.wait()` completion, rejection of suspension while holding a transaction, DRAIN/join, and teardown. The complete lifecycle is repeated 20 times in one process and the same 20-run test passes under GCC ThreadSanitizer. This is a focused regression, not a general worker-mode thread-safety certification. See `18_WORKER_TASK_WAIT_CHANNEL_AUDIT.md`.


**Checkpoint archive 307:** `flvm[44]` verifies a four-task wait chain, cycle rejection, cancellation of a middle waiter (waking its dependent), and cancellation/cleanup of a task parked on Channel contention while its transaction owner is cancelled. `flvm[40-44]` passed individually; the dedicated test passed 100 consecutive plain runs and six GCC ThreadSanitizer runs. No runtime semantics changed. Full suite (1179 discovered), ASan/UBSan, Windows workers and actual Emscripten linking remain unvalidated. See `19_WORKER_WAIT_GRAPH_CANCELLATION_AUDIT.md`.


**Checkpoint archive 308:** fixes the four test cases reported failing by the user on Termux. `types[108]` now links `flat_locale.c`; `types[72]` inserts malformed data at the SCHM/JBMX boundary rather than corrupting BPAR; `flvm[38]` uses a DOM gate hold point for a stable RUNNING-state check; `flvm[40]` has a 30-second mobile-friendly deadline. All four focused tests pass on the available POSIX host; the exact Termux failures for `flvm[38]` and `[40]` still require confirmation with verbose output on device. No runtime behavior changed. See `20_TERMUX_TEST_PORTABILITY.md`.
