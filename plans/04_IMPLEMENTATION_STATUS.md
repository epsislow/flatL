# Task Runtime — stadiul implementării

**Ultima actualizare:** 2026-11-28  
**Arhivă de referință:** `flatL-main-351-fleditor-wasm-task-channel.zip`
**Arhivă curentă:** `flatL-main-352-panic-block-any.zip`  
**Baseline de proiect:** `flatL-main(57).zip`


## Current checkpoint — archive 352 (2026-11-28)

- Archive 351 FLEditor WASM Task/Channel integration is validated by the user: `make fleditor-wasm-test` passed on Termux, including the Node.js WASM Task/Channel smoke for inventory, rollback and task results; the user also ran all three examples successfully in the FLEditor browser UI.
- Archive 352 implements general block-scoped panic wildcard handlers (`#panic.#target.any`). Resolution order is exact fault-site handler, faulting block `.any`, caller-block `.any` handlers from nearest outward, then global `#panic.any`.
- Added `panic[7-9]` for current-block native panic, nested-call caller recovery and explicit `PANIC`; added `flvm[57]` to validate recovery inside a spawned task VM.
- Focused validation: `make all -j2` PASS; `bin/run_tests 'panic[1-9]' -v -e` PASS (9/9); `bin/run_tests 'flvm[57]' -v -e` PASS (1/1). Full default suite is not claimed.
- Planned work is paused after stage B as requested. No UI Runtime or further worker-pool expansion is started in this checkpoint.

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

## F0.3 — progres curent în arhiva 254

Refactorul structural F0.3 este implementat: **execution context per VM**.

- `FlatExecutorContext` deține binding-ul VM, frame stack-ul executor, stack-ul call-instruction, push-call positions și `last_call_function_sp`.
- `VmState` deține propriul `FlatExecutorContext`; resolver-ul call-instruction primește explicit `VmState` și nu folosește un pointer global către VM.
- `FlatNativeCtx` primește pointer-ul către executorul VM curent, VCX-ul curent și `call_instr_id` rezolvat pentru acel apel.
- Native executor operations (`unique` / `global`, begin / end) folosesc contextul explicit.
- VCX native operations folosesc `FlatNativeCtx.vcx`; fallback-ul către un runtime VCX global activ a fost eliminat.
- Panic drain, pop și teardown pentru stack-ul call-instruction sunt scoped la contextul VM curent.
- Au fost eliminate globalele `g_bind`, `g_bind_ok`, `g_frames`, `g_frame_sp`, `g_vm_call_instr`, `g_instr_stack`, `g_instr_push_call_sp`, `g_last_call_function_sp`, `g_vcx_active` și `g_call_instr_resolver`.

**Limitare importantă:** F0.3 elimină aceste globale și clarifică ownership-ul contextului, dar NU declară VM-ul reentrant, task-safe sau thread-safe. Mai există alte stări globale în runtime (de exemplu reset-ul native runtime, RNG, locale și alte subsisteme), care trebuie auditate înainte de worker pool sau execuție concurentă.

### Verificări pentru arhiva 254

- `make all -j2` — PASS.
- `bin/run_tests flvm -v -e` — 7/7 PASS.
- `bin/run_tests func[1-37] -v -e` și `func[101-106] -v -e` — 37/37 PASS cumulat.
- `bin/run_tests fmod -v -e` — 18/18 PASS.
- `bin/run_tests vcx -v -e` — 30/30 PASS.
- Suita completă (1141 teste default descoperite) nu a fost rulată în această etapă.

## Ce NU este implementat încă (după arhiva 352)

- Helper-ele de diagnostic `task.log()` / `channel.log()`; logger-ul lifecycle și opțiunile de afișare există deja.
- Validarea reală a worker pool-ului pe Windows.
- O certificare generală de thread-safety pentru toate modulele native și dependențele externe; modulele cu statut pending rămân protejate prin politica SERIALIZED.
- v2.5 UI Runtime: ownership UI thread și bridge UI ↔ task prin Channel.

Task/Channel cooperative, Channel transactions, POSIX worker pool, DRAIN/CANCEL și block-scoped panic `.any` sunt implementate. Execuția FLEditor WASM rămâne cooperativă și single-threaded.

## Stadiul curent

F0.4 — Global State Audit & Isolation rămâne închisă structural, cu limitele process-owned documentate în audit. `FlatVm` oferă stepping/yield/resume cooperativ la limite de instrucțiune. Arhivele 276–277 adaugă `FlatTask`, scheduler round-robin, wait/wakeup și cancellation. Arhiva 278 adaugă integrarea nativă inițială `task.spawn()`, `task.wait()` și `task.state()`, iar arhiva 279 adaugă transmiterea argumentelor către parametrii numiți ai block-urilor și formatul `.fb` v14. Arhiva 280 adaugă Channel registry runtime-owned, deep-copy între pool-uri, sincronizare C1, tranzacții C2 și rollback automat la terminarea task-ului. Arhiva 281 adaugă natives `channel.create/set/var/lock/unlock/revert/destroy`, transmiterea numerică a handle-urilor prin `task.spawn()`, prioritate scheduler pentru owner-ul tranzacției și testarea partajării efective între VM-uri.

**Istoricul F0.4 — Global State Audit & Isolation**:

1. [x] Inventarierea globalelor mutabile relevante pentru VM/runtime.
2. [x] Mutarea RNG-ului în `VmState` și transmiterea prin `FlatNativeCtx.rng`.
3. [x] Test de izolare pentru două instanțe RNG.
4. [ ] Clasificarea globalelor rămase după ownership: process / VM / task.
5. [x] Separarea date run state per VM.
6. [x] Separarea manifestului per VM și inițializarea registry-ului embedded la nivel de proces (lazy loading rămâne nesincronizat).
7. [x] Eliminarea modificării localei procesului și mutarea setup-ului stdio la inițializarea CLI.
8. [x] Snapshot per-VM pentru path/env și extinderea registry-ului fallback memory streams; logger-ul rămâne process-owned și necesită audit de sincronizare.

F0.3 este închis structural. F0.4 a fost continuată în arhivele 255–263; limitele process-owned rămase sunt păstrate în audit. Nu declara general reentrancy sau thread-safety.


## F0.4 — progres în arhiva 255

A fost eliminată starea RNG globală. `FlatRngState` este deținut de `VmState`, iar `FlatNativeCtx.rng` transmite pointer-ul către native calls. `math.seed`, `math.random` și `hash.randomBytes` păstrează RNG comun în interiorul aceluiași VM, dar contexte RNG diferite nu se influențează reciproc. RNG-ul nu este criptografic.

Teste adăugate: `test/cases/flvm/008_rng_context_isolation` verifică izolarea între două instanțe RNG.

Verificări pentru arhiva 255:
- `make all -j2` — PASS.
- `bin/run_tests flvm -v -e` — 8/8 PASS.
- `bin/run_tests math -v -e` — 34/34 PASS.
- `bin/run_tests hash -v -e` — 4/4 PASS.
- `bin/run_tests bytes -v -e` — a depășit limita de execuție după afișarea testului 11; nu este declarat PASS.

Auditul globalelor a identificat în plus registry/reset plugin native, date run state, path separator, environment state, memory-stream slots, runtime logger, compile panic-site emission state și WASM last-result state. Acestea nu au fost mutate în această etapă.


## F0.4 — progres în arhiva 256

Starea de configurare date/time a fost mutată din `g_date_state` într-un `FlatDateRunState` deținut de fiecare `VmState`. `FlatNativeCtx.date` transmite pointer-ul către handler-ele native. `flat_date_global_state()` a fost eliminată; `flat_date_run_begin()` inițializează acum starea VM-ului curent, inclusiv snapshot-ul valorilor `FLAT_DATE_RUN_OFFSET` și `FLAT_DATE_NOW_SEC`. `date.run_offset()` modifică doar configurația VM-ului apelant.

Test nou: `test/cases/date/082_run_state_isolation` verifică două structuri inițializate cu configurații diferite și faptul că modificarea uneia nu o afectează pe cealaltă.

Verificări pentru arhiva 256 (1143 teste default descoperite):
- `make all -j2` — PASS.
- `bin/run_tests date[1-48] -e` — 48/48 PASS.
- `bin/run_tests date[49-81] -v -e --time` — 33/33 PASS.
- `bin/run_tests date[82] -v -e` — 1/1 PASS.
- Încercarea combinată `date[1-82]` a atins limita de execuție după testul 50; toate cele 82 au fost confirmate prin cele trei rulări separate de mai sus.

### Native plugin registry — clasificare

Auditul a confirmat că `native_plugin_host.c` combină stări cu ownership diferit: `g_handlers`, flag-urile `g_loaded_*` și `g_search_dir` sunt registry/configurație de proces (cu mutații la înregistrare și încărcare), în timp ce `g_manifest` / `g_manifest_count` reprezintă permisiunile unui program/VM și nu trebuie să fie globale. `flat_native_runtime_reset()` șterge și reconstruiește handlers, resetează flag-urile de module și golește manifestul la fiecare `flat_vm_run_opts()`. Acest reset nu este compatibil cu două VM-uri suprapuse și poate invalida registry-ul altui apel.

Nu am schimbat încă lifecycle-ul registry-ului: încărcarea dinamică și înregistrarea handler-elor trebuie proiectate separat de manifestul fiecărui VM. Până atunci, execuțiile VM suprapuse/concurente rămân nesuportate. Nu declara reentrancy sau thread-safety.

## F0.4 — progress in archive 257: native plugin host

The native plugin host now separates process-wide plugin availability from per-VM authorization:

- Removed process-global `g_manifest` and `g_manifest_count`.
- Added `FlatNativeRuntimeContext` with a manifest pointer/count, owned by `VmState` and passed through `FlatNativeCtx.native_runtime`.
- Replaced per-run `flat_native_runtime_reset()` with one-time process initialization `flat_native_runtime_init()`.
- Embedded core handlers are registered once and tracked separately from optional plugin handlers.
- Optional plugin authorization is checked against the current VM manifest even when another execution has already loaded the handler into the process registry.
- The process-wide handler table, loaded-module flags, and search directory remain process-owned.

Regression test: `test/cases/flvm/009_native_manifest_context` verifies that two VM runtime contexts retain independent manifests. An initial regression run exposed that embedded core `mem` handlers must not be treated as optional plugins; the registry now tracks embedded handlers separately, and `native_use[5]` passes.

Validation on archive 257:
- `make all -j2` — PASS.
- `make docs` — PASS (45 sections).
- `flvm` — 9/9 PASS.
- `native` — 22/22 PASS.
- `native_use` — 6/6 PASS.
- `fmod` — 18/18 PASS in two bounded runs (1–14 and 15–18; the first combined command timed out after test 14, which was not counted as a completed suite).
- `math` — 34/34 PASS.
- `list` — 5/5 PASS.
- `obj` — 17/17 PASS in two bounded runs (1–13 and 14–17).
- `csv` — 36/36 PASS.

**Not claimed:** the full default suite (1144 discovered tests) was not run. The registry still has unsynchronized process-wide initialization and lazy loading, so this does not make the VM thread-safe or ready for a worker pool.

## F0.4 progress in archive 258 — locale and stdio

- Removed process-global `setlocale(LC_ALL, "C")` from VM startup.
- Added `flat_strtod()` / `flat_snprintf()` with thread-scoped C numeric locale handling on POSIX (shared immutable locale object, atomic one-time initialization) and locale-specific conversion APIs on Windows; WASM uses its C-locale runtime behavior.
- Routed FlatL numeric parsing and numeric formatting/serialization through those helpers.
- Moved `flat_port_stdio_setup()` from every VM run to one-time `flvm` / `flatl` CLI process initialization. Embedders must call it themselves when Windows text-mode stdio is required.
- `make all -j2` — PASS; `make docs` — PASS (45 sections).
- Tests: `flvm` 9/9, `math` 34/34, `json` 50/50, `date` 82/82, `io` 53/53, `io_ctx` 13/13, `values` 9/9, `str` 16/16, `http` 32/32, `port` 1/1, selected `types` 82/82. Runs were bounded; combined JSON/date/IO attempts hit the execution limit and remaining cases were rerun separately. `env` had 3 failures/14 and `path` had 1 failure/9; the same failures were reproduced against the unmodified archive 257 (e.g. env cases 004/008/009 report `io.outln.args` instead of their expected native panic, and path case 006 misses `path.normalize.empty`). These are pre-existing test failures, not regressions from archive 258. The full 1144-case default suite was not run.
- Non-C locale integration was not directly exercised; requires a host with a suitable installed locale.

### Remaining F0.4 work

- [x] Remove process-global locale mutation and move stdio setup to CLI process initialization.
- [x] Snapshot environment overrides/argv and path separator into per-VM contexts.
- [x] Replace the fallback memory-stream fixed slot table with a dynamically sized synchronized registry.
- [ ] Define runtime logger lifecycle and synchronization; it remains process-owned.
- [ ] Audit compiler panic-site state and WASM last-result state.
- [ ] Define a single-thread cooperative runtime contract and the process-global freeze/synchronization boundary for any later worker pool.


## F0.4 progress — per-VM environment/path snapshots and memory-stream registry (archive 259)

The process-level environment override/argv staging API remains available to CLI and WASM entry points, but each `flat_vm_run_opts()` now snapshots it into `FlatEnvContext` owned by `VmState`. `FlatNativeCtx.env` passes that context to `env.has`, `env.get`, `env.argc`, and `env.args`. The snapshot deep-copies override keys/values and program arguments, so later changes to process staging do not alter an already-running VM. The host process environment (`getenv`) remains process-owned and is treated as immutable for the duration of a VM run; hosts must not mutate it while a VM is executing.

Path separator configuration is now snapshotted into `FlatPathContext` per VM and passed through `FlatNativeCtx.path`. Native `path.*` and `file.sep` operations use that context instead of reading the process staging separator during execution. The legacy process-level path setter is retained only as CLI/WASM staging and for compatibility wrappers; the VM copies its selected separator at run start.

The non-GNU fallback implementation of `flat_open_memstream()` no longer has a fixed 16-slot registry. It now allocates registry nodes dynamically and protects registry insertion/removal with a C11 atomic-flag lock. Stream I/O and output-buffer ownership remain with the caller; the lock protects only the process-wide FILE-to-output-target registry. A forced-fallback regression test opens 40 simultaneous streams and verifies their independent outputs.

New regression test `test/cases/flvm/010_env_path_context_isolation` verifies deep snapshots of overrides/argv and independent path separators. New test `test/cases/flvm/011_memstream_registry_capacity` forces the fallback implementation, opens 40 streams (more than the previous 16-slot cap), and checks all output buffers.

Validation on archive 259:
- `make all -j2` — PASS.
- `bin/run_tests flvm -v -e` — 10/10 PASS before adding case 011; cases 010–011 — 2/2 PASS after addition.
- `bin/run_tests env -v -e` — 11/14 PASS; cases 004, 008, and 009 still fail with missing expected panic text, matching the known failures recorded for archive 258.
- `bin/run_tests path -v -e` — 8/9 PASS; case 006 still fails with missing expected `path.normalize.empty`, matching the known failure recorded for archive 258.
- Full default suite (1147 discovered tests) not run.

Remaining process-owned state: environment/path staging APIs (must be configured before VM start and not mutated concurrently), process environment itself, native plugin registry lazy loading, runtime logger configuration/counters/output, compiler panic-site emission state, and WASM last-result state. The fallback memory-stream registry is now dynamically sized and its registry operations are synchronized. This does not establish general VM reentrancy or thread safety.


## F0.4 — progres în arhiva 260

