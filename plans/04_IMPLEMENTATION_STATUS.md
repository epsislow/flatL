# Task Runtime — stadiul implementării

**Ultima actualizare:** 2026-11-25  
**Arhivă de referință:** `flatL-main-252-task-runtime-plans.zip`
**Arhivă curentă:** `flatL-main-253-task-runtime-executor-context.zip`  
**Baseline de proiect:** `flatL-main(57).zip`

## Ce este prezent în arhiva 252 (bază)

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
- `bin/run_tests flvm -v` — 7/7 PASS (include testul nou `007_executor_context_isolation`).
- `make test` — a fost întrerupt prin timeout după aproximativ 160 din 1097 teste selectate; nu s-a raportat un eșec înainte de întrerupere, dar suita completă NU este declarată PASS.

Verificările cross-platform (Termux, MSYS2 UCRT64, WSL Ubuntu) menționate anterior de utilizator se referă la arhiva de bază 57, nu constituie o validare a arhivei 251.

## F0.3 — progres curent în arhiva 253

Prima sub-etapă a F0.3 este implementată: **executor context per VM**.

- `FlatExecutorContext` deține acum binding-ul VM și stack-ul de frame-uri executor.
- `VmState` deține propriul `FlatExecutorContext`.
- `FlatNativeCtx` primește pointer-ul către contextul executorului VM curent.
- Native executor operations (`unique` / `global`, begin / end) folosesc contextul explicit; nu mai rebindează o stare globală.
- Panic drain și unwind pentru frame-urile executorului primesc contextul VM curent.
- `g_bind`, `g_bind_ok`, `g_frames` și `g_frame_sp` au fost eliminate din `flat_executor.c`.

**Încă nefinalizat în F0.3:** bookkeeping-ul call-instruction din `vm.c` și `g_vcx_active` / `g_call_instr_resolver` din `flat_vcx.c` sunt încă globale. `flat_vcx_call_instr_ctx()` și stiva asociată rămân dependente de acel mecanism. Prin urmare, această schimbare elimină doar prima familie de globale și NU declară VM-ul reentrant, task-safe sau thread-safe.

### Verificări pentru arhiva 253

- `make all -j2` — PASS.
- `bin/run_tests flvm -v` — 7/7 PASS (include testul nou `007_executor_context_isolation`).
- `bin/run_tests func -v` — 37/37 PASS (include nested/native executor și panic unwind).
- `bin/run_tests fmod -v` — 18/18 PASS.
- Suita completă nu a fost rulată în această etapă.

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

1. [x] Introducerea unui context explicit pentru executor.
2. [x] Mutarea binding-ului și a frame stack-ului executor în acel context.
3. [ ] Mutarea bookkeeping-ului call-instruction din VM în contextul execuției.
4. [ ] Scoped VCX active runtime și call-instruction resolver.
5. [~] Panic drain pentru frame-urile executorului este scoped; drain-ul call-instruction încă folosește starea VM globală.
6. [x] Regresie pentru nested calls, unique/global executor și panic unwind prin suitele existente `func` / `fmod`.

Nu începe schedulerul înainte ca ownership-ul și cleanup-ul acestor contexte să fie demonstrate prin teste.
