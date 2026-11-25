# Task Runtime — stadiul implementării

**Ultima actualizare:** 2026-11-25  
**Arhivă de referință:** `flatL-main-251-task-runtime-foundation.zip`  
**Baseline de proiect:** `flatL-main(57).zip`

## Ce este prezent în arhiva 251

- `include/flat_runtime_log.h`
- `src/common/flat_runtime_log.c`
- opțiunile CLI de logging task/channel și buffering limitat;
- documentul `doc_src/TASK_RUNTIME_AUDIT.md`;
- teste flvm pentru opțiunile de logging, validarea bufferului și filtrare/output/buffering.

Logger-ul este central și opt-in. Logging-ul dezactivat nu trebuie să formateze sau să aloce evenimente. Categoriile task/channel sunt independente. Output-ul este live în stdout; bufferul opțional este limitat și raportează evenimentele eliminate când se umple.

**Important:** în această etapă nu există încă task-uri sau Channel-uri și, prin urmare, nu există producători reali de evenimente lifecycle/Channel. Logger-ul este infrastructură pregătită pentru fazele următoare.

## Verificări raportate pentru arhiva 251

- `make all` — PASS.
- `make docs` — PASS.
- `bin/run_tests flvm -v` — 6/6 PASS.
- `make test` — a fost întrerupt prin timeout după aproximativ 160 din 1097 teste selectate; nu s-a raportat un eșec înainte de întrerupere, dar suita completă NU este declarată PASS.

Verificările cross-platform (Termux, MSYS2 UCRT64, WSL Ubuntu) menționate anterior de utilizator se referă la arhiva de bază 57, nu constituie o validare a arhivei 251.

## Ce NU este implementat încă

- `FlatTask` și task lifecycle;
- `task.spawn()` / `task.wait()`;
- scheduler cooperativ și yield/resume;
- VCX separat per task;
- panic wildcard `.any` în forma `#panic.#worker.any`;
- Channel pool și `task.channel()`;
- `channel.set()` / `channel.var()`;
- mutex per Channel;
- `lock` / `unlock` / `revert` și tranzacții;
- `task.log()` / `channel.log()`;
- worker pool pthread și execuție paralelă;
- integrare UI thread.

## Următorul pas

**F0.3 — Execution Context Refactoring**:

1. Introducerea unui context explicit pentru executor.
2. Mutarea `g_bind` și a frame stack-ului în acel context.
3. Mutarea bookkeeping-ului call-instruction din VM în contextul execuției.
4. Scoped VCX active runtime și call-instruction resolver.
5. Panic drain/cleanup exclusiv în contextul execuției care a eșuat.
6. Teste de regresie pentru nested calls, unique/global executor și panic unwind.

Nu începe schedulerul înainte ca ownership-ul și cleanup-ul acestor contexte să fie demonstrate prin teste.