- [x] Corectate testele `env[4]`, `env[8]`, `env[9]` și `path[6]`: apelurile native care trebuie să producă panic sunt acum instrucțiuni standalone cu binding explicit `-> p`, astfel încât panic detail-ul nativ ajunge la harness. Semantica runtime nu a fost modificată.
- [x] Sincronizată starea logger-ului process-owned cu `atomic_flag`: configure, emit/sequence, buffer, dropped counter, flush și shutdown sunt serializate.
- [x] Eliminată starea globală mutabilă din compiler emission: `FlatCompileContext` local fiecărei compilări deține schema catalog, panic-site collection și locația/IP-ul instrucțiunii emise.
- [x] Documentat contractul WASM last-result getters ca singleton pentru bridge-ul FLEditor curent; o API multi-instance va trebui să returneze rezultat per apel.
- [ ] Native plugin lazy loading și staging process-level env/path necesită contract freeze/sincronizare înainte de worker pool.

Validare arhiva 260: `make all -j2` PASS; `env path flvm flc panic` 44/44 PASS (env 14/14, path 9/9, flvm 12/12, flc 3/3, panic 6/6). `make docs` PASS (45 secțiuni). Suita default completă (1147 teste descoperite) nu a fost rulată. Nu se declară reentrancy sau thread-safety generală.

## Archive 261 — native plugin registry synchronization

- [x] Added a process lifecycle lock for one-time embedded registration, search-directory updates, and optional plugin lazy loading.
- [x] Added a separate handler-registry lock for registration and lookup; native handlers are invoked only after releasing both locks.
- [x] Made dynamic plugin initialization transactional with thread-local staged registrations. Failed initializers discard staged handler pointers before `dlclose()`.
- [x] Made embedded-registration marking thread-local to prevent unrelated registrations from being marked as core handlers.
- [x] Snapshotted `FLAT_NATIVE_PATH` on first optional plugin load. Hosts must set it before the first optional load; later environment changes are ignored.
- [x] Added `flvm[13]` concurrent initialization/lazy-load coverage with eight threads and a post-load manifest-denial check.

Validation: `make all -j2` PASS; `make docs` PASS (45 sections); `flvm` 13/13; `native` 22/22; `native_use` 6/6; `env` 14/14; `path` 9/9; `fmod` 18/18; `math` 34/34; `list` 5/5; `obj` 17/17; `csv` 36/36 — all PASS. The full 1148-test default suite was not run. This closes the plugin registry initialization/lazy-load synchronization item, but does not establish general VM thread safety or worker-pool readiness.

## Archive 262 — transversal audit and initialization-race fixes

- [x] Inventory file-scope and function-local mutable statics beyond `g_*` names.
- [x] Serialize environment staging mutation/lookup with per-VM snapshot creation; fix partial-allocation cleanup in `flat_env_context_init()`.
- [x] Make path separator staging atomic and VM snapshot reads atomic.
- [x] Serialize libcurl global initialization and latch its result.
- [x] Replace Windows QPC lazy-init flag with `INIT_ONCE`.
- [x] Make math multidimensional debug-flag first-use initialization atomic.
- [x] Make native signature fallback and WASM module-list buffers thread-local.
- [x] Make Windows temporary shell-script sequence allocation atomic.
- [x] Add `flvm[14]` host environment/path staging concurrency regression.
- [x] Update the ownership matrix and document WASM singleton/fixed-file limitations.

Validation: `make all -j2` PASS; `make docs` PASS (45 sections); `flvm` 14/14; `env` 14/14; `path[1-4]` + `path[5-9]` 9/9; `http[1-8]` + `[9-16]` + `[17-24]` + `[25-32]` 32/32; `math_mdim` 51/51; `port` 1/1. Full default suite (1149 discovered) not run. Windows-specific changes were not compiled or exercised in this Linux environment. General VM reentrancy/thread safety and worker-pool readiness are not claimed.

## Archive 263 — parser/linker and host configuration

- [x] Audited parser, linker, and compiler for mutable file-scope/function-local static state; none was found in the parser/linker path, and compiler emission state remains invocation-local.
- [x] Replaced locale-sensitive C `ctype` classification in FlatL syntax code with ASCII-defined helpers (`flat_ascii.h`) across parser, linker, function, schema, native-use, labels, slots, numbers, and literal parsing.
- [x] Moved large linker module queue/use workspaces and `flat_link_program()` module/fmod workspaces from thread stacks to per-call heap allocations with centralized cleanup.
- [x] Added `FlatHostConfig`, independent initialization and legacy staging capture APIs, and `FlatVmOptions.host_config`.
- [x] Added deep-clone and independent per-context environment override/argv APIs; VM snapshots host configuration before execution.
- [x] Preserved legacy process staging when `host_config` is NULL.
- [x] Extended `flvm[10]` to cover independent host configs, staging capture, and deep snapshot behavior.

Validation: `make all -j2` PASS; `make docs` PASS (45 sections); `flvm[10]` + `flc` + `native_use` 10/10 PASS; selected `types`/`func` parser-related cases 15/15 PASS. Broader `types` and `func` runs reached the execution timeout after printing passing cases and are not counted as complete. Full default suite (1149 discovered tests) not run. No general thread-safety or worker-pool readiness is claimed.

## Archive 264 — FlatRuntime lifecycle foundation

- [x] Added opaque `FlatRuntime` with create/run/destroy API.
- [x] Runtime deep-copies an explicit `FlatHostConfig`, or captures legacy staging when config is NULL.
- [x] `flat_runtime_run()` makes runtime-owned host config authoritative while forwarding other VM options.
- [x] Track active VM calls and reject destruction while a run is active.
- [x] Added `flvm[15]` lifecycle test with a stubbed VM boundary, including active-run destroy rejection and host-config ownership.
- [x] Added `flvm[16]` integration test that executes real bytecode through `flat_runtime_run()`.

Validation: `make all -j2` PASS; `make docs` PASS; `flvm` 16/16 PASS. Full default suite (1151 discovered tests) not run. Actual concurrent VM execution, Windows, and WASM are not validated. The host must prevent new API calls from starting concurrently with `flat_runtime_destroy()`. This does not establish general VM thread safety or scheduler readiness.

## Archive 265 — FlatRuntime resource ownership audit

- [x] Classified runtime-lifetime, VM-invocation, caller-owned, compile/link invocation, and process-owned resources.
- [x] Kept CSV/DOM/VCX/HTTP/IO/file/pool/executor/RNG/date state per VM invocation; these resources contain mutable state and cross-references that are not safe to share between runs.
- [x] Confirmed `FlatRuntime` currently owns only its immutable `FlatHostConfig` snapshot; native registry, plugin libraries, logger, locale, libcurl initialization, and memory-stream registry remain process-owned.
- [x] Added `flvm[17]` to force a real VM execution error after setup, then run successfully again through the same runtime, checking diagnostic reset and teardown/reuse.
- [x] Documented caller ownership of bytecode and `FlatVmOptions` output buffers, including distinct buffers for concurrent runs.

Validation: `flvm[15-17]` 3/3 PASS; `make all -j2` PASS; `make docs` PASS (45 sections). Full default suite (1152 discovered tests) not run. This does not establish general VM thread safety, persistent VM semantics, scheduler readiness, or worker-pool safety.

## Archive 266 — persistent VM and cooperative task lifecycle specification

- [x] Defined separate ownership/lifecycle boundaries for `FlatRuntime`, `FlatTask`, and persistent `FlatVm`.
- [x] Defined task states and allowed transitions, including terminal-state invariants.
- [x] Defined instruction-boundary yield/resume and the saved-next-IP invariant.
- [x] Defined bytecode ownership, copied scalar options, and prohibition on retaining caller output pointers across yields.
- [x] Defined panic/failure isolation, cooperative cancellation, cleanup, and runtime-destroy busy semantics.
- [x] Defined scheduler-level `task.wait` behavior and documented initial wait-cycle limitation.
- [x] Added normative `doc_src/TASK_RUNTIME_MODEL.md` and indexed it for generated documentation.
- [ ] Heap-stable persistent `FlatVm` implementation and opcode-budget stepping.
- [ ] `FlatTask` registry, cooperative scheduler, spawn/wait natives, and Channels.

Validation on archive 266: baseline `make all -j2` PASS; baseline `bin/run_tests flvm -v -e` 17/17 PASS. This archive is a specification/documentation milestone; no persistent VM, task scheduler, or yield/resume code is introduced. Full default suite (1152 discovered tests) not run.


## Archive 267 — FlatVm ownership API foundation

- [x] Added opaque heap-allocated `FlatVm` handle and public lifecycle API (`flat_vm_create`, `flat_vm_execute`, `flat_vm_destroy`, `flat_vm_state`).
- [x] `flat_vm_create()` deep-copies the FlatBytecode through `flat_fb_load_buffer()`, copies scalar execution options, and deep-snapshots `FlatHostConfig` (or captures process staging).
- [x] `flat_vm_execute()` uses VM-owned bytecode/configuration and returns a value result with termination and diagnostic; it does not retain caller output pointers. Execution is one-shot and transitions READY → RUNNING → TERMINAL.
- [x] Added `flvm[18]` proving caller bytecode and host config can be destroyed after VM creation, successful execution, and rejection of a second execute.

**Explicit limitation:** the current `FlatVm` is an ownership shell, not yet a resumable VM. `VmState`, IP, guard scratch, and the opcode loop still live in `flat_vm_run_opts()` and are torn down at the end of execution. No bounded step/yield/resume, `FlatTask`, scheduler, task natives, Channels, or pthread workers are implemented.

Validation for archive 267 so far: `make all -j2` PASS; `bin/run_tests 'flvm[18]' -v -e` PASS (1/1). A full `bin/run_tests flvm -v -e` attempt stalled at the new case under the runner's suite invocation; the same case passed independently. The full default suite (1153 discovered tests) was not run.

Next step: extract the current VM initialization/continuation state into the stable handle, then implement an internal bounded-step function and resume equivalence tests before adding scheduler/task abstractions.


## Archive 268 — stable heap allocation for VM invocation state

- [x] Changed `flat_vm_run_opts()` to allocate `VmState` on the heap instead of the C stack.
- [x] Kept the `VmState` address stable for the entire invocation, before VCX and executor bindings retain pointers into it.
- [x] Routed normal completion and setup-failure cleanup through the existing teardown path and then freed the heap state.
- [x] Kept invalid-bytecode validation before allocation, avoiding an allocation leak on early rejection.
- [x] Updated the task-runtime model to distinguish stable per-invocation allocation from persistent cross-call continuation ownership.

**Explicit limitation:** this does not yet make `FlatVm` resumable. The IP, guard scratch arrays, and opcode loop remain local to `flat_vm_run_opts()`; `VmState` is still destroyed at the end of each invocation. No stepping/yield/resume, task scheduler, task natives, Channels, or pthread workers are implemented.

Validation for archive 268: `make all -j2` PASS; `bin/run_tests 'flvm[1-17]' -e` PASS (17/17); `bin/run_tests 'flvm[18]' -v -e` PASS (1/1). The combined `flvm` invocation was interrupted while building test 18, but the same case was built and passed independently. Full default suite (1153 discovered tests) not run.


## Archive 269 — continuation scratch consolidation

- [x] Moved the instruction pointer and entry-block index into `VmState`.
- [x] Moved guard-name and guard-value scratch arrays into `VmState`.
- [x] Kept the existing step/jump counters and call/loop frames in the same state object.
- [x] Removed the unused borrowed `FlatVmOptions *` member from `VmState`; options remain call-scoped in the run-to-completion interpreter.
- [x] Preserved existing `flat_vm_run_opts()` behavior and cleanup paths.

**Explicit limitation:** `VmState` is still allocated, initialized, executed, torn down, and freed inside one `flat_vm_run_opts()` call. The `FlatVm` handle does not yet own the initialized state across calls. No bounded stepping, yield/resume, `FlatTask`, scheduler, task natives, Channels, or pthread workers are implemented.

Validation for archive 269: `make all -j2` PASS; `bin/run_tests 'flvm[1-17]' -e` PASS (17/17); `bin/run_tests 'flvm[18]' -v -e` PASS (1/1). Full default suite (1153 discovered tests) not run.


## Archive 270 — explicit FlatVm terminal outcomes

- [x] Distinguished `FLAT_VM_COMPLETED` from `FLAT_VM_FAILED` in the public VM lifecycle state.
- [x] Kept `FLAT_VM_TERMINAL` as a compatibility alias for successful completion.
- [x] Added `flat_vm_is_terminal()` so callers can recognize either completed or failed handles without duplicating enum checks.
- [x] Updated `flvm[18]` to assert successful completion state and terminal query behavior.
- [x] Updated the task-runtime model and generated documentation source.

**Explicit limitation:** this is a lifecycle/API clarification, not persistent execution. `flat_vm_execute()` still runs to completion in one call; no yield/resume or opcode-budget stepping is available. The handle still delegates to `flat_vm_run_opts()`, which creates and destroys `VmState` during that call. No `FlatTask`, scheduler, task natives, Channels, or pthread workers are implemented.

Validation for archive 270: `make all -j2` PASS; `make docs` PASS (46 sections); `bin/run_tests 'flvm[1-17]' -v -e` PASS (17/17); `bin/run_tests 'flvm[18]' -v -e` PASS (1/1). A combined `bin/run_tests flvm -v -e` run passed tests 1–17 but hit the command timeout while compiling test 18; test 18 passed separately. Full default suite (1153 discovered tests) not run. The source snapshot contains no `.git` metadata, so no branch/commit status was available.


## Archive 271 — VM initialization separation

- [x] Extracted VM-owned runtime/resource initialization from `flat_vm_run_opts()` into `vm_state_initialize()`.
- [x] The helper initializes the VM state, binds VCX/executor pointers only after the heap allocation is stable, allocates guard scratch and jump counters, and selects the main entry block/IP.
- [x] Centralized partial-initialization failure cleanup; guard scratch is released before the remaining VM resources are torn down.
- [x] Kept the opcode loop and the public one-shot API behavior unchanged.

**Explicit limitation:** `vm_state_initialize()` is currently called by `flat_vm_run_opts()` and its `VmState` is still destroyed before that function returns. The new helper is not yet a persistent `FlatVm` initializer. No bounded stepping, yield/resume, `FlatTask`, scheduler, task natives, Channels, or pthread workers are implemented.

Validation for archive 271: `make all -j2` PASS; `bin/run_tests 'flvm[1-17]' -v -e` and `bin/run_tests 'flvm[18]' -v -e` are run separately (results recorded after final validation); `make docs` result recorded after final validation. Full default suite (1153 discovered tests) not run. Combined `bin/run_tests flvm -v -e` reached test 17 but timed out while building test 18; test 18 passed independently.


## F0.5 — persistent VM ownership progress (archive 272)

`FlatVm` now owns the initialized `VmState` from `flat_vm_create()` through `flat_vm_destroy()`. The internal bridge in `src/common/flat_vm_internal.h` provides state create/execute/destroy operations without exposing `VmState` publicly. `flat_vm_execute()` runs the owned state; the compatibility `flat_vm_run_opts()` uses the same bridge and preserves its run-to-completion behavior. `VmState` records whether runtime resources are active so destroying a never-executed READY handle tears them down exactly once, while terminal execution does not trigger duplicate teardown.

The flvm[18] lifecycle test now also creates and destroys a READY VM without executing it. This confirms the newly moved initialization/ownership path has a cleanup path before execution.

Validation for archive 272:
- `make all -j2` — PASS.
- `bin/run_tests flvm -v -e` — tests 1–17 PASS; command hit its execution timeout while compiling test 18.
- `bin/run_tests 'flvm[18]' -v -e` — 1/1 PASS (including READY-state destroy).
- Combined flvm coverage: 18/18 confirmed across the two runs above; the full default suite (1153 discovered tests) was not run.

**Not implemented yet:** bounded stepping, `YIELDED`, resume across calls, `FlatTask`, scheduler, task natives, or Channels.


## Archive 273 — cooperative FlatVm stepping and resume

