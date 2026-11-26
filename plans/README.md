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

**Ultima arhivă:** `flatL-main-298-native-sync-invocation-gates.zip`.

**Ultimul checkpoint:** arhiva 298 adaugă `FlatMutex` (POSIX pthread mutex / Windows SRWLOCK / Emscripten single-thread no-op), câte un invocation gate per modul, aplicarea policy-ului `SERIALIZED`/`PENDING` la limita `flat_native_invoke()` și aceeași protecție pentru `flat_dom_runtime_destroy()`. `THREAD_SAFE` rămâne fără gate; `UNSUPPORTED` continuă să fie permis în runtime-ul cooperativ și este respins numai de worker preflight. Worker pool-ul, scheduler synchronization, cancellation request și shutdown-ul nu sunt implementate. Lexbor rămâne `PENDING_THREAD_SAFETY_VALIDATION`; DOM gate-ul implementează fallback-ul `SERIALIZED`, fără să schimbe încă statutul de audit. Orice decizie nouă se consemnează în `01_DECISIONS.md` și roadmap înainte de implementare.
