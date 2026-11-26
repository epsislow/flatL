# FlatL — Task Runtime & Channels plans

Acest director păstrează deciziile de arhitectură, direcția și ordinea de implementare pentru Task Runtime, Channel și viitoarea integrare UI. Scopul este ca lucrul să poată continua într-o conversație nouă fără să depindă de istoricul chatului.

## Documente

- [01 — Decizii și contracte de design](01_DECISIONS.md): ce am agreat explicit și ce nu trebuie schimbat accidental.
- [02 — Roadmap și fazare](02_ROADMAP.md): milestones v1.0, v1.5, v2.0 și v2.5, livrabile și criterii de acceptare.
- [03 — Audit inițial al runtime-ului](03_RUNTIME_AUDIT.md): constatările tehnice din arhiva 57 și limitele arhitecturii actuale.
- [04 — Stadiul implementării](04_IMPLEMENTATION_STATUS.md): ce există în arhiva curentă, ce nu este încă implementat și următorul pas.
- [05 — Roadmap original](05_SOURCE_ROADMAP.md): copia planului inițial furnizat pentru Task Channels, păstrată pentru trasabilitate.
- [06 — Worker Pool Architecture Specification](06_WORKER_POOL_SPEC.md): inventarul ownership-ului curent și specificația propusă pentru worker pool-ul v2.0; nu reprezintă implementare pthread.
- [07 — Native Module Thread-Safety Audit](07_NATIVE_THREAD_SAFETY_AUDIT.md): auditul stării globale a modulelor native, limitele de ownership și politica de capabilități pentru pluginuri.
- [08 — External Dependency Concurrency Audit](08_EXTERNAL_DEPENDENCY_AUDIT.md): evaluarea documentației upstream și a modului concret de utilizare PCRE2, Lexbor și libcurl în FlatL.
- [09 — Cancellation Request and Worker Shutdown Specification](09_CANCELLATION_SHUTDOWN_SPEC.md): contractul viitor pentru anulare request-only, cleanup deținut de worker, DRAIN/CANCEL și join; specificație, nu implementare pthread.
- [10 — Native Synchronization and Invocation Gates](10_NATIVE_SYNC_GATES.md): mutex abstraction multiplatformă, gates per modul, integrarea policy în dispatcher și protecția DOM teardown.
- [11 — Scheduler Synchronization Foundation](11_SCHEDULER_SYNC_FOUNDATION.md): primitive multiplatformă și MPMC ready queue introduse în arhiva 299; arhiva 302 integrează coada în schedulerul cooperativ.
- [12 — Runtime Scheduler Mutex Integration](12_RUNTIME_SCHEDULER_MUTEX.md): integrarea `FlatMutex` în `FlatRuntime`, sincronizarea task registry/wait graph și ownership-ul dispatch; arhiva 302 completează integrarea ready queue și mutex-ul Channel; fără worker threads.
- [13 — Build Target Source Parity](13_BUILD_TARGET_PARITY.md): corecțiile de build pentru testul HTTP `pkg-config` fallback și completarea surselor comune lipsă din `Makefile.wasm`.
- [14 — Ready Queue Integration and Channel Mutex Conversion](14_READY_QUEUE_CHANNEL_MUTEX_INTEGRATION.md): integrarea cozii FIFO/MPMC în schedulerul cooperativ și înlocuirea spin gate-ului Channel cu `FlatMutex`.
- [15 — Request-only Cancellation Runtime](15_CANCELLATION_REQUEST_RUNTIME.md): API request-only, safe-point cleanup deținut de scheduler și limitele pre-worker.
- [Native module thread capabilities](../doc_src/NATIVE_THREAD_CAPABILITIES.md): enum-ul de capabilități, declarația pluginurilor, fallback-ul conservator și preflight-ul worker-mode.

## Reguli pentru continuarea lucrului

1. Citește acest director înainte de orice modificare majoră la Task Runtime.
2. Verifică sursele curente; documentele descriu intenția și constatările la momentul arhivei indicate, nu înlocuiesc auditul codului actual.
3. Păstrează API-urile și semantica marcate ca decizii agreate. Orice schimbare trebuie documentată aici.
4. Nu marca o fază completă doar pe baza build-ului: sunt necesare teste pozitive, negative, panic/cleanup și verificări de resurse.
5. Testele funcționale se integrează în `test/cases/` și se execută prin `bin/run_tests`.
6. Nu introduce Python-based tests sau runner separat.
7. Nu implementa worker threads înainte ca starea VM/executor/native runtime să fie izolată per execuție.

**Baseline inițial:** `flatL-main-251-task-runtime-foundation.zip`, derivată din `flatL-main(57).zip`.

**Ultima arhivă:** `flatL-main-303-cancellation-request-runtime.zip`.

**Ultimul checkpoint:** arhiva 303 adaugă `flat_runtime_task_cancel_request()` și cleanup deținut de scheduler pentru cererile de anulare, inclusiv observarea anulării RUNNING numai după revenirea din `flat_vm_step()`. Arhiva 301 repară fallback-ul de link pentru testul `flvm[32]` când `pkg-config` nu poate fi executat și sincronizează `Makefile.wasm` cu sursele runtime/native introduse până la arhiva 300. Build-ul Emscripten real trebuie reconfirmat într-un mediu cu `emcc`; arhiva 301 a verificat dry-run-ul surselor. Arhiva 300 înlocuiește atomic spin gate-ul `FlatRuntime` cu `FlatMutex`, sincronizează task registry/wait graph și integrează `FlatTaskDispatchState` în schedulerul cooperativ. Arhiva 302 conectează MPMC ready queue la `FlatRuntime`, păstrează prioritatea tranzacțiilor Channel și convertește gate-ul fiecărui Channel la `FlatMutex`; `flat_vm_step()` rămâne în afara mutex-ului. Un singur thread poate conduce în continuare schedulerul, iar apelurile concurente de schedule sunt respinse. Arhiva 303 implementează request-only cancellation, dar nu worker cleanup queue, DRAIN/CANCEL sau join. Lexbor rămâne `PENDING_THREAD_SAFETY_VALIDATION`, cu `SERIALIZED` drept fallback. Orice decizie nouă se consemnează în `01_DECISIONS.md` și roadmap înainte de implementare.


**Checkpoint archive 302:** queue publication/pop este integrat în runtime, cu `try_pop` nonblocking, eliminare FIFO a ID-urilor pentru WAITING/cancel și claim atomic sub `scheduler_mutex`. Channel folosește acum `FlatMutex`. `flvm[39]` validează accesul concurent la un Channel comun (8 thread-uri × 2.000 set/read), iar testele 037/039 trec sub ThreadSanitizer. Nu există încă worker pool sau shutdown/join.


**Checkpoint archive 303:** request-only cancellation is published under `scheduler_mutex`; the scheduler cleans unowned requests and the current dispatch owner cleans RUNNING tasks after `flat_vm_step()` returns. `flvm[40]` verifies CREATED/RUNNABLE/WAITING/RUNNING, idempotency and terminal-result precedence. No worker threads or shutdown/join API exist yet.