- [x] Added public `flat_vm_step(vm, instruction_budget, result)`; a zero budget runs until terminal state.
- [x] Added `FLAT_VM_YIELDED` to the public VM lifecycle states.
- [x] Refactored the interpreter dispatch loop to yield at the top of the next dispatch iteration, only after the preceding opcode has completed and updated `VmState.ip`.
- [x] Kept the `VmState`, VCX/executor bindings, call/loop frames, pools, IO/file state, and counters alive across yielded calls.
- [x] Kept `flat_vm_execute()` and `flat_vm_run_opts()` as run-to-completion compatibility paths.
- [x] Added `flvm[19]`, compiling a multi-opcode program and resuming it with a one-opcode budget until completion.

The per-call instruction budget is distinct from the cumulative `FlatVmOptions.max_steps` execution limit. Yield cannot preempt an opcode or native call; long-running native work remains cooperative and non-preemptible.

**Not implemented:** exhaustive resume equivalence tests for nested calls, loops, executor scopes, and panic handlers; `FlatTask`, scheduler, task natives, Channels, cancellation, and pthread workers.

Validation for archive 273: `make all -j2` PASS; `make docs` PASS (46 sections); `bin/run_tests 'flvm[1-17]' -v -e` PASS (17/17); `bin/run_tests 'flvm[18]' -v -e` PASS (1/1); `bin/run_tests 'flvm[19]' -v -e` PASS (1/1). A combined `bin/run_tests flvm -v -e` run passed tests 1–17 but timed out while compiling test 18; tests 18 and 19 passed separately. The full default suite (1154 discovered tests) was not run. The source snapshot has no `.git` metadata, so branch/commit status could not be inspected.


## F0.6 — archive 274: control-flow continuation regression

The persistent VM stepping test now covers more than straight-line bytecode. `flvm[20]` exercises nested block `call`/`return`, `for`, `foreach`, nested unique executor scopes with `func:callVM 1`, and panic-handler control transfer through `#panic.any`. The test compares `flat_vm_run_opts()` against a stepped run of the same bytecode using rotating instruction budgets (1, 2, 3), and verifies equal terminal exit code/termination plus at least one yield.

The test deliberately uses one compiled program so that all continuation features coexist in the same `VmState`. It also frees the caller's loaded bytecode after `flat_vm_create()`, continuing to verify the VM-owned bytecode snapshot. The harness supplies `out_termination` and `out_panic_msg` pointers at creation, changes both caller buffers immediately after creation, and confirms that repeated steps never mutate them.

**Still outstanding after archive 274:** exact output side-effect comparison, failure lifecycle tests, and all `FlatTask`/scheduler/Channel APIs. Archive 275 closes the first two test gaps. No thread-safety or worker-pool guarantee is implied.

## Archive 275 — stepping side effects and failure lifecycle

`flvm[20]` now captures stdout from both the compatibility `flat_vm_run_opts()` execution and the repeated `flat_vm_step()` execution of the same bytecode, then compares the output byte-for-byte. This extends the existing nested call/loop/executor/panic-handler continuation test from result equivalence to observable side-effect equivalence.

New `flvm[21]` covers three lifecycle paths: (1) yield after an output instruction, then execute logical `FAIL` and verify `FLAT_VM_FAILED` / `FLAT_TERM_FAIL`; (2) execute an unhandled `math.calc('1 / 0')` panic and verify `FLAT_VM_FAILED` / `FLAT_TERM_PANIC` plus a diagnostic; and (3) destroy a VM immediately after yield and verify later instructions never run. Both failure cases reject further stepping, while destruction remains permitted for terminal and yielded handles.

Validation for archive 275:
- `make all -j2` — PASS.
- `make docs` — PASS (46 sections).
- `bin/run_tests 'flvm[1-17]' -v -e` — 17/17 PASS.
- `bin/run_tests 'flvm[18]' -v -e` — 1/1 PASS.
- `bin/run_tests 'flvm[19]' -v -e` — 1/1 PASS.
- `bin/run_tests 'flvm[20]' -v -e` — 1/1 PASS (including stdout equivalence).
- `bin/run_tests 'flvm[21]' -v -e` — 1/1 PASS (failure and destroy-after-yield lifecycle).
- `bin/run_tests 'func[1-37]' -v -e` — 31/31 PASS.
- The default suite has 1156 discovered tests and was not run in full.

No `FlatTask`, scheduler, task natives, Channels, cancellation API, or pthread worker pool is introduced. The source snapshot has no `.git` metadata, so branch/commit status is unavailable.


## Archive 276 — first cooperative FlatTask scheduler

- [x] Added public task IDs and lifecycle enum in `include/flat_task.h`.
- [x] Added runtime-owned task registry; each record owns one persistent `FlatVm`.
- [x] Added monotonic runtime-local task IDs that are not reused after task destruction.
- [x] Added explicit `CREATED -> RUNNABLE` start transition.
- [x] Added synchronous round-robin scheduler using `flat_vm_step()` with a per-turn instruction quantum and bounded/unbounded scheduler turns.
- [x] Mapped VM yield/completion/failure to task lifecycle states; one failed task does not stop other runnable tasks.
- [x] Added task state/result queries and explicit destruction of CREATED or terminal tasks.
- [x] Made runtime destruction return busy while any task record remains.
- [x] Added `flvm[22]` with two interleaving successful tasks, one failing task, result/state checks, runtime busy checks, and ID non-reuse.

Validation for archive 276: `make all -j2` PASS; direct `test-cooperative-task-scheduler` integration executable PASS; `flvm[22]` test case pending final runner validation; full default suite not run. Task registry/scheduler APIs are single-host-thread only. `WAITING`, wakeup, cancellation, task natives, Channels, and pthread workers are not implemented. The source snapshot has no `.git` metadata, so branch/commit status is unavailable.


**Arhivă curentă 276:** `flatL-main-276-cooperative-flat-task-scheduler.zip`. `make all -j2`, `make docs`, `flvm[15-17]`, `flvm[18-21]` și `flvm[22]` PASS. Testul 22 verifică alternarea a două task-uri cu quantum 1, eșec izolat al unui al treilea task, query de state/result, runtime busy cât timp task-urile există și ne-reutilizarea ID-urilor. Suita generală are 1157 teste descoperite și nu a fost rulată integral. Task APIs sunt single-host-thread; `WAITING`, wakeup, cancellation, task natives, Channels și pthread workers nu sunt implementate. Snapshot-ul nu conține `.git`, deci branch/commit nu au putut fi verificate.


## Archive 277 — task wait/wakeup and safe-point cancellation

- [x] Added `flat_runtime_task_wait()` with self/unknown ID validation, a single wait dependency per waiter, immediate continuation for an already-terminal target, and `RUNNABLE` → `WAITING` transition.
- [x] Wake all dependent waiters when a target completes, fails, or is cancelled.
- [x] Prevent destruction of a target while tasks still wait on it.
- [x] Added `flat_runtime_task_cancel()` for CREATED/RUNNABLE/WAITING tasks; cancellation is applied between scheduler turns and releases VM execution resources.
- [x] Added `FLAT_VM_CANCELLED` and `FLAT_TERM_CANCELLED`; cancelled tasks expose terminal results and wake their dependants.
- [x] Added `flvm[23]` coverage for wait/wakeup, immediate terminal waits, dependency-safe destruction, cancellation before start and after yield, and wakeup after cancellation.

Validation on archive 277: `make all -j2` PASS; `make docs` and the focused/full flvm regression runs are recorded in the archive summary. Waiting-cycle detection, FlatL task natives, Channels, and pthread workers remain unimplemented. C-level task/scheduler APIs remain confined to the scheduler-driving host thread.

Validation on archive 277:
- `make all -j2` — PASS.
- `make docs` — PASS (46 sections).
- `flvm[1-18]` — 18/18 PASS in the first broad run; the run timed out while compiling test 19.
- `flvm[19-21]` — 3/3 PASS.
- `flvm[22]` — 1/1 PASS.
- `flvm[23]` — 1/1 PASS after the final wait/failure/cancellation coverage additions.
- `func[1-37]` — 31/31 discovered tests PASS.
- Full default suite (1158 discovered tests) not run.


## Archive 278 — FlatL task natives

- [x] Registered built-in `task` native module and documented it in `doc_src/natives/task.md`.
- [x] Added `task.spawn(#block)` to create a runtime-owned child VM starting at a named block in the same bytecode. The initial form accepts no arguments.
- [x] Added `task.wait(taskId)` as a scheduler-aware native suspension; the VM advances past the native call before yielding and does not replay it after wakeup.
- [x] Added `task.state(taskId)` for numeric lifecycle-state inspection.
- [x] Connected task-native callbacks through a per-VM bridge; non-task VMs do not receive a bridge.
- [x] Added `flvm[24]` coverage for successful child completion, native wait/resume, and wakeup after child FAIL.

Argument passing, Channel handles, wait-cycle detection, and pthread workers remain future work. The scheduler remains single-threaded and task APIs remain host-thread confined.


## Archive 279 — task.spawn named argument binding and bytecode v14

- [x] Extended `.fb` format to v14 with the `BPAR` section after `JBMX`; each block's declared parameter names are serialized in block order.
- [x] Kept the loader compatible with v3–v13 bytecode. A v13 compatibility image derived from the minimal fixture was loaded successfully.
- [x] Included block parameter names in compiler `var_slots` accounting and rejected invalid/duplicate parameter names during compilation.
- [x] Extended `task.spawn(#block, arg1, ...)` to parse nested/quoted argument expressions, including commas inside strings, lists, and objects. Argument count must exactly match the target block's declared parameters.
- [x] Bound values to the child's named parameters in its initial private VCX context. Scalars and strings are copied; LIST/OBJECT values are recursively deep-copied across independent VM pools. File handles are rejected because their table ownership is VM-local.
- [x] Added `flvm[25]`, which transfers NUMBER, STRING, LIST, and OBJECT values, mutates the child copies, and verifies the parent retains its original LIST/OBJECT values.

Validation on archive 279:
- `make all -j2` — PASS.
- `make docs` — PASS (47 sections).
- `flvm[1-10]` — 10/10 PASS; `flvm[11-18]` — 8/8 PASS; `flvm[19-21]` — 3/3 PASS; `flvm[22]`, `[23]`, `[24]`, `[25]` — each 1/1 PASS. All 25 flvm cases were validated in bounded runs.
- `native[1-8]` — 8/8 PASS; `native[9-22]` — 13/13 selected tests PASS (case 17 is not present in this range's discovered set); `native_use[1-6]` — 6/6 PASS.
- `func[1-37]` — 31/31 discovered tests PASS.
- `flvm[25]` also constructs a v13-compatible bytecode image by removing BPAR and verifies that the loader accepts it; it checks the v14 BPAR names, nested LIST/OBJECT deep-copy isolation, comma-containing string parsing, and exact-arity failure.

A combined `flvm[1-25]` run exceeded the command time limit after tests 1–18 while compiling the next target; the cases were then run in smaller groups or individually. The full default suite (1160 discovered tests) was not run. Source snapshot has no `.git` metadata, so branch/commit status is unavailable.

Channels, wait-cycle detection, result transfer/await return values, file-handle transfer, and pthread workers remain future work.


## Arhiva 280 — Runtime Channels C API

- `include/flat_channel.h` / `src/common/flat_channel.c` definesc Channel handles și operațiile create/set/var/lock/unlock/revert/destroy.
- `FlatRuntime` deține registry-ul și allocatorul monotonic de Channel ID-uri; runtime destroy rămâne busy cât timp există Channels.
- Fiecare Channel deține pool-uri independente de string/collection; primitivele, string-urile, LIST și OBJECT sunt copiate recursiv. VAL_FILE este respins.
- Fiecare operație C1 este serializată printr-un gate C11 atomic per Channel. Acesta este locking portabil pentru milestone-ul cooperativ, nu o declarație de thread-safety generală și nu un mutex pthread.
- Tranzacțiile C2 păstrează un snapshot deep-copy și sunt deținute de un task ID. `unlock` comite, `revert` restaurează snapshot-ul fără a elibera lock-ul, iar non-owner access întoarce busy.
- Tranzacțiile active sunt automat rollback + unlock la completion, failure, cancellation sau destroy al task-ului.
- `flvm[26]` verifică ownership, copiere, file-handle rejection, tranzacții, busy destroy și rollback la cancellation.

Nu există încă integrare FlatL native pentru Channels, transfer de Channel handles prin `task.spawn`, evenimente Channel în logger sau worker pool pthread.

Validare pentru arhiva 280:
- `make all -j2` — PASS.
- `make docs` — PASS (48 sections).
- `flvm[1-17]` — 17/17 PASS.
- `flvm[18-21]` — 4/4 PASS.
- `flvm[22-23]` — PASS; rularea grupată `flvm[22-24]` a expirat în timpul compilării targetului 24, care a trecut separat.
- `flvm[24]`, `flvm[25]`, `flvm[26]` — fiecare 1/1 PASS separat.
- `func[1-37]` — 31/31 discovered tests PASS.
- `native` — 22/22 PASS; `native_use` — 6/6 PASS.
- Toate cele 26 de cazuri `flvm` au fost confirmate prin rulări bounded sau individuale. Suita generală (1161 discovered tests) nu a fost rulată integral.
- Snapshot-ul de bază nu conține metadata `.git`; branch/commit nu pot fi verificate.


## Arhiva 281 — FlatL Channel native integration

- [x] Registered the built-in `channel` native module and added `doc_src/natives/channel.md`.
- [x] Added `channel.create()` for runtime-owned Channel creation from a running task.
- [x] Added `channel.set(channelId, name, value)` and `channel.var(channelId, name)` with deep-copy semantics between VM and Channel pools.
- [x] Added `channel.lock`, `channel.unlock`, and `channel.revert` for explicit C2 transactions.
- [x] Added `channel.destroy(channelId)` for explicit destruction of an unlocked Channel; handles are not reference-counted.
- [x] Reused the existing numeric `task.spawn(#block, channelId)` argument transfer; the handle is copied while the runtime-owned Channel remains shared.
- [x] Added scheduler priority for runnable transaction owners so a small instruction quantum does not interleave other tasks into an active multi-instruction transaction.
- [x] Rejected `task.wait()` while the current task owns a Channel transaction; terminal cleanup still rolls back on failure/cancellation.
- [x] Added `flvm[27]` with two workers sharing one Channel, transaction commit/revert, LIST/OBJECT values, one-instruction quantum, explicit destruction, and failure/rollback for wait-inside-transaction.

Validation for archive 281:
- `make all -j2` — PASS.
- `make docs` — PASS (49 sections).
- `flvm[1-17]` — 17/17 PASS.
- `flvm[22]` — 1/1 PASS separately.
- `flvm[22-25]` — 4/4 PASS; `flvm[26-27]` — 2/2 PASS. The expanded `flvm[26]` also verifies that the C-level `flat_runtime_task_wait()` rejects a waiter that owns a Channel transaction. `flvm[27]` verifies the same restriction through the FlatL native. A larger combined `flvm[22-27]` invocation timed out after test 25; tests 26–27 passed in a separate bounded run.
- `native` — 22/22 PASS; `native_use` — 6/6 PASS.
- Full default suite (1162 discovered tests) not run.
- Source snapshot has no `.git` metadata; branch/commit status is unavailable.

Remaining: Channel logger event producers/`channel.log()`, wait-cycle detection, automatic/reference-counted Channel handle lifetime, non-owner Channel-lock waiting, pthread worker pool and cross-thread runtime/VM safety.


## Archive 282 — wait cycles, Channel references, and event logging

- [x] Reject a new wait dependency if following the target's wait chain reaches the waiter; added three-task cycle coverage to `flvm[23]`.
- [x] Register direct numeric `task.spawn()` arguments that match live Channel IDs as task-held Channel references.
- [x] Release task-held Channel references on completion, failure, cancellation, and task destruction.
- [x] Reject Channel destruction while another task holds a reference; allow the sole remaining task holder to destroy its Channel.
- [x] Emit `--show-channel-log` events for create/set/var/lock/unlock/revert/destroy, including outcomes and safe metadata but never stored values.
- [x] Added `flvm[28]` for Channel event logging and task-held lifetime protection.

Validation for archive 282:
- `make all -j2` — PASS.
- `make docs` — PASS (49 sections).
- `flvm[1-18]` — 18/18 PASS in the first bounded run.
- `flvm[19-21]` — 3/3 PASS; the combined `flvm[19-22]` invocation timed out while compiling test 22, which passed separately.
- `flvm[22]`, `flvm[23]`, `flvm[24-25]`, `flvm[26]`, `flvm[27]`, and `flvm[28]` — all PASS in bounded runs (1/1, 1/1, 2/2, 1/1, 1/1, 1/1 respectively).
- `native` — 22/22 PASS; `native_use` — 6/6 PASS.
- `func[1-37]` was attempted but timed out after the first 3 tests, all of which passed. The complete function suite is not claimed as passing in this archive.
- Full default suite (1163 discovered tests) not run.

Limitations: Channel references are recognized for direct numeric spawn arguments and task-level Channel operations only; nested LIST/OBJECT values are not scanned. Because Channel IDs remain ordinary NUMBER values, a numeric argument coincidentally equal to a live Channel ID is conservatively treated as a reference. Non-owner transaction waiting and pthread worker safety remain future work.

## Cooperative Channel contention and task result summary — archive 283

- [x] Add a native retry continuation flag distinct from completed-native `task.wait()` suspension.
- [x] Make task-native `channel.lock`, `channel.set`, and `channel.var` wait on a transaction owner instead of failing with busy.
- [x] Preserve the VM IP on a contended native instruction and retry it after Channel unlock or owner transaction abort.
- [x] Register a Channel reference for a task waiting on that Channel so the handle cannot be destroyed while the task is suspended.
- [x] Wake Channel waiters on commit/unlock and on transaction-owner terminal cleanup; keep waiters suspended across `revert`.
- [x] Add `task.result(taskId)` returning terminal state, exit code, termination, and diagnostic metadata.
- [x] Add `flvm[29]` for lock/set/var contention and retry; add `flvm[30]` for 48-task / 24-Channel runtime stress.
- [x] Extend `flvm[24]` to verify success/failure result objects.

Validation for archive 283:
- `make all -j2` — PASS.
- `make docs` — PASS (49 sections).
- `flvm[1-18]` — 18/18 PASS.
- `flvm[19-22]` — 4/4 PASS before the larger `flvm[19-30]` batch timed out after #22.
- `flvm[23]`, `flvm[24]`, `flvm[25]`, `flvm[26]`, `flvm[27]`, `flvm[28]`, `flvm[29]`, and `flvm[30]` — each PASS in separate bounded runs.
- `native` — 22/22 PASS; `native_use` — 6/6 PASS; `func[1-37]` — 31/31 PASS.
- Full default suite — not run.

The scheduler and Channel semantics remain cooperative and single-threaded. The C Channel API still reports busy rather than suspending because host calls have no current task continuation. No pthread or general thread-safety claim is made.


## Archive 284 — cooperative scheduler correctness audit

**Arhivă de bază:** `flatL-main-283-cooperative-channel-wait-and-task-results.zip`
**Arhivă curentă:** `flatL-main-288-cooperative-chained-wait-graph-audit.zip`

### Schimbări

- Extins testul existent `test/cases/flvm/029_channel_lock_contention` pentru două scenarii suplimentare: owner-ul este anulat sau eșuează VM-ul său în timp ce waiter-ul este WAITING. Ambele verifică rollback/release, wakeup-ul waiter-ului, retry al aceleiași instrucțiuni `channel.lock` și terminarea cu succes a waiter-ului.
- Nu s-a schimbat implementarea runtime; testul adaugă acoperire pentru calea de cancellation care exista deja în cleanup-ul task-ului.
- `01_DECISIONS.md` documentează contractul retry/yield, ownership-ul waiter-ului, referința Channel, wakeup rules și `task.result`.
- `02_ROADMAP.md` actualizează milestone-urile și checkpoint-ul următor.
- `03_RUNTIME_AUDIT.md` înregistrează traseele confirmate și limitele care blochează pthread.

### Validare

- `make all -j2` — PASS.
- `make docs` — PASS (49 sections).
- `bin/run_tests 'flvm[23]' -v -e` — 1/1 PASS.
- `bin/run_tests 'flvm[24]' -v -e` — 1/1 PASS.
- `bin/run_tests 'flvm[28]' -v -e` — 1/1 PASS.
- `bin/run_tests 'flvm[29]' -v -e` — 1/1 PASS după extinderea testului cu owner cancellation și owner failure.
- `bin/run_tests 'flvm[30]' -v -e` — 1/1 PASS într-o rulare individuală; o comandă grupată a atins limita de timp înainte de finalizarea compilării acestui target.

**Nu este declarat:** full default suite; întregul `flvm[1-30]` într-o singură rulare; thread safety sau readiness pentru worker pool.

### Următorul pas

Verificări pentru task destruction în prezența dependențelor wait și a referințelor Channel, inclusiv ordinea destroy/wakeup/retry. Abia după acestea continuăm regresia wait graph / cleanup. Nu introducem pthread în această etapă.


## Arhiva 285 — cooperative task destruction audit

### Modificări

- Extins `test/cases/flvm/029_channel_lock_contention/channel_lock_contention_test.c` cu două secvențe de lifecycle pentru task destruction și Channel references.
- Verificat că owner-ul cu waiter activ nu poate fi distrus; după anularea waiter-ului, destroy-ul owner-ului CREATED face abort/rollback al tranzacției și eliberează Channel-ul.
- Verificat că waiter-ul trezit de commit este RUNNABLE și nu poate fi distrus direct; cancellation înainte de retry eliberează referința, apoi waiter/owner/Channel pot fi distruse.
- Actualizate `plans/01_DECISIONS.md`, `02_ROADMAP.md`, `03_RUNTIME_AUDIT.md`, acest status și `doc_src/TASK_RUNTIME_MODEL.md`.
- Nu a fost necesară schimbare în runtime: auditul a confirmat că guard-urile și cleanup-ul existente respectă contractul testat.

### Validare

- `make all -j2` — PASS.
- `make docs` — PASS (49 sections).
- `bin/run_tests 'flvm[23]' -v -e` — 1/1 PASS.
- `bin/run_tests 'flvm[24]' -v -e` — 1/1 PASS.
- `bin/run_tests 'flvm[26-27]' -v -e` — 2/2 PASS.
- `bin/run_tests 'flvm[28]' -v -e` — 1/1 PASS.
- `bin/run_tests 'flvm[29]' -v -e` — 1/1 PASS cu noile secvențe de task destruction.
- `bin/run_tests 'flvm[30]' -v -e` — 1/1 PASS.
- `bin/run_tests native -v -e` — 22/22 PASS.
- `bin/run_tests native_use -v -e` — 6/6 PASS.

Full default suite (1165 discovered tests) — NOT RUN. Nu se declară full-suite PASS, thread safety sau readiness pentru worker pool. Snapshot-ul sursă nu conține metadata `.git`; branch/commit nu pot fi verificate.


## Archive 286 — multi-Channel suspension guard

### Finding and implementation

- Found a gap in the archive 285 invariant: `task.wait()` was rejected while a task owned a Channel transaction, but a Channel retry on a second Channel could still suspend that same task while it retained the first transaction.
- Updated `flat_runtime_channel_lock()` to reject contention-based waiting when the caller owns any Channel transaction.
- Updated `runtime_task_wait_channel()` with the same ownership guard, covering contended `channel.set()` and `channel.var()`.
- The ownership scan is executed before taking runtime/Channel gates, avoiding recursive acquisition of the target Channel gate.
- Added `lock_while_holding.fl`, `set_while_holding.fl`, and `var_while_holding.fl` to `flvm[29]`. Each test acquires Channel 1, contends on Channel 2, and verifies task failure plus rollback of Channel 1.

### Validation

- `make all -j2` — PASS.
- `make docs` — PASS (49 sections).
- `bin/run_tests 'flvm[23]' -v -e` — 1/1 PASS (within the first bounded `flvm[23-30]` attempt).
- `bin/run_tests 'flvm[24]' -v -e` — 1/1 PASS (within the first bounded `flvm[23-30]` attempt).
- `bin/run_tests 'flvm[25]' -v -e` — 1/1 PASS (individual rerun after the grouped command timed out during this case's build).
- `bin/run_tests 'flvm[26]' -v -e` — 1/1 PASS.
- `bin/run_tests 'flvm[27]' -v -e` — 1/1 PASS.
- `bin/run_tests 'flvm[28]' -v -e` — 1/1 PASS (individual rerun after a grouped invocation timed out during build).
- `bin/run_tests 'flvm[29]' -v -e` — 1/1 PASS, including lock/set/var while holding a different Channel transaction.
- `bin/run_tests 'flvm[30]' -v -e` — 1/1 PASS.
- `bin/run_tests native -v -e` — 22/22 PASS.
- `bin/run_tests native_use -v -e` — 6/6 PASS.

A grouped `flvm[23-30]` run timed out during build steps; all eight cases subsequently passed in bounded runs (23–24 in the first grouped attempt, 25 and 28 individually, and 26–27 / 29–30 in bounded invocations). The full default suite (1165 discovered tests) is not claimed as passing. No pthread or general thread-safety guarantee is introduced. The source archive has no `.git` metadata, so branch/commit state cannot be verified from this snapshot.


## Archive 287 — cooperative multi-Channel wait graph audit

### Modificări

- Extins `test/cases/flvm/029_channel_lock_contention` cu un owner care deține simultan două Channel-uri și două waiter-e care așteaptă același owner prin Channel-uri diferite.
- Verificat wakeup separat la commit pentru primul Channel și wakeup ulterior al celui de-al doilea waiter prin abort-ul tranzacției rămase în cleanup-ul terminal al owner-ului.
- Verificat că ambele waiter-e sunt RUNNABLE după cleanup-ul owner-ului și apoi reiau operațiile originale până la COMPLETED.
- Corectate programele no-suspend din testul `flvm[29]`: folosesc acum Channel-urile live 3 și 4. Auditul a descoperit că versiunile din arhiva 286 foloseau Channel 1 după ce acesta fusese distrus, deci testele eșuau prea devreme și nu verificau efectiv suspendarea cu tranzacție activă.
- Nu a fost necesară modificarea runtime-ului; corectitudinea căilor de cleanup/wakeup existente a fost confirmată prin teste care ating acum scenariile intenționate.
- Actualizate deciziile, roadmap-ul, auditul runtime, acest status și documentația normativă `doc_src/TASK_RUNTIME_MODEL.md`.

### Validare

- `make all -j2` — PASS.
- `make docs` — PASS (49 sections).
- `bin/run_tests 'flvm[23]' -v -e --time` — 1/1 PASS.
- `bin/run_tests 'flvm[24]' -v -e --time` — 1/1 PASS.
- `bin/run_tests 'flvm[25]' -v -e --time` — 1/1 PASS.
- `bin/run_tests 'flvm[26]' -v -e --time` — 1/1 PASS.
- `bin/run_tests 'flvm[27]' -v -e --time` — 1/1 PASS.
- `bin/run_tests 'flvm[28]' -v -e --time` — 1/1 PASS.
- `bin/run_tests 'flvm[29]' -v -e --time` — 1/1 PASS, inclusiv owner multi-Channel, wakeup diferențiat și testele no-suspend corectate.
- `bin/run_tests 'flvm[30]' -v -e --time` — 1/1 PASS.

O comandă grupată pentru mai multe cazuri a fost întreruptă în timpul build-ului la `flvm[26]`; cazurile 23–30 au fost apoi rulate individual și fiecare a trecut. Full default suite (1165 discovered tests) — NOT RUN. `native` și `native_use` nu au fost rerulate în această arhivă. Nu se declară pthread readiness sau thread safety. Snapshot-ul nu conține `.git`; branch/commit nu pot fi verificate.


## Archive 288 — cooperative chained task.wait + Channel wait graph audit

### Modificări

- Extins `test/cases/flvm/029_channel_lock_contention/channel_lock_contention_test.c` cu un lanț mixt de dependențe: Task C așteaptă un Channel deținut de B; D execută `task.wait(C)`; A execută `task.wait(B)`.
- Verificat că unlock-ul Channel-ului trezește numai C, în timp ce A și D rămân WAITING.
- Verificat că finalizarea lui C trezește D, fără să trezească A; finalizarea ulterioară a lui B trezește A.
- Runtime-ul nu a necesitat modificări. Testul confirmă folosirea comună a `waiting_on` și rolul separat al `waiting_channel_id`.
- Documentat de ce un ciclu mixt printr-un owner activ de tranzacție este exclus de regula cooperativă no-suspend-while-owning, fără a pretinde deadlock freedom generală.
- Actualizate `plans/01_DECISIONS.md`, `02_ROADMAP.md`, `03_RUNTIME_AUDIT.md`, acest status, `plans/README.md` și `doc_src/TASK_RUNTIME_MODEL.md`.

### Validare

- `make all -j2` — PASS.
- `bin/run_tests 'flvm[23]' -v -e` — 1/1 PASS.
- `bin/run_tests 'flvm[24]' -v -e` — 1/1 PASS.
- `bin/run_tests 'flvm[25]' -v -e` — 1/1 PASS.
- `bin/run_tests 'flvm[26]' -v -e` — 1/1 PASS.
- `bin/run_tests 'flvm[27]' -v -e` — 1/1 PASS.
- `bin/run_tests 'flvm[28]' -v -e` — 1/1 PASS.
- `bin/run_tests 'flvm[29]' -v -e` — 1/1 PASS, inclusiv lanțul mixt de dependențe.
- `bin/run_tests 'flvm[30]' -v -e` — 1/1 PASS.
- `bin/run_tests native -v -e` — 22/22 PASS.
- `bin/run_tests native_use -v -e` — 6/6 PASS.
- `make docs` — PASS (49 sections).

Comanda grupată `flvm[23-30]` a fost întreruptă în timpul build-ului la `flvm[25]`; toate cazurile 23–30 au fost apoi executate individual și au trecut. Full default suite (1165 discovered tests) — NOT RUN. Nu se declară pthread readiness sau thread safety. Snapshot-ul nu conține `.git`; branch/commit nu pot fi verificate.

### Următorul pas

Testarea cancellation/failure în fiecare poziție a lanțului: owner-ul Channel, waiter-ul Channel și task-ul care așteaptă waiter-ul. Apoi auditul teardown-ului runtime după eliminarea tuturor dependențelor și referințelor Channel.

## Archive 289 — cooperative chained cancellation and failure audit

### Modifications

- Extended `test/cases/flvm/029_channel_lock_contention/channel_lock_contention_test.c` with owner-cancellation propagation through a Channel waiter and two distinct `task.wait()` dependants.
- Added an intermediate-waiter cancellation case. It verifies the downstream dependent is woken immediately, then demonstrates that its own Channel operation can wait on the still-active owner until that owner is cancelled.
- Added `waiter_fail_after_lock.fl`: a Channel waiter wakes, retries and acquires the lock, then deliberately fails. The test verifies terminal failure cleanup releases the transaction and wakes its downstream task waiter.
- Updated `run.sh` to compile and pass the new bytecode program to the C harness.
- Runtime source was not changed; the existing wakeup and cleanup paths passed the expanded regression.
- Updated decisions, roadmap, runtime audit, this status, `plans/README.md`, `doc_src/TASK_RUNTIME_MODEL.md`, and the main README.

### Validation

- `make all -j2` — PASS.
- `make docs` — PASS (49 sections).
- `bin/run_tests 'flvm[23]' -v -e --time` — 1/1 PASS.
- `bin/run_tests 'flvm[24]' -v -e --time` — 1/1 PASS.
- `bin/run_tests 'flvm[25]' -v -e --time` — 1/1 PASS.
- `bin/run_tests 'flvm[26]' -v -e --time` — 1/1 PASS (individual rerun after a batch interruption).
- `bin/run_tests 'flvm[27]' -v -e --time` — 1/1 PASS.
- `bin/run_tests 'flvm[28]' -v -e --time` — 1/1 PASS.
- `bin/run_tests 'flvm[29]' -v -e --time` — 1/1 PASS, including owner cancellation, intermediate waiter cancellation, and failure after Channel retry.
- `bin/run_tests 'flvm[30]' -v -e --time` — 1/1 PASS (individual rerun after a batch interruption).
- `bin/run_tests native -v -e` — 22/22 PASS.
- `bin/run_tests native_use -v -e` — 6/6 PASS.

The full default suite (1165 discovered tests) was not run. No pthread/thread-safety or worker-pool readiness is claimed. The source snapshot has no `.git` metadata, so branch/commit state cannot be verified.


## Archive 290 — cooperative runtime teardown and resource ownership audit

### Modifications

- Extended `test/cases/flvm/029_channel_lock_contention/channel_lock_contention_test.c` with `run_runtime_teardown_audit()`.
- Verified runtime destruction is rejected while a CREATED task and Channel remain registered.
- Verified a RUNNABLE task holding a Channel transaction cannot be destroyed; runtime and Channel destruction are rejected until cancellation rolls back the transaction and explicit task/Channel destruction occurs.
- Verified a WAITING Channel waiter prevents task/runtime destruction. Cancelling its owner wakes it, but the RUNNABLE waiter still pins the Channel and keeps the runtime alive until it is cancelled and destroyed.
- Verified COMPLETED, FAILED, and CANCELLED task records remain in the runtime registry and keep the runtime alive until explicit `flat_runtime_task_destroy()`.
- Verified the successful teardown sequence and that a newly created runtime starts independent task and Channel IDs at one.
- No runtime implementation changes were required; the existing lifecycle guards and ownership cleanup satisfy the covered cooperative cases.
- Updated `plans/01_DECISIONS.md`, `02_ROADMAP.md`, `03_RUNTIME_AUDIT.md`, this status, `plans/README.md`, `doc_src/TASK_RUNTIME_MODEL.md`, and the test case description.

### Validation

- `bin/run_tests 'flvm[29]' -v -e --time` — 1/1 PASS, including CREATED/RUNNABLE/WAITING/terminal teardown guards, Channel pinning, cancellation cleanup, and fresh-runtime reuse.
- `bin/run_tests 'flvm[22]' -v -e --time` — 1/1 PASS (task registry/scheduler lifecycle).
- `bin/run_tests 'flvm[28]' -v -e --time` — 1/1 PASS (Channel references and wait-cycle lifecycle).
- `bin/run_tests 'flvm[30]' -v -e --time` — 1/1 PASS (cooperative runtime stress).
- A combined command that ran `flvm[29]`, `[22]`, `[28]`, and `[30]` was interrupted by the command timeout while building/running `[30]`; `[30]` was rerun separately and passed.
- `make all -j2` — PASS (`done: all`).
- `make docs` — PASS (49 sections).
- `bin/run_tests 'flvm[15]' -v -e --time` — 1/1 PASS (active VM run prevents runtime destruction).
- `bin/run_tests 'flvm[17]' -v -e --time` — 1/1 PASS (runtime reuse after failed VM invocation).
- Full default suite (1165 discovered tests) — NOT RUN. No pthread/thread-safety or worker-pool readiness is claimed. The source snapshot has no `.git` metadata, so branch/commit state cannot be verified.


## Archive 291 — worker-pool architecture specification

### Scope

Specification/audit only. No runtime C implementation, pthread worker, public API, task lifecycle behavior, or Channel semantics were changed.

### Source findings

- Re-read `src/common/flat_runtime.c`, `include/flat_task.h`, `include/flat_channel.h`, `src/common/flat_channel.c`, `src/common/flat_vm_handle.c`, `src/common/vm.c`, native plugin host, logger, environment/path/locale helpers, memory stream fallback and WASM bridge.
- Confirmed that task registry/state/wait graph/scheduler operations are not consistently protected by the existing runtime atomic gate.
- Confirmed that the per-Channel atomic gate is not a pthread mutex and does not protect runtime registry/wakeup operations.
- Confirmed that native plugin registry/lazy preparation has process-wide locks, but native handler internals are not thereby thread-safe.
- Identified the mutable function-static HTTP multipart sequence in `src/lib/mod_http.c::http_ensure_boundary()` as a concrete unsynchronized shared-state item. The public `flat_runtime_run()` header permits concurrent runs on one runtime, but this does not certify every native handler for concurrent invocation.
- Recorded per-task VM ownership, deep-copy spawn arguments, runtime-owned task/Channel registries, logger, env/path/locale, memory stream and WASM constraints.

### Deliverables

- Added `plans/06_WORKER_POOL_SPEC.md` with verified current ownership, blockers, proposed lock hierarchy, worker dispatch and wakeup-race handling, cancellation safe points, Channel transaction semantics, native/plugin policy, output, startup/shutdown, worker failures, platform fallback and acceptance gates.
- Updated `plans/01_DECISIONS.md` with the worker-pool design baseline and explicit open API questions.
- Updated `plans/02_ROADMAP.md` with archive-291 completion and remaining gates.
- Updated `plans/03_RUNTIME_AUDIT.md` with source-level findings and the proposed synchronization baseline.
- Updated `plans/README.md` and the main README checkpoint.

### Validation

- `make all -j2` — PASS (`done: all`).
- `make docs` — PASS (49 sections).
- `bin/run_tests 'flvm[22]' -v -e --time` — 1/1 PASS.
- `bin/run_tests 'flvm[28]' -v -e --time` — 1/1 PASS.
- `bin/run_tests 'flvm[29]' -v -e --time` — 1/1 PASS.
- `bin/run_tests 'flvm[30]' -v -e --time` — 1/1 PASS (individual rerun).
- `bin/run_tests native -v -e --time` — 22/22 PASS.
- `bin/run_tests native_use -v -e --time` — 6/6 PASS.

A later combined selected run passed DOM, FLVM, HTTP and regex regressions, then timed out during the native suite after 19/22 cases; the full `native` suite and `native_use` suite were rerun individually and passed. The full default suite was not run. No complete-VM worker test exists in this checkpoint and none is claimed as passing. No pthread worker pool or general thread-safety guarantee is introduced.

### Next

Complete the static mutable-state audit across embedded native modules and supported optional plugins; fix or serialize the HTTP multipart sequence; then settle plugin concurrency and cancellation/shutdown API contracts before implementing workers.


## Archive 292 — native module thread-safety audit and HTTP boundary race fix

### Modifications

- Added `plans/07_NATIVE_THREAD_SAFETY_AUDIT.md`, covering all embedded native handler source files and built-in optional plugin entry points, plus directly shared process-owned helpers.
- Classified the main native state boundaries: VM/context-owned state, runtime-shared task/Channel bridge state, process-wide synchronized helper state, host-owned resources, and external library state.
- Replaced the unsynchronized function-static multipart boundary counter in `src/lib/mod_http.c` with `flat_http_boundary_next_sequence()`.
- Added `include/flat_http_boundary.h` and `src/common/flat_http_boundary.c`; the process-wide sequence uses `atomic_uint_fast64_t` and relaxed atomic fetch-add because only uniqueness/allocation ordering is required.
- Added `test/cases/flvm/031_http_boundary_concurrency/`: eight pthread callers allocate 40,000 sequence values and verify all are unique and present.
- Added an explicit worker-mode plugin policy to the audit/specification: `THREAD_SAFE`, `SERIALIZED`, `UNSUPPORTED`; unknown third-party plugins default to `UNSUPPORTED` until explicit capability metadata is implemented.
- Updated `Makefile` so the HTTP build links the new boundary allocator source.
- Updated decisions, roadmap, runtime audit, `plans/README.md`, main README, and the worker-pool specification.

### Validation

- `make all -j2` — PASS (`done: all`).
- `make docs` — PASS (49 sections).
- `bin/run_tests 'flvm[31]' 'http[28-32]' 'flvm[12-14]' -v -e` — 9/9 PASS.
  - `flvm[31]`: 8 pthread callers × 5,000 sequence allocations; uniqueness and complete range verified. A focused ThreadSanitizer build and an AddressSanitizer/UndefinedBehaviorSanitizer build of this C regression also passed.
  - `http[28-32]`: 5/5 multipart integration regressions PASS.
  - `flvm[12-14]`: existing logger, native registry and host staging concurrency regressions PASS.
- Full default suite (1167 discovered tests) — NOT RUN.
- No complete-VM worker test was added; no pthread worker pool or general thread-safety guarantee is claimed.
- Source snapshot has no `.git` metadata, so branch/commit state cannot be verified.

### Remaining gates

- Confirm external dependency concurrency guarantees for Lexbor, PCRE2, libcurl and supported platform C runtimes.
- Implement plugin capability metadata and enforcement.
- Complete runtime scheduler/task/Channel synchronization and lifetime-safe API entry/teardown.
- Add deterministic multi-worker VM tests and sanitizer validation.


## Archive 293 — external dependency concurrency audit and HTTP preflight

### Modifications

- Added `plans/08_EXTERNAL_DEPENDENCY_AUDIT.md` with the upstream/documented concurrency contracts and FlatL integration assessment for bundled PCRE2 10.49, bundled Lexbor 3.0.0, native libcurl and platform C runtime boundaries.
- Added `flat_http_prepare()` to `include/flat_http_transport.h`. The native curl implementation calls the existing once-only `curl_global_init()` path; the WASM Fetch implementation returns success as a no-op.
- Added `CURLOPT_NOSIGNAL=1L` to each native libcurl easy handle to avoid libcurl signal-handler manipulation in threaded use. DNS timeout behavior depends on the configured resolver backend.
- Updated worker-pool specification, native audit, runtime audit, decisions, roadmap, plans index and root README.
- No pthread worker pool, scheduler lock protocol or complete-VM concurrency guarantee was added.

### Validation

- `make all -j2` — PASS after the source changes.
- Selected regressions — 63/63 PASS across the following completed runs:
  - `bin/run_tests 'http[28-32]' 'dom[1-18]' 're[1-8]' 'flvm[29]' 'flvm[31]' -v -e` — 32/32 PASS before adding `flvm[32]`.
  - `bin/run_tests 'flvm[32]' -v -e` — 1/1 PASS (explicit transport initialization).
  - `bin/run_tests 'flvm[29-32]' -v -e` — 4/4 PASS (rerun as part of the later grouped run).
  - `bin/run_tests native -v -e` — 22/22 PASS (individual suite rerun after the grouped command timed out).
  - `bin/run_tests native_use -v -e` — 6/6 PASS.
  - DOM: 18/18; HTTP multipart mock regressions: 5/5; PCRE2 regex: 8/8.
  - `flvm[31]`: eight pthread callers × 5,000 sequence allocations; uniqueness/completeness regression PASS.
  - `flvm[32]`: `flat_http_prepare()` called twice and returns success both times.
- Validation host reports libcurl 8.10.1 with `AsynchDNS` and `threadsafe` features; system PCRE2 reports 10.46. Bundled PCRE2 is 10.49 without JIT. These are environment observations, not a cross-platform matrix.
- Full default suite (1167 discovered tests) — NOT RUN.
- `make docs` — PASS (49 sections).
- Source snapshot has no `.git` metadata; branch/commit state cannot be verified.

### Remaining gates

- Lexbor remains not certified until concurrent independent-document tests and sanitizer runs pass.
- Run concurrent complete-VM tests for PCRE2, DOM and HTTP after the worker scheduler exists.
- Validate libcurl runtime features and resolver behavior on each supported target; call `flat_http_prepare()` before worker creation.
- Implement plugin capability metadata/enforcement, cancellation request semantics, shutdown modes, scheduler synchronization and worker lifecycle.


## Archive 294 — Lexbor worker-mode status and serialized fallback

### Decision / documentation changes

- Changed Lexbor/DOM worker audit wording from an apparent exclusion/unsupported state to `PENDING_THREAD_SAFETY_VALIDATION`.
- Kept the eventual capability set unchanged: `THREAD_SAFE`, `SERIALIZED`, `UNSUPPORTED`. Pending is a qualification status, not a runtime capability.
- Agreed to attempt independent per-VM DOM concurrency first. If this cannot be certified, `SERIALIZED` is the preferred fallback; `UNSUPPORTED` is reserved for a demonstrated uncontainable constraint.
- Explicitly included Lexbor-backed document destruction in `flat_dom_runtime_destroy()`/VM teardown in the serialized boundary. Serializing only `dom.*` handler calls would be incomplete.
- Required the future DOM gate to be process-wide, bounded to a handler or teardown operation, and never nested with scheduler/Channel locks or held across task suspension, VM execution or blocking I/O.
- Kept Lexbor global allocator configuration setup-only and immutable while workers are active.
- No runtime code, mutex, worker pool, or DOM worker allowlist was added.

### Validation

- Documentation consistency checked across decisions, roadmap, runtime audit, worker-pool specification, native audit and external dependency audit.
- `make all -j2` — PASS (`done: all`).
- `make docs` — PASS (49 sections).
- `bin/run_tests 'dom[1-18]' -v -e` — 18/18 PASS (existing single-runtime DOM regressions only).
- Full default suite (1167 discovered tests) — NOT RUN.
- No concurrent complete-VM DOM test exists yet; no Lexbor worker-mode capability is claimed.

### Next

Add deterministic concurrent complete-VM DOM tests after the scheduler exists, validate bundled and supported system-Lexbor variants under sanitizers, then choose `THREAD_SAFE` or `SERIALIZED` from evidence. Keep `UNSUPPORTED` only for a demonstrated uncontainable constraint. Continue with cancellation/shutdown and plugin capability contracts before pthread worker implementation.


## Archive 295 — bundled Lexbor independent-context concurrency probe

### Changes

- Added `test/cases/flvm/033_lexbor_concurrency/lexbor_concurrency_test.c` and its `case.toml` to the existing `bin/run_tests` framework.
- The C test compiles the exact bundled Lexbor 3.0.0 amalgamation and runs eight pthreads. Each thread repeatedly owns and destroys its own HTML document, CSS parser, selector engine, callback state and serialization buffer.
- Each document lifecycle covers HTML parse, CSS selector parse/find, attribute mutation/readback, tree serialization, and cleanup. Total: 2,000 documents per run.
- No production runtime or DOM wrapper code changed. No process-wide DOM mutex was added.
- Updated decisions, roadmap, runtime audit, worker-pool specification, native/external dependency audits, DOM documentation, plans index and root README.

### Validation

- `bin/run_tests 'flvm[33]' -v -e` — 1/1 PASS.
- Direct bundled Lexbor pthread regression — 8 threads / 2,000 documents PASS.
- ThreadSanitizer build/run — PASS; no race reported in this test.
- AddressSanitizer + UndefinedBehaviorSanitizer build/run — PASS; no error reported.
- `bin/run_tests 'dom[1-18]' -v -e` — 18/18 PASS.
- `make all -j2` — PASS (`done: all`).
- `make docs` — PASS (49 sections).
- System-Lexbor variant — NOT RUN; no `lexbor` pkg-config package was available.
- Complete-VM concurrent DOM test — NOT RUN; current runtime/scheduler is not yet safe for concurrent host-driven VM runs.
- Full default suite (1168 discovered tests) — NOT RUN.
- Source snapshot has no `.git` metadata; branch/commit state cannot be verified.

### Conclusion and remaining gates

The bundled Lexbor independent-context test provides positive concurrency evidence and makes `THREAD_SAFE` a stronger candidate. It does not certify FlatL's full DOM integration or runtime. DOM remains `PENDING_THREAD_SAFETY_VALIDATION`; `SERIALIZED` remains the fallback if later complete-VM/build-matrix evidence requires it. Worker creation remains blocked by scheduler, task, Channel, cancellation, plugin and runtime-lifetime synchronization work.


## Archive 296 — cancellation request and worker shutdown specification

### Changes

- Added `plans/09_CANCELLATION_SHUTDOWN_SPEC.md` with the proposed request-only cancellation API, cancellation state/ownership rules, safe-boundary semantics, cleanup order, DRAIN/CANCEL lifecycle modes, join-before-destroy ordering, and future worker acceptance matrix.
- Updated `01_DECISIONS.md`, `02_ROADMAP.md`, `03_RUNTIME_AUDIT.md`, `06_WORKER_POOL_SPEC.md`, `07_NATIVE_THREAD_SAFETY_AUDIT.md`, `doc_src/TASK_RUNTIME_MODEL.md`, `plans/README.md`, and the root README.
- Preserved the current cooperative `flat_runtime_task_cancel()` behavior and host-thread confinement.
- No production C code, public API, asynchronous cancellation queue, shutdown implementation, pthread worker, or plugin metadata implementation was added. Archive 296 is specification-only.

### Validation

- `bin/run_tests 'flvm[23]' -v -e` — PASS (1/1); existing cooperative wait/wakeup/cancellation safe-point regression.
- `bin/run_tests 'flvm[29]' -v -e` — PASS (1/1); existing Channel wait graph, cancellation/failure cleanup, task/Channel lifetime and teardown guards.
- `bin/run_tests 'flvm[30]' -v -e` — PASS (1/1); cooperative task/Channel stress regression.
- `bin/run_tests 'flvm[33]' -v -e` — PASS (1/1); independent bundled Lexbor context pthread regression.
- `make all -j2` — PASS (`done: all`).
- `make docs` — PASS (49 sections).
- Test discovery reports 1168 tests; the full default suite was not run.
- No new runtime/test API was implemented in this specification-only archive.
- Source snapshot has no `.git` metadata; branch/commit state cannot be verified.

### Remaining gates

- Implement and test plugin capability metadata/enforcement.
- Implement scheduler synchronization, worker ownership, cancellation request/cleanup queue, DRAIN/CANCEL, join and runtime lifetime protocol.
- Add actual pthread tests for cancellation-vs-completion, cancellation of RUNNING/WAITING tasks, blocked DRAIN, CANCEL cleanup, and join/destroy ordering.
- Run complete-VM Lexbor/PCRE2/HTTP concurrency tests after the scheduler exists.


## Archive 297 — native module capability metadata and worker preflight

### Implemented

- Added the capability enum and process-wide module metadata table in `include/flat_native_capability.h` / `src/lib/native_capability.c`.
- Added built-in defaults, unknown-module rejection, pre-start overrides, and `flat_native_runtime_freeze_worker_capabilities()` (which serializes the freeze against lazy plugin loading). `PENDING` maps conservatively to `SERIALIZED`.
- Preserved `flat_native_plugin_init(reg)` and added optional plugin metadata symbol `flat_native_plugin_thread_capability()`. Bundled optional plugin entrypoints explicitly declare `THREAD_SAFE`; legacy external plugins without metadata are marked `UNSUPPORTED` for worker admission.
- Added `flat_native_runtime_worker_policy()` to prepare a module, reject unsupported worker use, and return `THREAD_SAFE` or `SERIALIZED`.
- Added `test/cases/flvm/034_native_capabilities/` (`flvm[34]`) for policy mapping, unknown-module rejection, override/freeze behavior and concurrent reads.
- Added normative documentation `doc_src/NATIVE_THREAD_CAPABILITIES.md` and linked it in `doc_src/index.json`.

### Not implemented

- No per-module mutex gate exists yet; `SERIALIZED` is returned as policy, not automatically applied around calls.
- The native dispatcher does not yet invoke worker preflight because worker mode and worker ownership do not exist.
- No pthread worker pool, scheduler synchronization, cancellation request API, DRAIN/CANCEL or join implementation exists.
- Built-in `THREAD_SAFE` declarations are source-audit admission baselines, not complete-VM concurrency certifications. DOM remains pending and task/channel remain unsupported.

### Validation

- `make all -j2` — PASS.
- `bin/run_tests 'flvm[34]' -v -e` — PASS (1/1; 8 threads, 1,600,000 capability-policy reads).
- `nm -D` confirmed `flat_native_plugin_thread_capability` is exported by all seven bundled optional plugin shared objects.
- `bin/run_tests 'flvm[31-35]' -v -e` — PASS (5/5).
- `bin/run_tests native -v -e` — PASS (22/22).
- `bin/run_tests native_use -v -e` — PASS (6/6).
- `bin/run_tests 'dom[1-18]' -v -e` — PASS (18/18).
- `flvm[34]` capability metadata test under ThreadSanitizer — PASS (8 threads, 1,600,000 policy reads).
- `flvm[34]` capability metadata test under AddressSanitizer/UndefinedBehaviorSanitizer — PASS.
- `make docs` — PASS (50 sections).
- Full default suite (1170 discovered tests) — NOT RUN.

## Archive 298 — platform mutex abstraction and native invocation gates

### Implemented

- Added `include/flat_sync.h` with POSIX `pthread_mutex_t`, Windows `SRWLOCK`, and single-threaded Emscripten no-op mutex implementations. The abstraction intentionally contains only mutex lock/unlock; thread create/join and condition variables remain future work.
- Added one static process-wide invocation gate per canonical module in `src/lib/native_capability.c`, exposed through `flat_native_module_gate_enter()` / `flat_native_module_gate_leave()`.
- Refactored `flat_native_invoke()` into a common wrapper and inner dispatcher. The wrapper resolves the canonical module and gates `SERIALIZED` and effective `PENDING` calls; `THREAD_SAFE` calls are not gated.
- Kept `UNSUPPORTED` modules callable in the current cooperative runtime. Worker-mode rejection remains the responsibility of `flat_native_runtime_worker_policy()` and future worker admission.
- Added the DOM teardown gate to `flat_dom_runtime_destroy()`, so Lexbor document destruction shares the same `dom` gate as DOM native calls.
- Added `test/cases/flvm/036_native_invocation_gates/` (`flvm[36]`) for exclusive serialized access, independent per-module progress, and DOM teardown blocking behind the gate.
- Added `plans/10_NATIVE_SYNC_GATES.md` and updated native capability documentation.

### Not implemented

- No pthread worker pool, scheduler mutex/condition-variable protocol, ready queue, worker ownership, or wake-pending protocol.
- No asynchronous cancellation request, DRAIN/CANCEL shutdown, worker join, or worker-owned VM cleanup.
- No worker admission enforcement in the scheduler. The existing worker preflight is available, but no worker dispatch boundary exists.
- No complete-VM concurrency certification for modules classified `THREAD_SAFE`; DOM remains `PENDING_THREAD_SAFETY_VALIDATION`.
- No Windows or Emscripten pthread runtime validation in this environment.

### Validation

- `make all -j2` — PASS.
- `make docs` — PASS (50 sections).
- `bin/run_tests 'flvm[31-36]' -v -e` — PASS (6/6).
- `bin/run_tests 'flvm[23-25]' -v -e` — PASS (3/3); `flvm[26]` and `flvm[27]` also PASS individually; `flvm[28-30]` PASS (3/3).
- `bin/run_tests native -v -e` — PASS (22/22).
- `bin/run_tests native_use -v -e` — PASS (6/6).
- `bin/run_tests 'dom[1-18]' -v -e` — PASS (18/18).
- `flvm[36]` under ThreadSanitizer — PASS (serialized plugin first-use dispatch, independent gates and DOM teardown gate).
- `flvm[36]` under AddressSanitizer/UndefinedBehaviorSanitizer — PASS.
- A combined `flvm[23-36]` run encountered intermittent runner/build timeouts around rebuilding the `flvm[26]`/`flvm[27]` integration binaries. Those IDs were rerun separately and passed; no complete grouped-range pass is claimed.
- Test discovery reports 1171 tests; the full default suite was NOT RUN.
- Windows and Emscripten builds were not available for validation.

## Archive 299 — scheduler synchronization foundation

### Implemented

- Extended `include/flat_sync.h` with cross-platform mutex lifecycle, condition variables, and thread create/join. POSIX uses pthreads; Windows uses SRWLOCK/CONDITION_VARIABLE and `_beginthreadex`; single-thread Emscripten explicitly rejects blocking waits and thread creation while retaining no-op mutexes.
- Added `include/flat_scheduler_sync.h` and `src/common/flat_scheduler_sync.c` with a dynamically growing MPMC FIFO ready queue of task IDs.
- Added `FlatTaskDispatchState` with `queued`, `worker_owned`, `wake_pending`, and dispatch tokens. Wakeups during worker ownership are coalesced and requeued once on release; stale tokens are rejected. Queue destruction rejects an open or non-drained queue.
- Added `test/cases/flvm/037_scheduler_sync_primitives/` (`flvm[37]`) to the existing `bin/run_tests` framework.
- Added `plans/11_SCHEDULER_SYNC_FOUNDATION.md` and updated architecture decisions/roadmap.

### Not implemented

- The ready queue and dispatch state are not connected to `FlatRuntime` or `FlatTaskRecord`.
- The current `FlatRuntime` atomic gate has not been replaced; task registry, wait graph, state queries, and task lifecycle APIs are not yet safe for concurrent host callers.
- No worker threads execute VM tasks. No scheduler mutex/condition wait loop, cancellation request, DRAIN/CANCEL, join, or worker-owned teardown exists.
- Channel registry and per-Channel atomic gate have not been converted to the future scheduler/Channel mutex protocol.
- No complete-VM concurrent execution or worker-pool readiness is claimed.

### Validation

- `flvm[37]` — PASS (4 producers, 4 consumers, 20,000 unique queue IDs; dispatch ownership/wake-pending/token checks; close/drain/broadcast).
- Full build, documentation build, sanitizer results and other selected regression results are recorded below after execution.

### Validation results

- `make all -j2` — PASS.
- `make docs` — PASS (50 sections).
- `bin/run_tests 'flvm[37]' -v -e` — PASS (1/1; 4 producers, 4 consumers, 20,000 unique IDs).
- `bin/run_tests 'flvm[29]' -v -e` — PASS.
- `bin/run_tests 'flvm[30]' -v -e` — PASS.
- `bin/run_tests 'flvm[34]' -v -e` — PASS.
- `bin/run_tests 'flvm[36]' -v -e` — PASS.
- Focused `flvm[37]` binary under ThreadSanitizer — PASS.
- Focused `flvm[37]` binary under AddressSanitizer/UndefinedBehaviorSanitizer — PASS.
- Test discovery reports 1172 tests; the full default suite was NOT RUN.
- Windows and Emscripten builds were not available for validation. The current Linux build validates the POSIX implementation only.
- `bin/run_tests 'flvm[36]' -v -e` — PASS (rerun individually after a combined validation command hit the execution timeout at this case).


## Archive 300 — FlatRuntime scheduler mutex integration

### Implemented

- Replaced `FlatRuntime`'s `atomic_bool` spin gate with `FlatMutex scheduler_mutex`; mutex initialization/destruction is part of runtime lifecycle.
- Protected task ID reservation and registry publication, task start/wait/cancel/state/result/destroy, native task bridge registry lookups, wait-graph changes, active-run/scheduler-driver counters, and Channel registry/reference acquisition.
- Embedded `FlatTaskDispatchState` in `FlatTaskRecord`. The cooperative scheduler claims a token under the mutex, executes `flat_vm_step()` outside it, then publishes task result/state and releases ownership under the mutex.
- Integrated queued/worker-owned/wake-pending transitions into task start, wait, Channel wait, wakeup, cancellation and scheduler yield handling. Runtime selection remains a task-registry scan; the MPMC ready queue is not yet used.
- Rejected overlapping `flat_runtime_schedule()` calls. Rejected synchronous cancel/destroy when `dispatch.worker_owned` remains set, including a transient WAITING state published by a native wait callback before `flat_vm_step()` returns.
- Added owner-aware Channel acquisition so task validation and Channel reference pinning occur atomically under scheduler-registry then Channel locking. Automatic transaction abort/rollback now wakes waiters on each Channel released by task cleanup.
- Added `test/cases/flvm/038_runtime_scheduler_mutex/` (`flvm[38]`) using the existing `bin/run_tests` framework. It runs 128 tasks, starts 64 while scheduling is active, performs concurrent state queries, rejects a second scheduler driver, and rejects both cancellation and destruction of a RUNNING task. It also blocks the DOM gate while another thread cancels a yielded DOM task and destroys a CREATED DOM task, proving scheduler queries continue while VM teardown waits.
- Added `plans/12_RUNTIME_SCHEDULER_MUTEX.md` and updated the architecture decisions, roadmap, runtime audit, normative task runtime model, README and plans index.

### Not implemented / limitations

- No pthread worker pool or worker loop. Only one scheduler-driving thread is permitted.
- The MPMC ready queue remains a standalone primitive; the runtime still scans its task registry.
- `FlatChannel` still uses its existing per-Channel atomic gate; conversion to `FlatMutex` and the final lock-order audit remain open.
- No request-only cancellation API, DRAIN/CANCEL shutdown, condition-variable worker wait, worker join or post-join runtime teardown.
- `flat_runtime_destroy()` still requires the host to prevent concurrent new API entries.
- No complete-VM thread-safety or worker-pool readiness is claimed. Windows and Emscripten were not available for validation.

### Validation

- `make all -j2` — PASS.
- `make docs` — PASS (50 sections).
- `flvm[22]`, `[23]`, `[24]` — PASS individually.
- `flvm[25]` — PASS individually and in 100 direct repeated executions of the compiled test binary.
- `flvm[26]`, `[27]`, `[28]`, `[29]`, `[30]` — PASS individually.
- `flvm[31-38]` — PASS individually, including new `flvm[38]`.
- `native` — PASS (22/22); `native_use` — PASS (6/6); `dom[1-18]` — PASS (18/18).
- `flvm[38]` repeated 10 times before the final cancellation guard and 5 times after it — PASS.
- Focused `flvm[38]` under ThreadSanitizer — PASS (128 tasks; concurrent queries and starts; overlapping scheduler, RUNNING-task cancel and destroy rejected; DOM-gated VM cancellation/destruction leaves scheduler queries unblocked).
- An AddressSanitizer/UndefinedBehaviorSanitizer build attempt exceeded the execution timeout during full-source compilation; no ASan/UBSan result is claimed for archive 300.
- `bin/run_tests` reported 1173 discovered tests; the full default suite was NOT RUN.
- Windows and Emscripten builds were not available; only POSIX/Linux synchronization was exercised.


## Archive 301 — build target parity fixes

### Implemented

- Replaced the inline `flvm[32]` `pkg-config` invocation with `test/cases/flvm/032_http_prepare/run.sh`. The script uses libcurl pkg-config flags when available and executable, otherwise falls back to `-lcurl`.
- Reconciled `Makefile.wasm` with the current runtime source graph by adding `flat_locale.c`, `flat_runtime_log.c`, `flat_channel.c`, `flat_scheduler_sync.c`, `flat_vm_handle.c`, `native_capability.c`, `mod_task.c`, `mod_channel.c`, and the shared `flat_http_boundary.c` implementation.
- Guarded dynamic-plugin staging helpers in `native_plugin_host.c` with `FLAT_HAVE_DLOPEN`; Emscripten static plugin loading does not use these helpers.
- Added `plans/13_BUILD_TARGET_PARITY.md`.

### Validation

- `make all -j2` — PASS.
- `make docs` — PASS (50 sections).
- `bin/run_tests 'flvm[32]' -v -e` — PASS.
- `flvm[32]` with a non-executable `pkg-config` shim earlier in `PATH` — PASS via `-lcurl` fallback.
- `bin/run_tests 'flvm[31-38]' -v -e` — PASS (8/8).
- `bin/run_tests 'flvm[18]' -v -e` — PASS.
- `make -f Makefile.wasm -n ... fleditor-wasm` — PASS; all added source files are present in the generated emcc command.
- Actual Emscripten linking was NOT run: `emcc` is unavailable in this environment. Do not mark the WASM build verified until `make fleditor-wasm V=1` passes with the supported SDK.
- A full `bin/run_tests flvm` attempt hit the execution timeout while building/running the existing VM-owned lifecycle case; no test failure was reported by that attempt. Focused affected regressions passed.
- Test discovery reports 1173 cases; the complete default suite was NOT run.

### Scope / next work

No runtime semantics changed and no worker threads were added. Archive 301 only fixes the HTTP test's linker fallback and restores the missing common/native source files in the WASM target. Channel mutex conversion, lock-order audit and ready-queue integration remain future work from the archive 300 roadmap.


## Archive 302 — ready-queue integration and Channel mutex

### Implemented

- `FlatRuntime` owns and initializes `FlatReadyQueue`; runnable tasks are published to it on spawn/start, wakeup and quantum yield.
- The cooperative scheduler consumes IDs through nonblocking `flat_ready_queue_try_pop()` and claims dispatch under the same `scheduler_mutex` critical section. This avoids stale-ID reuse across WAITING/RUNNABLE transitions.
- Transaction-owning tasks retain priority; their ID is removed from the FIFO before claim. Other tasks follow FIFO queue order.
- WAITING, cancellation and task-destroy transitions remove queued IDs. Wakeups while a task is worker-owned are coalesced and published after ownership release. Queue publication failures leave runnable state recoverable for a later enqueue attempt.
- `FlatReadyQueue` now exposes nonblocking pop and ID removal; blocking pop remains available for future worker threads.
- `FlatChannel.gate` is now a `FlatMutex`; the per-Channel atomic spin gate is removed.
- Runtime creation/destruction owns ready-queue initialization and close/drain/destroy.
- Added `flvm[39]`: eight pthreads concurrently set/read one shared runtime Channel using independent value pools (2,000 iterations per thread). Updated `flvm[37]` to cover try-pop and FIFO removal. Updated `flvm[15]`'s minimal link command to include `flat_scheduler_sync.c`.
- Added `plans/14_READY_QUEUE_CHANNEL_MUTEX_INTEGRATION.md` and updated decisions/roadmap/docs.

### Validation

- `make all -j2` — PASS.
- `make docs` — PASS (50 sections).
- Focused FLVM cases `[15-30]` and `[31-39]` were run individually or in focused groups; all relevant cases passed. Grouped invocations that exceeded the execution window were rerun individually. The full default suite was NOT run.
- `flvm[37]` under ThreadSanitizer — PASS.
- `flvm[39]` under ThreadSanitizer — PASS (8 threads × 2,000 set/read iterations).
- `flvm[38]` under ThreadSanitizer — PASS on a diagnostic rerun (128 tasks; concurrent starts/queries and DOM teardown gate).
- AddressSanitizer/UndefinedBehaviorSanitizer compilation for `flvm[39]` exceeded the execution time limit; no ASan/UBSan result is claimed.
- POSIX/Linux only. Windows and Emscripten builds were not run in this archive.

### Remaining

- One cooperative scheduler driver remains; no worker thread loop/pool is implemented.
- No request-only cancellation, worker-owned cleanup queue, DRAIN/CANCEL shutdown, or join implementation.
- Runtime destruction still requires the host to prevent concurrent new API entries.
- Full-VM/native worker-mode readiness is not claimed.
- The Emscripten build issue from archive 301 remains an independent pending verification on a machine with `emcc`; archive 302 did not validate the actual WASM link.


## Archive 303 — request-only cancellation and scheduler-owned cleanup

### Implemented

- Added `flat_runtime_task_cancel_request()` with return contract: `0` accepted/idempotent, `1` already terminal, `-1` invalid runtime or unknown task ID.
- Added a per-task `cancel_requested` flag protected by `scheduler_mutex`; the request caller only publishes intent.
- The cooperative scheduler scans pending requests for unowned CREATED/RUNNABLE/WAITING tasks before selecting normal work, claims cleanup, and runs `flat_vm_cancel()` outside `scheduler_mutex`.
- A RUNNING task's current dispatch owner checks the request after `flat_vm_step()` returns and performs cancellation before releasing dispatch ownership. A long-running native call therefore delays cancellation safely.
- Cancellation commit clears outgoing wait metadata, rolls back task-owned Channel transactions, releases Channel references, publishes CANCELLED, then wakes dependents.
- A VM completion/failure reached before the request is observed wins and is not rewritten. The existing synchronous `flat_runtime_task_cancel()` API remains available and unchanged in purpose.
- Added `flvm[40]` covering CREATED/RUNNABLE/WAITING/RUNNING, idempotent requests, unknown IDs, terminal-result preservation, and a DOM-gated in-flight VM step.
- Added `plans/15_CANCELLATION_REQUEST_RUNTIME.md` and updated the normative task runtime model, decisions, roadmap and cancellation/shutdown specification.

### Validation

- `make all -j2` — PASS.
- `make docs` — PASS (50 sections).
- `bin/run_tests 'flvm[40]' -v -e` — PASS (1/1).
- `bin/run_tests 'flvm[23]' -v -e` — PASS (existing synchronous cancellation/wait regression).
- `bin/run_tests 'flvm[38]' -v -e` — PASS (scheduler ownership and concurrent API regression).
- `bin/run_tests 'flvm[39]' -v -e` — PASS (Channel concurrency regression).
- Test discovery reports 1175 cases; the full default suite was NOT run.
- Focused `flvm[40]` under ThreadSanitizer — PASS (including a request issued while the VM is blocked inside the DOM native invocation gate).
- AddressSanitizer/UndefinedBehaviorSanitizer compilation exceeded the execution time limit; no ASan/UBSan result is claimed for archive 303.
- POSIX/Linux only; actual Emscripten build remains pending user validation from archive 301.

### Remaining

- The cooperative scheduler currently scans task records for pending cancellations; there is no dedicated worker cleanup queue yet.
- No pool lifecycle state machine, DRAIN/CANCEL request API, blocked-DRAIN status, worker join or pthread worker loop.
- No complete-VM multi-worker concurrency guarantee.
- Runtime destruction still requires the host to prevent concurrent new API entries.


## Archive 304 — cooperative runtime shutdown state machine

### Implemented

- Added lifecycle states `FLAT_RUNTIME_RUNNING`, `FLAT_RUNTIME_DRAINING`, `FLAT_RUNTIME_CANCELLING`, and `FLAT_RUNTIME_STOPPED`.
- Added `flat_runtime_shutdown_request()` for DRAIN/CANCEL, plus lifecycle and shutdown-status queries. Repeated requests are idempotent; DRAIN cannot downgrade CANCELLING; DRAIN-complete runtimes can escalate to CANCEL when CREATED records remain.
- DRAIN rejects new task creation/start, native `task.spawn`, and new direct `flat_runtime_run()` calls. Existing started tasks remain schedulable. CREATED records are retained and are not auto-started.
- A quiescent DRAIN with unresolved WAITING tasks reports `FLAT_RUNTIME_SHUTDOWN_BLOCKED`.
- CANCEL publishes request-only cancellation for every nonterminal task, including CREATED; existing scheduler safe-point cleanup performs VM and Channel cleanup.
- Runtime destruction after a shutdown request requires STOPPED, in addition to the existing idle/task/Channel constraints.
- Added `flvm[41]` for DRAIN completion, admission rejection, CREATED retention and post-DRAIN escalation, blocked DRAIN, no downgrade, and CANCEL cleanup.
- Added `plans/16_RUNTIME_SHUTDOWN_STATE_MACHINE.md`.

### Validation

- `make all -j2` — PASS.
- `bin/run_tests 'flvm[41]' -v -e` — PASS (1/1).
- `bin/run_tests 'flvm[40]' -v -e` — PASS (request-only cancellation regression).
- `bin/run_tests 'flvm[38]' -v -e` — PASS (scheduler mutex/ownership regression).
- `bin/run_tests 'flvm[39]' -v -e` — PASS (Channel mutex concurrency regression).
- `make docs` — PASS (50 sections).
- Focused `flvm[41]` under ThreadSanitizer — PASS.
- Test discovery reports 1176 cases; full FLVM/default suite — NOT RUN.
- ASan/UBSan for the new lifecycle test — NOT RUN.
- POSIX/Linux only.

### Explicit limitations

- This is a cooperative shutdown lifecycle, not a worker pool. The host must call `flat_runtime_schedule()` to advance tasks and cancellation cleanup.
- No pthread worker creation, condition-variable worker loop, startup rollback, shutdown broadcast, or actual worker join is implemented.
- No worker-owned cancellation queue exists; the scheduler still scans pending cancellation requests.

### Next

Implement real worker creation/loop/join and convert the host-driven scheduler progress path into worker dispatch while preserving this lifecycle state machine.


## Archive 305 — runtime-owned worker pool

### Implemented

- Added `flat_runtime_workers_start(runtime, count, quantum)` and `flat_runtime_workers_join(runtime)` using the cross-platform `FlatThread` abstraction.
- Added startup barrier: worker threads do not execute tasks until the entire requested pool is created. Partial startup failure broadcasts stop and joins all threads already created.
- Converted `schedule_active` from a single-driver flag to an active scheduler-turn count for internal worker dispatch. The public host-driven `flat_runtime_schedule()` remains exclusive and is rejected while workers are started.
- Each worker executes at most one VM quantum per scheduler turn. Multiple workers may own different tasks concurrently; dispatch state and tokens prevent duplicate ownership. `flat_vm_step()` and native handlers remain outside `scheduler_mutex`.
- Added ready-queue wake epochs and non-consuming wait. Workers capture the epoch before attempting a quantum, preventing lost cancellation/shutdown notifications between state check and condition wait.
- Integrated DRAIN/CANCEL lifecycle with worker exit. STOPPED broadcasts to idle workers; join requires STOPPED and zero active scheduler turns. Runtime destruction rejects unjoined worker state.
- Request-only cancellation wakes idle workers. CANCEL includes CREATED tasks; DRAIN preserves CREATED tasks and may remain blocked by unresolved WAITING tasks.
- Added VM native-manifest preflight before worker startup and before new task publication in worker mode. Permitted optional plugins are loaded before capability freeze; HTTP manifests call `flat_http_prepare()` before workers; unsupported/unknown declarations reject startup/task creation. A process-wide native worker-mode guard also rejects unsupported dispatch as defense in depth; SERIALIZED/PENDING modules retain their process-wide invocation gate. The guard also applies to other runtimes while a pool is active.
- Promoted built-in `task` and `channel` capabilities from UNSUPPORTED to SERIALIZED after scheduler/wait-graph and Channel state synchronization. `dom` remains PENDING/effectively SERIALIZED; undeclared legacy plugins remain UNSUPPORTED.
- Added `flvm[42]`: four workers, 96 tasks, DRAIN and join, CANCEL and join, UNSUPPORTED `math` preflight rejection before startup and for new tasks after startup, and pre-join destroy/direct-run/scheduler admission checks.
- Added `plans/17_RUNTIME_WORKER_POOL.md` and updated decisions, roadmap, README, native capability and Task Runtime documentation.

### Validation

- `make all -j2` — PASS.
- `make docs` — PASS (50 sections).
- `bin/run_tests 'flvm[41]' -v -e` — PASS.
- `bin/run_tests 'flvm[42]' -v -e` — PASS.
- `flvm[38]`, `[39]`, `[40]`, `[41]`, and `[42]` each passed in separately completed runs.
- 100 consecutive plain executions of the `flvm[42]` test binary — PASS after fixing a worker shutdown race discovered during stress.
- Five consecutive ThreadSanitizer executions of the earlier worker-pool regression shape — PASS. The final expanded test was not re-run under TSAN: GCC rebuild exceeded the 300-second command limit and the available Clang TSAN runtime failed to link due missing libdispatch/Blocks symbols.
- A grouped run `flvm[38-42]` timed out after printing PASS for `[38]`, `[39]`, `[40]`; the remaining cases were run separately. No full FLVM/default suite pass is claimed.
- Windows worker execution was not tested. The single-threaded Emscripten branch was syntax-checked and rejects worker startup before capability freeze; `emcc` is unavailable for an actual WASM link. ASan/UBSan and the full FLVM/default suite were not run.

### Limitations

- Native capability preflight covers each VM's declared optional-native manifest; runtime-generated native calls outside that manifest remain protected only by the dispatcher guard.
- The worker-mode guard is process-wide, so unsupported native calls from other runtimes are rejected while any pool is active.
- Worker pool is fixed-size and one-shot per runtime. No resizing, restart, timed join or detached workers.
- No broad multi-worker task.wait/Channel contention, HTTP, DOM, plugin lifecycle or dependency stress matrix has been completed. This archive is not a blanket complete-VM thread-safety certification.
- Runtime destruction still requires host-side exclusion of new API entry; workers must be joined first.


## Archive 306 — worker task.wait and Channel contention audit

Archive 306 adds `flvm[43]`, which runs the existing `027_channel_native/program.fl` through a four-worker runtime pool. It checks two child tasks contending on a shared Channel transaction, parent `task.wait()` completion, the rule that a task cannot suspend while holding a Channel transaction, and DRAIN/join/teardown. The complete lifecycle is repeated 20 times in one process.

Validation: `make all -j2` PASS; `bin/run_tests 'flvm[43]' -v -e` PASS (20 runtime lifecycles); the dedicated binary also passed all 20 runs under GCC ThreadSanitizer with `halt_on_error=1`. This is a focused scenario and does not establish general wait-graph, cancellation, HTTP/DOM, plugin, Windows or Emscripten thread safety. No runtime behavior changed.

Next validation priorities: multi-chain wait graph under workers, simultaneous cancellation and Channel retry/wakeup, HTTP/DOM worker-mode lifecycle, plugin loading races, and deterministic partial worker-start failure injection.


## Archive 307 — worker wait-graph and cancellation audit

### Implemented

- Added `flvm[44]` with a four-task dependency chain `A -> B -> C -> D`; the reverse dependency `D -> A` is rejected as a cycle.
- The test requests cancellation of middle task B before worker startup, starts four workers, then drains. B becomes CANCELLED, A wakes and completes, and C wakes after D completes. DRAIN reaches STOPPED and workers join, demonstrating no stale wait edge in this scenario.
- Added a Channel waiter scenario: a waiter is driven into WAITING on a transaction owned by another task, receives request-only cancellation, and then worker-mode CANCEL cleans both waiter and owner. After join, both task records and the Channel can be destroyed.
- Added `make test-worker-wait-graph-cancel` and `plans/19_WORKER_WAIT_GRAPH_CANCELLATION_AUDIT.md`. Runtime synchronization semantics were not changed.

### Validation

- `make all -j2` — PASS.
- `make docs` — PASS (50 sections).
- `bin/run_tests 'flvm[40]' -v -e` — PASS.
- `bin/run_tests 'flvm[41]' -v -e` — PASS.
- `bin/run_tests 'flvm[42]' -v -e` — PASS.
- `bin/run_tests 'flvm[43]' -v -e` — PASS.
- `bin/run_tests 'flvm[44]' -v -e` — PASS.
- Dedicated test binary: 100 consecutive plain executions — PASS.
- Dedicated test binary: six consecutive GCC ThreadSanitizer executions with `halt_on_error=1` — PASS, no TSAN report.
- Test discovery reports 1179 cases; full FLVM/default suite — NOT RUN. ASan/UBSan, Windows and actual Emscripten link — NOT RUN.

### Remaining

- No broad worker-mode HTTP/DOM lifecycle or plugin loading race matrix.
- No deterministic partial worker-creation failure injection test.
- No Windows worker execution or actual Emscripten build validation.
- This audit does not certify arbitrary wait-graph topologies or complete-VM thread safety.


## Archive 308 — Termux test portability

### Implemented

- Fixed `types[108]` standalone link inputs by adding `src/common/flat_locale.c` and `_GNU_SOURCE`.
- Reworked `types[72]` corruption helper to insert a trailing byte inside the SCHM section at the SCHM/JBMX boundary, preserving subsequent offsets and asserting `invalid SCHM section`.
- Reworked `flvm[38]` to use a controlled DOM gate hold point so the RUNNING-state API checks do not depend on polling a short-lived VM instruction.
- Increased the `flvm[40]` wait deadline for the blocked DOM call from 5s to 30s.
- Added `plans/20_TERMUX_TEST_PORTABILITY.md`.

### Validation

- `flvm[38]`, `flvm[40]`, `types[72]`, `types[108]` — PASS individually.
- Dedicated `flvm[38]` and `flvm[40]` binaries — five consecutive runs each PASS.
- `make all -j2` — PASS.
- `make docs` — PASS (50 sections).
- Full suite and Termux device rerun — NOT RUN.

### Limitations / next

The Termux log supplied exact linker errors for `types[108]` but did not include verbose failure details for `flvm[38]` and `[40]`. Their updated tests pass on this POSIX host; Termux confirmation is still required. After confirmation, resume the archive 307 roadmap: worker-mode HTTP/DOM lifecycle, plugin preparation/load races, and deterministic partial worker-start failure injection.


## Archive 309 — worker HTTP and DOM lifecycle audit

### Implemented

- Added `flvm[45]`, a real loopback HTTP server integration. Thirty-two tasks issue independent libcurl requests through four runtime workers; response status/body are asserted and the server records simultaneous active connections. The test unsets `FLAT_HTTP_MOCK_FILE`, ensuring the real curl transport is used. Worker startup preflight initializes curl through the HTTP manifest before thread creation.
- Added `flvm[46]`, which executes 48 independent DOM document lifecycles across four workers and asserts each VM's document contents.
- Added a DOM cancellation case: the host holds the serialized DOM invocation gate, the worker reaches RUNNING and blocks entering DOM, request-only cancellation is published, the gate is released, and CANCEL must complete VM teardown, join and destruction.
- Added Makefile targets `test-worker-http` and `test-worker-dom`.
- Added `plans/21_WORKER_HTTP_DOM_LIFECYCLE_AUDIT.md`.
- No runtime scheduling or capability policy changed. DOM remains `PENDING_THREAD_SAFETY_VALIDATION` with effective `SERIALIZED` fallback.

### Validation

- `make all -j2` — PASS.
- `make docs` — PASS (50 sections).
- `bin/run_tests 'flvm[45]' -v -e` — PASS.
- `bin/run_tests 'flvm[46]' -v -e` — PASS.
- HTTP worker test binary repeated 10 times — PASS; maximum observed simultaneous loopback connections: 4.
- DOM worker test binary repeated 10 times — PASS.
- Final GCC ThreadSanitizer builds: HTTP 10/10 consecutive runs PASS; DOM 10/10 consecutive runs PASS.
- HTTP ASan/UBSan run — PASS. DOM ASan/UBSan build exceeded the execution limit before producing a test binary.
- The complete default suite, Windows and actual Emscripten link were not run. The grouped `flvm[38-46]` invocation hit the command time limit; every selected case 38–46 passed in separately completed invocations.

### Next

Audit plugin loading/preflight races and add deterministic worker-thread creation failure injection before making any worker-pool policy or scheduler changes.


## Archive 310 — Termux worker-test failure diagnostics

- Corrected `flvm[34-35]` to match the implemented worker capability policy: `task` and `channel` are `SERIALIZED`; legacy plugin and post-freeze lazy loading remain rejected.
- Improved `bin/run_tests` exec failure messages to include the tail of captured command output.
- `make all -j2`, `flvm[34-35]`, and individual `flvm[38]`, `[40]`, `[42]`, `[43]`, `[44]`, `[45]`, `[46]` passed on the available POSIX host. `flvm[43]` also passed five direct repeated runs.
- The six failures originally observed on Termux are not yet diagnosed. No runtime behavior was changed; use the improved output in the next Termux run before making platform-specific changes.


## Archive 311 — complete shell-capture tails

### Implemented

- POSIX `flat_port_shell_capture()` now wraps the entire command list in `( ... ) 2>&1`, ensuring stderr from an earlier failed compile/build stage is captured, not only stderr from the final command.
- POSIX capture drains the pipe fully and stores a rolling tail of up to 4095 bytes.
- Windows capture also drains the child pipe while retaining the final output bytes.
- Added `port[2]` and `port[3]` regression cases for compound-command stderr and large-output tail retention.
- Added `plans/23_SHELL_CAPTURE_DIAGNOSTICS.md`. No runtime or worker implementation changed.

### Validation

- `make all -j2` — PASS.
- `bin/run_tests 'port[2-4]' -v -e` — PASS.
- `flvm[34-46]` — every case passed in an individually completed invocation.
- Grouped `flvm[34-46]` did not complete within the available command limit; this is recorded as NOT PASS rather than inferred from individual runs.
- Termux rerun — PENDING.


## Archive 312 — worker-test stage markers

- Added stderr stage markers to the compound commands for `flvm[38]`, `[40]`, `[42]`, `[43]`, `[44]`, `[45]`, and `[46]`. The markers identify FlatL compilation, harness compilation, and harness execution.
- `make all -j2` — PASS.
- `bin/run_tests 'port[2-4]' -v -e` — PASS.
- Each of the seven focused worker tests passed individually on the available POSIX host. One combined shell loop hit its overall time limit while running `[45]`; `[45]` and `[46]` then passed in separate invocations.
- Termux validation — PENDING. No runtime implementation or semantics changed.


## Archive 313 — signal-aware process status

- [x] Preserve POSIX `pclose()` signal termination as a negative signal number rather than mapping it to exit code 1.
- [x] Format signal termination distinctly in `bin/run_tests` error output.
- [x] Keep normal exit codes unchanged and leave Windows process capture unchanged.
- [x] Add `port[5]` to validate POSIX signal status preservation.
- [ ] Re-run `flvm[38-46]` on Termux and determine whether each failing harness exits normally or is killed by a signal.

**Archive 313 result:** a local POSIX `popen()`/`pclose()` reproduction confirmed that a child terminated by SIGSEGV is observable as `WIFSIGNALED` with signal 11. The change is diagnostic-only. Termux validation is pending.


## Archive 318 — multi-runtime worker-mode ownership

- Extended `flvm[42]` with independent-runtime worker-mode reference tests: second-runtime partial-start rollback, overlapping pools, one-runtime join while another remains active, last-runtime release, and concurrent joins on two distinct runtimes.
- Audited `g_worker_mode_users`: the existing atomic reference count and each runtime's `worker_mode_entered` ownership flag satisfy the tested interleavings; no production synchronization change was justified.
- Added `plans/30_MULTI_RUNTIME_WORKER_MODE_OWNERSHIP.md` and updated the worker-pool plan and roadmap.
- Validation: `make all -j2`, `make docs`, all 13 selected tests (`flvm[13]`, `[34-36]`, `[38-46]`) passed individually; final expanded `flvm[42]` passed under AddressSanitizer and UndefinedBehaviorSanitizer. A grouped 13-test run and a later loop exceeded the execution limit, so only completed individual test runs are counted. Full 1185-test suite, Windows worker execution and actual Emscripten linking remain unverified.


## Archive 319 — FlatThread join ownership

- Added `workers_joining`, protected by `scheduler_mutex`, so only one caller can join a given runtime's worker handles at a time. Concurrent same-runtime join callers are rejected; a failed join clears the operation claim but preserves unjoined handles for retry.
- `flat_runtime_destroy()` now also rejects while the join operation owns worker state. Concurrent destroy/API entry remains unsupported and must be excluded by the host.
- Windows `flat_thread_join()` now checks `CloseHandle()` and preserves the stored handle if closing fails. POSIX `pthread_join()` keeps the valid marker on error and clears it only on success; native error codes are preserved.
- Extended `flvm[42]` with four simultaneous callers joining the same stopped runtime; exactly one caller must succeed.
- Validation: `make all -j2` PASS; `flvm[42]` PASS; dedicated worker-pool harness 20/20 PASS. Windows execution, actual Emscripten linking and the full 1185-test suite remain unverified.

**Next:** follow `plans/02_ROADMAP.md`: broaden worker-mode stress coverage for multiple wait graphs and simultaneous cancellation/cleanup interleavings, while tracking Windows and Emscripten as separate platform validation gates.


## Arhiva 320 — worker multi-graph cancellation stress

A fost adăugat testul `flvm[47]`, care creează opt grafuri independente de câte patru task-uri, lansează simultan opt cereri de anulare din thread-uri host și verifică DRAIN, join, stările terminale și cleanup-ul complet. Harness-ul repetă scenariul de cinci ori per execuție.

Verificări locale POSIX: `make all -j2`, `make docs`, `flvm[39]`, `flvm[40-47]` PASS în rulări individuale; `flvm[47]` a trecut și cinci execuții consecutive, ASan și UBSan. Nu a fost necesară modificarea codului de producție. Matricea generală de stress rămâne deschisă; Windows, build-ul Emscripten real și suita completă de 1186 teste nu sunt validate în această etapă.


## Arhiva 321 — worker Channel / shutdown stress

A fost adăugat `flvm[48]` cu două scenarii: (1) 32 de writer tasks pe patru Channels, opt tranzacții per Channel, sub patru workers, cu verificarea valorilor finale; (2) patru Channel waiters cu patru owner tasks CREATED, DRAIN BLOCKED, patru cereri concurente de anulare, DRAIN până la STOPPED, join al worker pool-ului și apoi CANCEL cooperativ pentru owner tasks. Testul verifică stările terminale și distrugerea task-urilor/Channel-urilor după eliberarea referințelor.

Validare locală POSIX: `make all -j2`, `make docs`, `flvm[48]`, cinci execuții directe consecutive și harness-ul sub ASan, UBSan și TSAN — PASS. Testele `[39]`, `[41]`, `[42]`, `[44]`–`[48]` au trecut în rulări finalizate individuale; rularea grupată `[39] [41-48]` a depășit limita după `[43]`, iar `[44]`–`[48]` au fost reluate separat. Nu a fost necesară modificarea codului de producție. Suita completă de 1187 teste, Windows și linking Emscripten real rămân neverificate.

Archive 322: added `flvm[49]` for converging wait-graph DAG cancellation stress. The worker pool implementation is unchanged; platform validation remains open.


## Archive 323 — staged worker DAG cancellation

`flvm[50]` extends the converging DAG coverage with four independent 10-task graphs, deterministic 2–4 / 5–3 leaf fan-out splits, eight real workers and cancellation staged at RUNNING and WAITING states. One graph drains untouched as a control. Three rounds validate exact terminal states, worker join and full task/runtime cleanup. `flvm[47–50]`, `make all -j2`, and `make docs` passed locally; `flvm[50]` also passed six consecutive direct executions and ASan/UBSan/TSAN. No production runtime change was needed. The full suite and Windows/Emscripten worker gates remain open.


## Archive 324 — seeded worker DAG stress

- Added `flvm[51]` with reproducible seeded DAG generation (16 tasks, acyclic forward-only wait edges, variable fan-in/disconnected components) and four concurrent request-only cancellation callers.
- Six deterministic seeds run twice; the test compares topology/cancellation signatures, then checks exact terminal states after six-worker DRAIN, joins workers and destroys all resources.
- No production scheduler or public API change was required. The case complements `flvm[50]`, which continues to cover cancellation during live RUNNING/WAITING execution.
- Validation: `flvm[51]` runner PASS, three additional direct executions PASS, ASan PASS, UBSan PASS, direct TSAN harness PASS. The TSAN runner invocation exceeded the command limit and is not counted. `flvm[47-49]` passed before a grouped command timeout; `flvm[50]` and `[51]` passed separately. `make all -j2` and `make docs` PASS (50 sections).
- User-reported Termux validation of archive 320: `bin/run_tests flvm` passed 47/47 with 1186 discovered overall in 7:40.819. This is Android validation of archive 320 only, not tests added later.
- Windows worker execution, actual Emscripten linking and the full default suite remain open. See `plans/36_WORKER_SEEDED_DAG_STRESS.md`.


## Archive 325 — mixed worker shutdown escalation and bounded soak

- Added `flvm[52]`: 24 tasks per runtime (18 started, six CREATED), a forced wait edge from started work into the CREATED suffix, and eight host threads concurrently publishing a deterministic mix of DRAIN and CANCEL requests. Every round requires CANCEL escalation, all 24 tasks terminal as CANCELLED, worker join, and complete task/runtime destruction.
- Repeats 12 runtime lifecycles with distinct deterministic seeds.
- Validation: `bin/run_tests 'flvm[52]' -v -e` PASS; ten additional direct harness executions PASS (120 runtime cycles / 2,880 task instances); ASan PASS. Normal harness compilation PASS. UBSan compilation exceeded the available command limit and is not counted; TSAN not run. No production scheduler change was required.
- User-reported archive-320 Android baseline remains 47/47 FLVM tests PASS; archive 325 and tests 48–52 have not yet been validated on Termux. Windows worker execution and actual Emscripten linking remain open. See `plans/37_WORKER_MIXED_SHUTDOWN_SOAK.md`.


**Archive 326 — worker failure isolation and lifecycle soak:** added `flvm[53]` with 100 runtime create/start/DRAIN/join/destroy cycles per invocation. Each cycle runs two intentional failing tasks alongside six healthy tasks on four workers and verifies that failure remains task-local. The harness passed the normal runner, four additional completed repetitions, and ASan/UBSan/TSAN. `make all -j2` and `make docs` passed. A fifth extra repeat and the grouped `[47-53]` command hit the outer execution limit; those incomplete attempts are not counted. Windows and actual Emscripten validation remain open.


**Archive 327 — Emscripten single-thread worker gate:** audited `flat_sync.h`'s no-pthreads branch and added `flvm[54]`. The test builds the runtime source set with the host compiler, `-D__EMSCRIPTEN__`, and no `-pthread`; it verifies inert mutex operations, rejected blocking wait/thread operations, `flat_threads_supported() == 0`, clean rejection of `flat_runtime_workers_start()`, unchanged RUNNING lifecycle, and successful runtime destruction. The target and runner test passed (1/1). This is a host-compiler branch simulation, not an actual Emscripten build. `emcc`/`emcmake` and Windows execution remain unavailable; no production scheduler change was made. See `plans/39_EMSCRIPTEN_SINGLE_THREAD_WORKER_GATE.md`.


## Archive 328 — Windows FlatSync / FlatThread audit

- Audited the Windows synchronization wrappers and the ready-queue condition-variable predicate loops.
- Corrected `flat_thread_create()` in the Windows and POSIX branches so a fresh wrapper is marked non-joinable before function validation/native creation; callers must pass a fresh or successfully joined wrapper, never overwrite a live handle.
- Removed the Windows-only trampoline write to `FlatThread.result`; the trampoline now owns only its `{fn,arg}` start record and discards the function return value, matching POSIX behavior.
- Added `flvm[55]` and `make test-flat-thread-contract` for failed-create state, condition wakeup, join, and repeated-join rejection.
- Validation on local POSIX host: `make all -j2`, `make docs` (50 sections), `flvm[37]`, `flvm[42]`, `flvm[55]`, and the dedicated `flvm[55]` harness under ASan/UBSan PASS. Windows compile/run remains unavailable; `_WIN32_WINNT >= 0x0600` must be configured for SRWLOCK/CONDITION_VARIABLE APIs. Actual Emscripten build remains unavailable.
