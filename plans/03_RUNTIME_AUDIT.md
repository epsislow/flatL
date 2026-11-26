# Task Runtime — audit tehnic inițial

**Baseline auditat:** `flatL-main(57).zip`  
**Reconfirmat în:** `flatL-main-251-task-runtime-foundation.zip`  
**Scop:** constatări arhitecturale pentru pregătirea task-urilor; nu reprezintă implementare de task runtime.

## 1. Durata de viață VM

`flat_vm_run_opts()` creează un `VmState st` local și inițializează runtime-ul pentru o execuție completă. Bucla interpreter operează pe această stare locală, iar căile normale de terminare apelează `vm_finish()` / `vm_teardown()`.

Modelul este potrivit pentru invocarea VM one-shot actuală, dar nu este încă un obiect task persistent și reluabil. Pentru scheduler cooperativ trebuie să păstrăm explicit starea necesară suspendării/reluării: instruction pointer, call stack, loop frames și starea necesară native execution. Apelarea repetată a `flat_vm_run_opts()` nu poate relua o execuție întreruptă.

## 2. Executor

În baza 252, `src/common/flat_executor.c` folosea stare globală:

- `g_bind` / `g_bind_ok` — binding-ul VM pentru operațiile native executor;
- `g_frames` / `g_frame_sp` — stack-ul de frame-uri executor.

În etapa 253, aceste globale au fost eliminate și mutate în `FlatExecutorContext`, deținut de `VmState`. În etapa 254, același context a preluat și bookkeeping-ul call-instruction. Contextul este transmis explicit prin `FlatNativeCtx`, iar panic drain/unwind folosește același owner.

## 3. Bookkeeping VM call instruction

`src/common/vm.c` folosea bookkeeping global pentru call instructions (`g_vm_call_instr`, `g_instr_stack`, `g_instr_push_call_sp`, `g_last_call_function_sp`). În arhiva 254, stack-ul și indicii de call sunt deținute de `FlatExecutorContext`; resolver-ul primește explicit `VmState *`, iar `FlatNativeCtx.call_instr_id` transmite rezultatul către native calls. Nu există un pointer global către VM activ.

## 4. VCX active runtime

În arhiva 254, `g_vcx_active` și `g_call_instr_resolver` au fost eliminate. Native VCX operations primesc runtime-ul prin `FlatNativeCtx.vcx`, iar call instruction prin `FlatNativeCtx.call_instr_id`. Nu mai există fallback la un runtime global activ.

## 5. Alte globale

Setup-ul VM apelează `setlocale(LC_ALL, "C")` și `flat_native_runtime_reset()`. RNG-ul a fost mutat în contextul VM în arhiva 255, iar date run state în arhiva 256. Rămân globale mutabile în registry-ul pluginurilor native, path separator, environment, memory streams, logger, compiler și WASM; acestea trebuie auditate și clasificate după ownership înainte de worker pool.

Runtime-ul cooperativ inițial trebuie să aibă un model clar single-thread și nu trebuie declarat thread-safe.

## 6. Panic handling

`vm_jump_panic_any()` în forma auditată:

1. folosește `FlatExecutorContext` deținut de VM-ul curent;
2. golește frame-urile executorului și contexte call instruction pending numai în acel context;
3. rezolvă locația panic-ului și înregistrează panic-ul prins;
4. caută `#panic.<loc>`, apoi `#panic.any`;
5. marchează panic-ul nepreluat dacă nu există handler.

În arhiva 254, drain-ul nu mai este cuplat la un executor global: este scoped la contextul VM curent. Când va exista un task persistent, ownership-ul contextului și cleanup-ul trebuie să rămână scoped la task-ul care a eșuat. Un panic într-un task nu trebuie să golească frame-urile altui task sau să distrugă VCX-ul lui.

Extinderea dorită `#panic.#worker.any` este documentată în [01_DECISIONS.md](01_DECISIONS.md); implementarea trebuie să respecte mecanismul actual și să fie acoperită de teste.

## 7. Concluzie

Refactorul F0.3 a mutat executor binding/frame stack și call-instruction bookkeeping în contextul explicit al VM-ului și a eliminat VCX active/resolver globale. Schedulerul nu trebuie introdus înainte de auditul celorlalte stări globale mutabile și înainte de definirea duratei de viață a VM-ului persistent.


## F0.4 progress — per-VM RNG (archive 255)

The process-global RNG (`g_state`, `g_seeded`) was removed. `FlatRngState` now owns the SplitMix64 state, is embedded in `VmState`, and is passed to native calls through the appended `FlatNativeCtx.rng` field. `math.seed`, `math.random`, and `hash.randomBytes` use the same RNG state within one VM, while independent VM contexts have independent RNG states. The random generator remains non-cryptographic.

A C-level regression test (`flvm` case 008) verifies deterministic interleaving and that reseeding one RNG context does not affect another. Existing language tests also verify `math.seed`/`math.random` and the shared `math.seed` + `hash.randomBytes` behavior.

Other global state remains. In particular, `flat_native_runtime_reset()` resets process-wide plugin registration/manifest/loading state, and `setlocale(LC_ALL, "C")` changes process locale. The next audit should classify these globals by process, VM, or task ownership; do not claim reentrancy or thread safety.


## F0.4 progress — per-VM date run state (archive 256)

`g_date_state` and `flat_date_global_state()` were removed. `FlatDateRunState` is now embedded in `VmState` and passed to native handlers through the appended `FlatNativeCtx.date` pointer. `flat_date_run_begin()` snapshots the current `FLAT_DATE_RUN_OFFSET` and `FLAT_DATE_NOW_SEC` values into the owning VM state; `date.run_offset()` mutates only that state.

A C-level regression test (`date` case 082) checks independent initialization and mutation of two date run-state instances. The full date suite was confirmed in three bounded runs: cases 1–48 (48/48), 49–81 (33/33), and 82 (1/1). A combined 1–82 run hit the execution time limit after case 50, so the split results are the authoritative verification.

### Native plugin host ownership finding

`native_plugin_host.c` currently mixes process registry state (`g_handlers`, `g_loaded_*`, `g_search_dir`) with per-program authorization state (`g_manifest`, `g_manifest_count`). `flat_native_runtime_reset()` clears and reconstructs the handler registry and resets the manifest for every `flat_vm_run_opts()` call. This lifecycle is unsafe for overlapping VM runs: one run can clear or replace the registry/manifest while another is using it. No registry lifecycle refactor is included in archive 256; the next step is to make the manifest an explicit per-VM input and make process registry initialization/loading independent of VM reset.

## F0.4 progress — native plugin registry and per-VM manifest (archive 257)

The native plugin host no longer stores program authorization in process-global `g_manifest` / `g_manifest_count`. `FlatNativeRuntimeContext` carries the immutable manifest pointer and count for the owning VM; `VmState` owns this context and `FlatNativeCtx.native_runtime` passes it to native dispatch. The `flat_io_exec_line_manifest()` entry point also builds a temporary runtime context from its explicit manifest arguments, so nested native evaluation retains the same authorization scope.

`flat_native_runtime_reset()` was replaced by `flat_native_runtime_init()`. Embedded handlers are registered once per process and are no longer cleared/rebuilt for each VM run. The handler table, loaded-module flags, and plugin search directory remain process-wide. Optional dynamic/static plugin handlers are cached at process level after loading. Embedded core handlers are separately marked during one-time registration so they retain core-runtime behavior and are not mistakenly required to appear in the optional plugin manifest.

An important authorization rule was made explicit: an optional plugin being present in the process handler table does not authorize every VM to call it. The current VM's manifest is checked before reusing a previously loaded optional handler. Embedded core handlers are separately marked during process initialization and remain directly available as core runtime operations; they are not confused with optional plugin handlers. This distinction preserves the existing `mem` behavior while preventing a previously loaded optional module from bypassing another VM's manifest.

A regression test (`flvm` case 009) verifies that two `FlatNativeRuntimeContext` instances with different manifests remain independent. Native/plugin regression suites also cover alias resolution, lazy loading, core `mem`, math, list, object, CSV, and fmod use.

**Concurrency limitation:** initialization and lazy plugin loading still mutate process-wide state without synchronization. `flat_native_runtime_init()` is intentionally documented as not thread-safe. The refactor removes per-run reset/manifest interference but does not establish concurrent VM safety or worker-pool readiness. Process-level plugin loading and search-path configuration must be frozen or synchronized before parallel execution.

## F0.4 progress — locale and stdio process boundary (archive 258)

Numeric parsing/formatting no longer changes the host process locale. The VM-level `setlocale(LC_ALL, "C")` call was removed. `include/flat_locale.h` provides locale-independent `flat_strtod()` and `flat_snprintf()` helpers: POSIX builds use a shared immutable C numeric locale initialized once with an atomic guard, selected only for the current thread via `uselocale()`, Windows uses `_strtod_l` / `_vsnprintf_l` with a C numeric locale, and WASM retains its C-locale runtime behavior. The C locale object is shared and immutable after atomic one-time initialization; POSIX applies it only to the current thread with `uselocale()`. No process-global locale mutation or per-thread locale-object leak is introduced.

All FlatL numeric parsing sites that previously used `strtod()` were routed through `flat_strtod()` (VM loop limits, value parsing/operations, typed number parsing, math expressions, date numeric inputs, and JSON query numbers). Numeric serialization/formatting sites using `%g` / `%f` were routed through `flat_snprintf()` (number formatting, JSON serialization, HTTP form values, and `str.from_num`). This keeps decimal points stable even when the embedding host has selected a non-C numeric locale.

Stdio setup is now process initialization rather than VM initialization. `flat_port_stdio_setup()` is no longer called by `flat_vm_run_opts()`; the `flvm` and `flatl` CLI entry points call it once. Embedding hosts that require Windows text-mode stdio must call the documented host/process setup themselves. This prevents each VM run from resetting the host process's stdio mode.

Validation on archive 258: `make all -j2` PASS; `make docs` PASS (45 sections); `flvm` 9/9; `math` 34/34; `json` 50/50 (two bounded runs); `date` 82/82 (three bounded runs; final implementation uses the shared immutable locale with thread-scoped `uselocale()`); `io` 53/53 across three bounded runs; `io_ctx` 13/13; `port` 1/1; `values` 9/9; `str` 16/16; `http` 32/32; `types[1-31,50-58]` 40/40 from the first bounded attempt and `types[59-100]` 42/42. Full default suite (1144 discovered) was not run. Locale behavior under a non-C locale was not directly exercised because locale availability is host-dependent; implementation no longer calls process-global `setlocale()`.

**Still not thread-safe:** process-wide plugin registry lazy loading, environment overrides/argv, path separator, fallback memory-stream slot table on platforms without `open_memstream`, and the runtime logger remain process-owned mutable state. Their lifecycle/synchronization must be defined before worker-pool execution. This archive does not establish VM reentrancy or thread safety.

### Process-owned state classification after archive 258

| State | Current owner | Current use / constraint |
|---|---|---|
| Native plugin handlers, loaded flags, search directory | Process | Registration/lazy loading mutate shared state; not synchronized. |
| Environment overrides and program argv (`flat_env.c`) | Process | CLI resets/configures before its run; embedded overlapping runs cannot use independent overrides/argv. |
| Path separator (`flat_path.c`) | Process | CLI sets it before run; two embedded VMs cannot select different separators concurrently. |
| Runtime logger (`flat_runtime_log.c`) | Process | CLI-configured shared sequence/buffer; not synchronized. |
| Fallback memory-stream slots (`flat_memstream.c`, non-glibc) | Process | Shared slot table keyed by `FILE *`; requires synchronization or per-owner registry before threaded use. |
| Stdio text-mode setup | Process initialization | Moved out of `flat_vm_run_opts()`; CLI calls once, embedding host owns setup. |
| Numeric locale | Thread-local conversion scope | No process-global `setlocale()`; C numeric locale is cached per thread for parse/format helpers. |

Archive 258 addresses locale side effects and per-VM repeated stdio setup, but does not isolate environment or path configuration. These remain explicit limitations for embedding multiple independently configured VMs in one process.

Additional baseline comparison: `env` reports 3/14 failures and `path` 1/9; the same failing cases were reproduced on the unmodified archive 257, so they are pre-existing and not regressions introduced by 258.


## Archive 259 follow-up — environment, path, and memory streams

`FlatEnvContext` and `FlatPathContext` are now owned by each `VmState`; native calls receive them through `FlatNativeCtx.env` and `FlatNativeCtx.path`. CLI/WASM setters remain process staging interfaces, copied at VM startup. The environment override/argv snapshot is deep-copied. Process `getenv()` is still consulted when enabled and is assumed immutable while a VM is running. Path native operations no longer read the mutable process separator after startup.

The non-GNU memory-stream fallback replaced its fixed 16-entry global array with a dynamically allocated linked registry. An atomic-flag lock protects registry insertion/removal; the stream itself and its output targets remain owned by the caller. This removes the fixed-capacity failure and makes registry bookkeeping safe against concurrent independent opens/closes, but does not make unrelated runtime state thread-safe.

The runtime logger remains process-owned: CLI configuration, sequence/drop counters, buffer, and stdout emission are global. It is currently configured by the `flvm` process entry point, not by each VM. It needs an explicit lifecycle/serialization contract before concurrent VM logging is supported.


## F0.4 progress — logger synchronization, compiler context, and test fixture repair (archive 260)

The four previously failing regression cases were corrected at the source-fixture level. Native calls that are expected to raise a panic now execute as standalone native instructions with an explicit output binding (`native.call(...) -> p`) rather than being nested inside `io.outln(...)`. The nested form caused the outer IO argument evaluator to report its own argument error before the native panic detail reached the test harness. No runtime panic semantics were weakened or changed. The corrected cases are `env[4]`, `env[8]`, `env[9]`, and `path[6]`.

The runtime logger remains process-owned by design (one CLI invocation), but its mutable state is now protected by a C11 `atomic_flag` lock. Configuration replacement, enabled checks, event sequence allocation, bounded-buffer writes, dropped counter reads, flush, and shutdown are serialized. This protects the logger's own state; it does not make VM execution generally thread-safe, and callers must still not race unrelated process-level configuration such as plugin loading or environment/path staging.

Compiler emission state is now invocation-owned. The former mutable globals `g_compile_schemas`, `g_compile_panic_sites`, `g_emit_panic_sites`, `g_emit_insn_ip`, and `g_emit_panic_loc` were removed from `src/common/compile.c`. A stack-local `FlatCompileContext` now carries the schema catalog pointer, panic-site collection, and current emission location/IP through variable counting, block measurement, and instruction emission. This removes cross-compilation interference from these fields; it is not a blanket guarantee that every compiler/linker subsystem is concurrently reentrant.

The WASM last-result API (`flat_wasm_get_last_termination()` / `flat_wasm_get_last_panic()`) remains a module-level singleton for the current single-instance FLEditor/WASM bridge. It is explicitly not suitable for concurrent independent WASM runs; a future multi-instance bridge should return a result object per invocation rather than rely on the legacy getters. Run-limit configuration and environment/path staging are also module-level inputs that must be set before a run and not mutated concurrently.

Validation on archive 260:
- `make all -j2` — PASS.
- `bin/run_tests env path flvm flc panic -v -e --categ=all` — 44/44 PASS. This includes env 14/14, path 9/9, flvm 12/12, flc 3/3, and panic 6/6.
- The four repaired cases were first rerun individually — 4/4 PASS.
- `make docs` — PASS (45 sections).
- Full default suite (1147 discovered tests) was not run.

**Concurrency boundary:** the logger's own process state is synchronized, compiler emission state is local to each compile invocation, and WASM result getters remain singleton-only. Native plugin lazy loading and process-level environment/path staging still need a freeze/synchronization contract. Do not declare general VM reentrancy or worker-pool readiness.

## F0.4 progress — native plugin registry synchronization (archive 261)

The process-wide native plugin host now has two synchronization domains. A lifecycle lock serializes embedded-handler initialization, plugin search configuration, and optional-module lazy loading. A separate registry lock protects handler publication and lookup. `flat_native_invoke_optional()` copies the handler pointer while protected, then releases all locks before invoking native code; plugin handlers never execute under the registry/lifecycle locks.

Dynamic plugin initialization is transactional: registrations made by the initializer are staged in thread-local storage and published to the process registry only when the initializer returns success. If initialization fails, staged pointers are discarded before the shared library is closed, preventing partially registered pointers into an unloaded module. Embedded-registration marking is also thread-local, so an unrelated host registration cannot accidentally be classified as an embedded core handler while initialization is in progress.

`FLAT_NATIVE_PATH` is snapshotted on the first optional plugin load. The host must configure this environment variable before the first optional load; later environment changes do not affect plugin search. `flat_native_runtime_set_search_dir()` remains mutable process configuration, but updates are serialized with lazy loading. The process environment itself is not made safe against concurrent external `setenv()` calls during the first snapshot.

Regression test `flvm[13]` starts eight threads that concurrently initialize the registry, update the same search directory, and prepare the same optional math handler. It then verifies that a second runtime context with an empty manifest is still denied after the process-wide handler has loaded. This validates serialized initialization/lazy loading and preservation of per-VM authorization. It does not test concurrent invocation of arbitrary VM code.

Validation on archive 261: `make all -j2` PASS; `make docs` PASS (45 sections); `flvm` 13/13 PASS; `native` 22/22 PASS; `native_use` 6/6 PASS; `env` 14/14 PASS; `path` 9/9 PASS; `fmod` 18/18 PASS; `math` 34/34 PASS; `list` 5/5 PASS; `obj` 17/17 PASS; `csv` 36/36 PASS. The full default suite (1148 discovered tests) was not run.

**Remaining boundary:** native registry initialization and lazy loading are serialized, and handler execution occurs outside locks. Process environment staging and path staging still require the host to configure before VM start and not mutate concurrently. Runtime logger is synchronized internally. WASM result getters remain singleton-only. No general VM reentrancy, full thread safety, or worker-pool readiness is claimed.


### Process-owned state after archive 261

| State | Owner / synchronization | Remaining constraint |
|---|---|---|
| Native plugin handlers and loaded flags | Process; lifecycle and registry atomic-flag locks | Plugin init must register synchronously on its initializer thread; handlers execute outside locks. |
| Native plugin search directory | Process; serialized with lazy loading | Host may update through setter; configure `FLAT_NATIVE_PATH` before first optional load (snapshot). |
| Environment overrides / argv staging | Process staging, snapshotted per VM | Host must configure before VM start and avoid concurrent staging mutation. |
| Path separator staging | Process staging, snapshotted per VM | Host must configure before VM start and avoid concurrent staging mutation. |
| Runtime logger | Process; internally serialized | Output is still a process-wide stream and logger lifecycle is process-scoped. |
| WASM last-result bridge | Module singleton | Not suitable for multiple independent/concurrent WASM instances. |
| Compiler emission context | Per compile invocation | Other compiler/linker state still needs a final global audit. |
| Fallback memory stream registry | Process; registry operations synchronized | Stream/buffer lifetime remains caller-owned. |

The native plugin lifecycle synchronization is complete for initialization and lazy loading, but this is not a declaration of full VM thread safety.

## Archive 262 — transversal global-state audit and remaining process boundaries

The archive 261 source tree was scanned for mutable file-scope/static state, including non-`g_*` function-local statics. The audit found several small initialization/cache races that were not visible in the earlier `g_*` inventory. Archive 262 synchronizes those sites and makes the host environment/path staging snapshots internally consistent.

### Changes implemented

- `flat_env.c`: process staging (`g_env`) is protected by an atomic-flag lock. Set/reset/lookup/argument operations and `flat_env_context_init()` are serialized; each VM snapshot is now copied from one consistent staging state. The borrowed pointer returned by legacy `flat_env_arg()` remains valid only until the next staging mutation. The actual host process environment accessed through `getenv()` is outside this lock and must remain immutable while VMs run.
- `flat_path.c`: process staging separator `g_sep` is an atomic byte. VM context initialization performs an atomic snapshot; path operations with a `FlatPathContext` remain independent after initialization.
- `flat_http_curl.c`: `curl_global_init()` is serialized with an atomic once-state. Successful initialization is published once; a failed initialization is latched and reported to later callers rather than racing/repeating partially initialized global setup.
- `flat_time.c`: Windows QueryPerformanceFrequency initialization uses `INIT_ONCE` / `InitOnceExecuteOnce`, removing the racy function-local `init` flag.
- `mod_math_mdim.c`: `FLAT_MDIM_DEBUG` is sampled once through an atomic initialization state; concurrent first calls no longer race on function-local `inited`/`on` variables. As with other `getenv()`-derived process settings, set the variable before first use and do not mutate the process environment while the runtime is active.
- `native_registry.c`: fallback signature formatting now uses a thread-local buffer instead of one process-shared static buffer.
- `flat_wasm.c`: `flat_wasm_mods()` now formats into a thread-local buffer and no longer races on a mutable `ready` flag/shared buffer.
- `flat_port_win.c`: temporary shell-script sequence allocation uses `InterlockedIncrement` rather than an unsynchronized static counter.
- Added `flvm[14]` (`test/cases/flvm/014_host_staging_concurrency`) to race environment/path staging writers against independent per-VM context snapshots.

### Ownership matrix after archive 262

| State | Owner / synchronization | Remaining constraint |
|---|---|---|
| VM execution state, executor, call-instruction, VCX, RNG, date, env snapshot, path snapshot, native manifest | VM | VM lifetime is still one-shot; no persistent task object yet. |
| Compiler emission context | Compile invocation (stack-local) | Linker/parser must still be checked independently for hidden shared mutable state. |
| Native handler table, loaded plugin flags, search path | Process; lifecycle and registry locks | Successful dynamic libraries remain process-loaded; arbitrary plugin code may itself have globals outside FlatL's control. |
| Runtime logger | Process; atomic-flag lock | One shared sequence/buffer/output; lock serializes calls but output may block. |
| Fallback memory-stream registry | Process; atomic-flag lock | Lock protects lookup registry only; caller owns stream I/O and must not close/use the same stream concurrently. |
| Environment override/argv staging | Process; atomic-flag lock; copied atomically into VM | Host `setenv`/`putenv` is external and must not race with VM `getenv()` reads. Legacy `flat_env_arg()` returns a borrowed pointer invalidated by staging mutation. |
| Path separator staging | Process; atomic byte; copied into VM | Staging remains one shared default for subsequently initialized VMs, not a per-host-instance configuration object. |
| Numeric C locale | Process immutable locale object; atomic one-time init; thread-scoped use | Host locale changes do not alter FlatL conversions; WASM uses its C-locale runtime. |
| libcurl global init | Process; atomic once-state | No global cleanup is performed during VM teardown; libcurl global lifetime is process lifetime. |
| Windows QPC frequency | Process immutable cache; Windows INIT_ONCE | Windows-specific path not compiled/tested in this Linux run. |
| `FLAT_MDIM_DEBUG` | Process immutable cached flag; atomic first-use init | Set before first math-mdim use; process environment must not be concurrently mutated. |
| Native doc signature fallback buffer | Thread-local | Returned pointer is overwritten by the next fallback call on the same thread. |
| WASM last termination/panic and run limits | WASM bridge singleton | Legacy FLEditor bridge remains single-instance and sequential; no per-instance result/configuration API. |
| WASM env/path setters | Process/Module staging; underlying env/path setters synchronized | Still one shared staging configuration for the current WASM module. |
| WASM compile temp files and `chdir("/fleditor")` | WASM module/filesystem singleton | Fixed filenames and process working-directory mutation make concurrent compile/run calls unsupported. |
| Static native metadata tables | Process immutable | Safe to share. |

### Validation archive 262

- `make all -j2` — PASS.
- `make docs` — PASS (45 sections).
- `flvm` — 14/14 PASS, including the new host-staging concurrency test.
- `env` — 14/14 PASS.
- `path[1-4]` and `path[5-9]` — 9/9 PASS in two bounded runs. A combined `path` invocation stalled after four cases in this environment; splitting the same range completed successfully.
- `http[1-8]`, `http[9-16]`, `http[17-24]`, `http[25-32]` — 32/32 PASS in four bounded runs. One combined invocation reached the execution timeout after printing all case dots; the bounded runs are the authoritative results.
- `math_mdim` — 51/51 PASS.
- `port` — 1/1 PASS.
- The full default suite (1149 discovered tests) was not run.

### Readiness decision

Archive 262 closes the identified small global initialization races and makes internal environment/path staging snapshots consistent. It does **not** make the whole runtime thread-safe. The process environment, process-wide staging model, plugin-defined state, WASM singleton bridge/fixed temp files, and un-audited parser/linker paths remain outside the guarantee. No scheduler, task persistence, pthread worker pool, or Channel implementation is started in this archive.

## Archive 263 — parser/linker audit and explicit host configuration

### Parser/linker audit

- `parse.c`: parser state (current block/function/schema, indentation frames, comment state, local-block serials, line buffers) is invocation-local. No mutable file-scope or function-local static state was found.
- `link.c`: import graph, module records, fmod records, temporary parsed programs, merge indices, and diagnostics are invocation-local. No mutable file-scope or function-local static state was found.
- `compile.c`: emission bookkeeping remains in the per-call `FlatCompileContext` introduced earlier; helper routines use caller-owned FGIR and local scratch state.
- The parser/linker previously reserved large fixed workspaces on the C stack. `discover_modules()` now allocates its module queue and use-line workspace on the heap; `flat_link_program()` allocates module/fmod workspaces on the heap and uses one cleanup path. This reduces per-thread stack requirements for future host worker threads.
- Source syntax classification in parser/linker/function/schema/native-use/label/slot/number/literal helpers now uses `flat_ascii.h`. FlatL syntax is ASCII-defined, so whitespace and identifier classification no longer depends on the embedding process's current C locale.

### Remaining parser/linker host constraints

- Relative imports are resolved relative to source-file paths and ultimately depend on the process working directory for relative entry paths. The host must not call `chdir()` concurrently with parsing/linking; preferably pass absolute entry paths.
- Source files and imported files must remain stable for the duration of one link operation. FlatL does not lock external filesystem writers.
- `FlatLinkOptions.verbose` writes diagnostics to process `stderr`; concurrent verbose link operations may interleave diagnostic lines, but do not share parser/linker state.
- `FgirProgram` is a large caller-owned object. Each concurrent linker call needs its own output program; callers must not share or mutate the same `FgirProgram` concurrently.

### Explicit host configuration API

Added `FlatHostConfig` (`include/flat_host_config.h`, `src/common/flat_host_config.c`) and `FlatVmOptions.host_config`.

- `flat_host_config_init()` creates an independent config with platform defaults, without reading or mutating legacy process staging.
- `flat_host_config_capture_staging()` captures the legacy environment/argv and path staging into an independently owned config; the host must not mutate either legacy staging source while this combined snapshot is being made.
- `FlatHostConfig.env` can be configured with the `flat_env_context_*` APIs; `FlatHostConfig.path` can be configured with `flat_path_context_set()`.
- `flat_vm_run_opts()` deep-copies the supplied environment context and copies the path context at run start. The VM therefore does not borrow host-config memory during execution.
- The same immutable host config may be reused by multiple VM runs. Do not mutate or destroy it concurrently with `flat_vm_run_opts()`; after the call has taken its snapshot, it may be changed or destroyed.
- When `FlatVmOptions.host_config == NULL`, legacy process-staging behavior remains unchanged for CLI and existing embedders.

The new context APIs include `flat_env_context_init_empty()`, `flat_env_context_clone()`, per-context override/use-process setters, and per-context argv operations. The clone validates bounds and performs deep copies.

### Validation archive 263

- `make all -j2` — PASS.
- `make docs` — PASS (45 sections).
- `flvm[10]`, `flc`, `native_use` — 10/10 PASS.
- `types[12-20]`, `types[54-57]`, `func[1-5]` — 15/15 PASS (the runner selected 2 extant func cases).
- A broader `types` run passed its first 46 printed cases but hit the execution timeout; it is not counted as a complete suite.
- A combined `flvm[10] flc func` run passed all flc cases, flvm[10], and the first 31 displayed func cases before timeout; it is not counted as a complete suite.
- Full default suite (1149 discovered tests) was not run.

**Readiness:** parser/linker have no identified shared mutable static state, their large working arrays are now heap-owned per call, and syntax classification is locale-independent. This supports independent concurrent parse/link calls provided callers use distinct `FgirProgram` outputs, stable source files, and a stable process working directory. It does not make the full runtime thread-safe. The explicit host configuration API removes the need for new embedders to coordinate process-global env/path staging, while preserving legacy compatibility. WASM bridge singleton/fixed-path limitations and process/plugin constraints remain. No scheduler or worker pool is introduced.

## Archive 264 — runtime ownership boundary

`FlatRuntime` is introduced as an explicit host/runtime lifetime object. It owns a deep copy of `FlatHostConfig` and supplies that immutable configuration to each VM run. It does not yet own VM instances, bytecode, native plugin modules, logger state, or process-wide registries; those remain process-owned or invocation-owned as documented above. Each `flat_runtime_run()` still creates and destroys a fresh `VmState` through `flat_vm_run_opts()`.

The runtime tracks active calls and refuses destruction while any call is active. The embedding host must serialize the beginning of new calls against `flat_runtime_destroy()`; no internal reference-count scheme can make use of an already-freed raw pointer valid. Concurrent calls through one live runtime are permitted by the API boundary, but actual VM concurrency remains subject to each subsystem's documented constraints and is not certified by this change.

## Archive 265 — runtime resource ownership and failed-run cleanup

The runtime resource audit confirms that archive 264's `FlatRuntime` currently has exactly one runtime-lifetime resource: its owned immutable `FlatHostConfig` snapshot. No existing mutable VM subsystem should be promoted into `FlatRuntime` merely to make the runtime object look more substantial. CSV sessions, DOM documents, VCX state, executor state, file handles, IO contexts, pools/collections, HTTP request slots, RNG/date state, and VM environment/path snapshots all refer to one execution's mutable state and therefore remain owned by that invocation's `VmState` until a persistent-VM design explicitly changes their lifetime.

### Ownership decision

| Resource | Owner | Lifecycle decision |
|---|---|---|
| `FlatRuntime.host` (`FlatHostConfig`) | Runtime instance | Deep-copied at create; immutable while runs may snapshot it; destroyed with runtime. |
| `VmState` and executor/call-instruction state | VM invocation | Created for each `flat_runtime_run()` and destroyed before the call returns. |
| VCX, collection/string pools, CSV, DOM, HTTP request slots, IO context, file table | VM invocation | Keep per-VM; these contain mutable handles/IDs and references into other VM-owned objects. Do not share between runs. |
| RNG and date run state | VM invocation | Keep per-VM; deterministic seed/date controls must not leak between invocations. |
| Environment/path VM snapshots | VM invocation | Clone/copy from runtime config at run start; no borrowing from runtime after setup. |
| Bytecode and `FlatVmOptions` output buffers | Caller | Caller retains ownership and must keep them valid for the duration of `flat_runtime_run()`. Concurrent runs need distinct output buffers. |
| Native handler table, loaded shared libraries, plugin search path | Process | Keep process-owned; runtime/VM manifest authorization remains per VM. Runtime destruction must not unload process-shared plugins. |
| Logger, locale object, libcurl global initialization, fallback memory-stream registry | Process | Keep process-owned; do not flush/shutdown/destroy them when one runtime is destroyed. |
| Parser/linker workspaces and compiler emission state | Compile/link invocation | Keep invocation-owned; no reason to attach them to an execution runtime. |

### Lifecycle and error-path validation

Added `flvm[17]` (`test/cases/flvm/017_runtime_reuse`) to exercise a real VM failure after VM setup, then restore the caller-owned bytecode and run it successfully again through the same `FlatRuntime`. It checks that the failed invocation reports `FLAT_TERM_ERROR` and a diagnostic, that the subsequent successful invocation reports a success termination and clears the prior diagnostic, and that the runtime can then be destroyed. This verifies that VM-owned resources are torn down on the error path and do not poison the next invocation.

This test deliberately corrupts one opcode in the loaded bytecode only for the first call, restoring it before the second call. The runtime does not own or modify bytecode; callers must not mutate bytecode concurrently with a run.

### Resource ownership conclusion

Archive 265 does not add a process-to-runtime resource migration because the audit found no existing mutable resource that is both safe and useful to share across VM invocations. The current ownership boundary is intentionally narrow: `FlatRuntime` owns host configuration; each run owns a fresh `VmState`; process services remain process-owned. A future persistent `FlatVm`/`FlatTask` API may promote selected resources only after defining suspend/resume semantics and proving that their internal references remain valid across yields.

Validation for archive 265: `flvm[15-17]` 3/3 PASS; `make all -j2` PASS; `make docs` PASS (45 sections). The full default suite (1152 discovered tests) was not run. This work does not establish general VM thread safety, persistent VM semantics, scheduler readiness, or worker-pool safety.

## Archive 266 — persistent VM/task lifecycle contract

Archive 265's `FlatRuntime` is reusable but does not preserve VM execution state between calls. The current `flat_vm_run_opts()` allocates `VmState` on its C stack, keeps the instruction pointer and guard scratch arrays as locals (before archive 269), and always calls `vm_teardown()` before returning. Archive 269 moved the IP, entry index, and guard scratch arrays into `VmState` and removed the unused borrowed `FlatVmOptions *` member; the state is still invocation-scoped and is torn down before returning. This is a run-to-completion API, not a suspendable VM.

Before a persistent VM can be introduced, the continuation state must become heap-owned and address-stable. This matters because `FlatVcxRuntime` and `FlatExecutorContext` bind pointers into the VM state; moving the containing object after binding would invalidate those references. The instruction pointer, guard scratch arrays, jump/step counters, and copied scalar options must ultimately be owned by the persistent `FlatVm`; archive 269 has consolidated IP and guard scratch into `VmState`, but has not yet transferred the `VmState` lifetime to the handle. `FlatVmOptions` output pointers must remain call-scoped and must never be stored for later resume.

`FlatBytecode` contains multiple derived pointers into owned storage. A persistent VM must take a canonical deep copy (preferably by reloading from a copied byte buffer through the normal FlatBytecode parser) or require an explicit immutable reference-counted bytecode owner. The first implementation chooses an owned copy to make lifetime independent and testable.

Yield occurs only after a bytecode instruction has fully completed. The resume IP is the next instruction. An opcode that invokes a long-running native is not preemptible; the first cooperative scheduler therefore cannot guarantee fairness while a blocking native is executing. A later async-native continuation contract is needed for nonblocking waits.

Task states, panic/failure/cancellation semantics, runtime destroy-busy behavior, and scheduler-level wait semantics are specified in `doc_src/TASK_RUNTIME_MODEL.md`. Archive 266 adds the specification only; it does not introduce a persistent VM or scheduler.

## Archive 272 — initialized VmState ownership

The ownership boundary has advanced: `flat_vm_create()` now calls the private `flat_vm_state_create()` bridge after the handle has copied bytecode and host configuration. The opaque `FlatVm` stores that `VmState *` and does not move it; this is required because VCX and executor bindings retain pointers into the state. `flat_vm_execute()` invokes the opcode loop against the owned state. On terminal outcome, `vm_finish()` tears down runtime resources but leaves the state allocation owned by the handle until `flat_vm_destroy()`. Destroying a READY VM also tears down its initialized resources; `resources_active` prevents duplicate teardown. The one-shot `flat_vm_run_opts()` wrapper uses the same create/execute/destroy bridge.

This does not yet preserve an executing VM across calls: the opcode loop still runs to terminal outcome in one call. There is no instruction budget distinct from `max_steps`, no `YIELDED` outcome, and no resume operation. Those remain the next acceptance target. Archive 272 validation: `make all -j2` PASS; `make docs` PASS (46 sections); `flvm[1-17]` PASS and `flvm[18]` PASS separately; `func[1-37]` 31/31 PASS; `vcx` timed out after 6 tests and is not declared PASS. Full default suite not run (1153 discovered).

## Persistent VM continuation regression checkpoint — archive 275

`FlatVm` keeps its initialized `VmState` across `flat_vm_step()` calls and yields only at completed bytecode-instruction boundaries. The continuation regression now checks externally visible output equivalence, not only terminal status: the same bytecode is run once through `flat_vm_run_opts()` and once through varying step budgets, and captured stdout must match exactly. Coverage includes nested calls, loops, executor scopes, and caught panic control flow.

Failure/lifecycle coverage also confirms that a VM can yield before a logical `FAIL`, then enter `FLAT_VM_FAILED` with `FLAT_TERM_FAIL`; an unhandled native panic produces `FLAT_VM_FAILED` with `FLAT_TERM_PANIC`; terminal VMs reject further steps; and a yielded VM can be destroyed without executing later instructions. These tests validate the cooperative single-thread lifecycle only. They do not make VM execution generally thread-safe, do not preempt native calls, and do not implement task cancellation or scheduler semantics.


### Task scheduler lifecycle (archive 277)

The cooperative scheduler now tracks one target ID per WAITING task and wakes dependants on all terminal task outcomes, including cancellation. Task cancellation tears down the VM only at a scheduler safe point. These APIs are single-host-thread operations; this does not add cross-thread cancellation or task-registry thread safety. Waiting cycles are not detected.

## Archive 279 — task argument ownership boundary

`task.spawn(#block, ...)` now evaluates each argument in the parent VM and deep-copies it into the child VM's private VCX, collection pool, and string pool. This deliberately avoids cross-VM references into parent-owned memory. LIST/OBJECT graphs are recursively copied; child mutations do not affect parent values. File handles are rejected because their IDs are meaningful only in the source VM's file table. The block's parameter names are serialized in `.fb` v14 BPAR metadata; v3–v13 bytecode remains loadable but has no parameter metadata. This establishes value-copy semantics only; it does not introduce shared memory, Channels, or cross-thread safety.


## Archive 280 — Channel ownership and synchronization

The runtime now owns an explicit Channel registry with monotone numeric IDs. Each Channel has independent string/collection pools and a key/value map. The C API deep-copies supported values across VM pools (including nested LIST/OBJECT) and rejects VM-local file handles. Per-operation C1 synchronization uses a C11 atomic gate per Channel; this is not a pthread mutex and does not imply general VM/runtime thread safety.

C2 transactions keep a deep snapshot and use a registered nonterminal task ID as logical owner. `unlock` commits, `revert` restores the baseline while retaining the transaction lock, and non-owner operations return busy. Task completion, failure, cancellation, and destruction automatically roll back and release all transactions owned by that task. The task cleanup path swaps in the already-owned snapshot, avoiding allocation during terminal cleanup.

The runtime remains busy while Channels are registered. FlatL Channel natives, Channel handle transfer through `task.spawn`, Channel logger events, and pthread worker synchronization remain future work. `flvm[26]` covers deep-copy, transaction, ownership, cleanup, and runtime lifetime behavior.


## Archive 281 — FlatL Channel natives and task handle sharing

The `channel` native module is now available to runtime-owned task VMs. `channel.create()` creates a runtime-owned Channel; `channel.set` and `channel.var` deep-copy supported values across the current VM and Channel pools; `lock`, `unlock`, and `revert` expose C2 transactions; `destroy` explicitly releases an unlocked Channel. Channel IDs are ordinary numeric handles and are copied through existing `task.spawn(#block, ...)` argument binding, so multiple tasks can explicitly address the same runtime-owned Channel without sharing VM pool pointers.

A native `channel.var` uses an unretained deep-copy into the current VM's pools; the VM's normal assignment binding retains the resulting value. The public C API continues returning an owned/retained value for host callers. This distinction prevents a double-retain leak in native result binding.

To make multi-instruction transactions atomic relative to the cooperative scheduler, runnable tasks that own a Channel transaction receive scheduling priority until all their transactions are released. `task.wait()` is rejected while the caller owns any transaction, preventing a task from suspending while holding a Channel lock. Other task operations against a locked Channel still return busy; they are not converted into Channel waiters. Task termination/failure/cancellation still rolls back open transactions.

`channel.destroy()` is explicit and not reference-counted. The program/host must ensure no task will use a handle before destroying it. The scheduler and VM remain single-threaded; C11 Channel gates are not pthread mutexes. `flvm[27]` covers two child tasks sharing one handle, transaction serialization with a one-instruction quantum, revert/commit, nested LIST/OBJECT copy-through, explicit destroy, and rejection/rollback when a task attempts `task.wait()` while holding a transaction.

## Archive 283 — cooperative Channel wait and native retry

Channel contention is no longer a terminal `busy` error for FlatL task-native calls. `channel.lock`, `channel.set`, and `channel.var` can register a wait dependency on the current transaction owner. The VM has a dedicated retry continuation bit: it returns a scheduler wait outcome without advancing the current `OP_NATIVE` IP, so the same operation is re-evaluated after wakeup. This differs from `task.wait`, whose native instruction completes and advances IP before suspension.

A contending task pins the Channel while waiting. Commit/unlock wakes Channel waiters; task-terminal transaction abort is followed by the existing terminal-target wakeup. `revert` deliberately keeps the lock and does not wake waiters. This is still a cooperative single-thread contract; the C API returns busy to host callers and no pthread-safety guarantee is introduced.

`task.result(taskId)` exposes terminal execution metadata as a FlatL OBJECT. Its diagnostic is capped at the inline-string limit, while the C API result remains the full `FlatVmResult`. `flvm[29]` tests lock/set/var wait-and-retry; `flvm[30]` exercises 48 tasks and 24 Channels.


## Archive 284 — audit cooperativ al Channel wait/retry

Revizuirea codului din arhiva 283 a confirmat următoarele trasee: `runtime_task_native_channel_set/var/lock()` transformă contendența într-o dependență scheduler; `runtime_task_wait_channel()` re-verifică lock-ul sub gate-ul runtime/Channel și înregistrează owner-ul tranzacției ca target; `mod_channel.c` marchează retry; `vm.c` păstrează IP-ul instrucțiunii native și nu publică rezultatul provizoriu; unlock și cleanup terminal trezesc waiter-ele.

### Contracte confirmate în cod

- `task.wait()` și Channel retry au continuări diferite: `task.wait()` completează native-ul și reia de la următoarea instrucțiune; retry reia aceeași instrucțiune `OP_NATIVE`/`OP_ASSIGN`.
- Un Channel waiter este legat de task-ul owner și ține referință Channel cât timp este suspendat.
- `unlock` trezește waiter-ele Channel; `revert` nu le trezește, deoarece tranzacția rămâne activă.
- Completion/failure/cancellation ale owner-ului rulează abort/rollback, release de referințe și wakeup pe dependențele task-ului.
- `flvm[29]` acoperă acum cancellation și failure a owner-ului în timp ce waiter-ul este WAITING; în ambele cazuri tranzacția este rollback/released, waiter-ul este trezit și reia `channel.lock`.

### Limite și riscuri rămase

- Wakeup-ul este broadcast logic către waiter-ele Channel-ului, nu FIFO; corectitudinea se bazează pe retry și re-verificarea condiției, nu pe rezervarea lock-ului pentru un waiter.
- C API host direct nu are continuare VM și raportează busy; wait/retry există numai pentru task natives.
- `runtime_wake_waiters()` și `runtime_wake_channel_waiters()` sunt proiectate pentru schedulerul single-threaded. Nu trebuie considerate sincronizate suficient pentru worker pool.
- `runtime_lock` și Channel atomic gate sunt primitive de protecție cooperativă/structurală curente, nu o dovadă că întregul `FlatRuntime`, task registry, VM, native registry sau logger este thread-safe.
- Rămâne verificarea distrugerii task-ului în prezența dependențelor și a referințelor, inclusiv după wakeup și înainte de retry.

**Concluzie:** arhiva 284 întărește corectitudinea schedulerului cooperativ, dar nu schimbă criteriul de blocare pentru P1. Worker pool-ul rămâne interzis până la un audit separat al lock ordering, lifecycle și stării process/VM/native pe toate căile de execuție.


## 16. Archive 285 — task destroy / wait graph / Channel reference interaction

### Confirmed invariants

1. `runtime_has_waiters()` scans the task registry and prevents target destruction while any WAITING task has `waiting_on == target_id`.
2. `flat_runtime_task_destroy()` permits only CREATED or terminal tasks. RUNNABLE waiters (including Channel waiters already woken) must be executed or cancelled before destruction.
3. `flat_runtime_task_cancel()` clears `waiting_on` and `waiting_channel_id`, aborts owned Channel transactions, releases task-held Channel references, marks the task CANCELLED, and wakes tasks that depend on this task.
4. Channel contention registers the transaction owner as the wait-graph target and records the Channel ID separately. The task-held Channel reference survives the wakeup to RUNNABLE because the native instruction still needs to retry.
5. Destroying a CREATED task with an open Channel transaction runs transaction abort and task-reference cleanup. A task with incoming waiters is rejected before this cleanup begins.

### Regression coverage added in archive 285

`flvm[29]` now verifies: owner destroy rejection while a Channel waiter is WAITING; Channel destroy rejection while a waiter/transaction still pins it; waiter cancellation and destruction; owner destruction aborting the remaining transaction; wakeup on commit; rejection of destroy while the woken waiter is RUNNABLE; cancellation before retry; and final Channel destruction after references are released.

### Limits / non-claims

This audit does not make task APIs thread-safe. `runtime_has_waiters()`, task state transitions, task list traversal/removal, wait graph traversal, and scheduler selection are not protected as a single atomic protocol for concurrent host callers. `runtime_lock` is used for selected registry/Channel operations but is not a task-registry mutex. The current API contract confines task registry/scheduler calls to the host thread driving the scheduler. Pthread worker implementation must first redesign this boundary rather than assuming the existing atomic gate is sufficient.


## 17. Archive 286 — prohibit suspension while holding a Channel transaction

### Finding

The archive 285 code rejected `task.wait()` while the caller owned a Channel transaction, but the Channel contention paths had a gap: a task could own Channel A, then encounter a busy Channel B. `channel.lock()` could register a wait edge directly, while `channel.set()` and `channel.var()` delegated to `runtime_task_wait_channel()`. This allowed a task to become WAITING while retaining an unrelated transaction.

### Correction

- `flat_runtime_channel_lock()` now checks whether the caller owns any transaction before it may register a Channel waiter. The ownership scan is performed before acquiring the runtime/target-Channel gates to avoid recursively acquiring a Channel gate.
- `runtime_task_wait_channel()` applies the same guard for `channel.set()` and `channel.var()` retry registration.
- When the guard rejects the wait, the native operation fails and the normal task failure cleanup aborts the caller's open transactions and releases its Channel references.

### Regression coverage

`flvm[29]` now compiles three separate FlatL programs. Each first locks Channel 1 and then contends on Channel 2 using, respectively, `channel.lock(2)`, `channel.set(2, ...)`, or `channel.var(2, ...)`. Every task must become FAILED rather than WAITING. A subsequent host probe successfully locks/unlocks Channel 1, proving the failed task's transaction was rolled back; both Channels can then be destroyed.

### Limits

This is a cooperative suspension invariant, not a pthread lock-ordering solution. It prevents a task from parking while holding any transaction, but does not establish cross-thread safety for the runtime registry, scheduler, Channel list, or VM/native services.


## 18. Archive 287 — multiple Channel ownership and waiters on separate Channels

### Confirmed behavior

- One task can own transactions on Channel A and Channel B at the same time.
- Distinct waiter tasks can register `waiting_on` edges to that same owner while recording different `waiting_channel_id` values.
- Committing Channel A wakes only its Channel waiters. The owner can continue to hold Channel B; its waiter remains WAITING until the owner reaches a terminal state.
- Owner terminal cleanup aborts all remaining transactions before `runtime_wake_waiters(owner_id)` moves the owner dependants to RUNNABLE. Both waiters then retry their original Channel operation and complete.
- The regression verifies the state transition boundary explicitly: both waiters are RUNNABLE immediately after owner completion, before the scheduler is allowed to run them.

### Test correction discovered during the audit

The archive-286 `flvm[29]` test had a false-positive setup: it destroyed Channel 1 before the no-suspend scenarios, but the three FlatL programs still attempted `channel.lock(1)` as their first operation. They therefore failed on an invalid Channel before they could prove that a task holding one transaction was prevented from waiting on another. Archive 287 corrects the test to acquire live Channel 3 and contend on live Channel 4. The test now exercises the intended path rather than merely observing task failure.

### Limits

This confirms cooperative ordering and cleanup only. It does not make `runtime_wake_waiters`, `runtime_wake_channel_waiters`, task state changes, Channel registry traversal, or task destruction safe for concurrent host threads. No runtime implementation change was required for the audited cases; archive 287 strengthens the regression tests and corrects the prior test setup.


## 19. Archive 288 — chained task.wait and Channel dependency audit

### Confirmed behavior

The wait graph stores one outgoing `waiting_on` edge per WAITING task. Channel contention uses the same edge to point to the transaction owner and stores `waiting_channel_id` separately for Channel-specific wakeup and reference lifetime. This allows a Channel waiter to be the target of another task's `task.wait()` dependency.

The new `flvm[29]` sequence verifies this chain:

1. Task B owns a Channel transaction.
2. Task C attempts `channel.lock` and becomes WAITING on B.
3. Task D waits on C; Task A independently waits on B.
4. Host commit/unlock on the Channel wakes C only. D and A remain WAITING.
5. C retries its original Channel operation and completes; D becomes RUNNABLE while A remains WAITING.
6. D completes. B is then started and completes; only then does A become RUNNABLE and complete.

This proves that Channel-specific wakeup and terminal-task wakeup remain distinct even when their dependencies form a mixed chain. It also confirms that multiple incoming edges can target the same owner without conflating their wakeup conditions.

### Cycle interpretation

A valid Channel wait edge points to a task that owns the contended transaction. The cooperative invariant forbids that owner from suspending while it owns any Channel transaction (`task.wait` and Channel contention both reject such suspension). Therefore a valid active transaction owner cannot simultaneously have an outgoing WAITING edge. A mixed cycle that continues through such an owner is excluded by the ownership/suspension invariant, rather than by a special Channel-specific graph algorithm. This does not prove general deadlock freedom for future locks, host synchronization, or worker threads.

### Validation and limits

- `flvm[29]` passes with the new chained-dependency sequence.
- `flvm[23]`–`flvm[30]` each pass in individual runs. The grouped invocation was interrupted during build steps and is not reported as a grouped PASS.
- `native`: 22/22 PASS; `native_use`: 6/6 PASS.
- `make all -j2`: PASS; `make docs`: PASS (49 sections).
- Full default suite (1165 discovered tests) was not run. No runtime implementation change was needed; archive 288 adds regression coverage and clarifies the graph contract.
- No pthread/thread-safety claim is made.

## 19. Archive 289 — cancellation/failure across chained dependencies

The `flvm[29]` regression now exercises three additional mixed wait-graph paths:

1. **Owner cancellation:** B owns a Channel transaction; C waits on the Channel; D waits on C; A waits directly on B. Cancelling B makes C and A RUNNABLE while D remains WAITING. C retries/completes, which wakes D; all three dependants complete independently.
2. **Intermediate waiter cancellation:** C waits on B's Channel and D waits on C. Cancelling C clears C's Channel dependency/reference and wakes D. D is then scheduled while B still owns the Channel, so D may itself enter Channel WAITING on B. The test cancels B before allowing D to complete. This explicitly separates a wakeup edge from successful completion of the dependent's subsequent operation.
3. **Failure after Channel retry:** C waits on B's Channel. D waits on C. B commits/unlocks, C wakes and retries `channel.lock`, then deliberately executes `FAIL`. C reaches FAILED, its cleanup releases the transaction, and D becomes RUNNABLE and completes.

No runtime source change was required. The existing `runtime_wake_waiters`, `runtime_wake_channel_waiters`, cancellation cleanup, and terminal task cleanup implement the tested behavior. The test now includes a dedicated `waiter_fail_after_lock.fl` program. All newly created tasks are destroyed before the final `flat_runtime_destroy()` call, which succeeds.

### Validation and limits

- `make all -j2` — PASS.
- `make docs` — PASS (49 sections).
- `flvm[23]` through `flvm[30]` — each PASS in an individual run. The first multi-case shell batch was interrupted during `flvm[26]` build; `flvm[26]` and the remaining cases were rerun individually and passed. A later `flvm[27-30]` batch was interrupted during `flvm[30]`; `flvm[30]` was rerun individually and passed.
- `native` — 22/22 PASS.
- `native_use` — 6/6 PASS.
- Full default suite (1165 discovered tests) — NOT RUN.
- Runtime implementation unchanged. No cross-host-thread cancellation guarantee, pthread safety, or worker-pool readiness is claimed.


## 20. Runtime teardown and final resource ownership audit (archive 290)

`flat_runtime_destroy()` checks three runtime-owned lifetime counters/conditions: active VM/scheduler runs, registered task count, and registered Channel count. It refuses destruction while any of these remain. It does not force-cancel tasks, resolve waiters, release Channel references, or destroy Channels on behalf of the host.

The focused `flvm[29]` teardown regression exercises these states:

1. **CREATED task:** runtime destruction is rejected while the task record and Channel exist. Destroying the CREATED task and then the Channel permits runtime destruction.
2. **RUNNABLE transaction owner:** runtime destruction is rejected while the task owns a Channel transaction. Channel destruction is independently rejected while the transaction is active. The task cannot be destroyed while RUNNABLE; cancellation aborts/rolls back the transaction, after which task and Channel destruction succeed.
3. **WAITING Channel waiter:** runtime and task destruction are rejected while the waiter is suspended. Cancelling the owner wakes the waiter, but wakeup alone neither removes the waiter record nor releases its Channel reference. Runtime destruction and Channel destruction remain rejected until the waiter is cancelled and its task record is destroyed.
4. **Terminal task records:** completion, failure, and cancellation release task-owned transactions/references as appropriate, but each record remains registered and `task_count` remains nonzero until explicit task destruction. The regression checks all three terminal outcomes and confirms the runtime still refuses destruction.
5. **Fresh runtime:** after successful teardown, a newly created runtime has independent task and Channel registries and starts both IDs at one.

The surrounding existing coverage also checks active-run rejection (`flvm[15]`), registered task rejection (`flvm[22]`), Channel references (`flvm[28]`), and Channel transaction/waiter lifecycle (`flvm[29]`). The current API contract requires the host to prevent new calls from racing with `flat_runtime_destroy()`. `active_runs` protects an already-entered VM/scheduler run; it is not a general lifetime-safe concurrent entry gate. Task registry and scheduler APIs remain single-host-thread-only.

**Conclusion:** the cooperative teardown order is explicit and passes the tested cases: resolve/cancel tasks, destroy task records, destroy unlocked/unreferenced Channels, then destroy the idle runtime. No runtime source change was required. This is not a thread-safety or worker-pool readiness claim. Process-owned native/plugin/logger/locale/network services still require a separate cross-worker ownership audit.


## 21. Archive 291 — worker-pool ownership and synchronization audit

The archive-290 source was inspected directly. The worker-pool design and its full ownership matrix are recorded in [`06_WORKER_POOL_SPEC.md`](06_WORKER_POOL_SPEC.md). This section records the most important source-level findings.

### Runtime/task state is not protected as one protocol

`FlatRuntime` contains an `atomic_bool lock`, task and Channel linked lists, counters and scheduler cursor. The lock is used in selected paths (for example Channel registry acquisition and active-run accounting), but task start/wait/cancel/state/result/destroy, scheduler selection, task state transitions, and waiter scans do not consistently acquire it. `runtime_wake_waiters()` and `runtime_wake_channel_waiters()` mutate task records while traversing the registry. This is acceptable only under the documented single scheduler-driving host thread; it is not a worker-pool synchronization protocol.

`flat_runtime_schedule()` currently chooses a RUNNABLE task, marks it RUNNING, calls `flat_vm_step()`, reconciles the result, performs Channel cleanup and wakes dependants synchronously on one host thread. The worker-pool implementation must split this into a short locked claim, unlocked VM quantum, and short locked result publication. Separately, `include/flat_runtime.h` documents concurrent `flat_runtime_run()` calls on one live runtime; that API statement is not a blanket certification of all native handlers, as the unsynchronized HTTP multipart sequence demonstrates.

### VM/task ownership is a useful starting point, not a concurrency guarantee

Each `FlatTaskRecord` owns a persistent `FlatVm`. `FlatVm` owns a copied bytecode image, a host configuration snapshot, and a persistent VM execution state. The execution state contains the task's VM pools, VCX, executor, file/IO state, RNG/date/env/path contexts and continuation. Spawn arguments are deep-copied into the child VM; file handles are rejected. This gives a strong per-task ownership boundary, provided one worker exclusively owns a RUNNING task and no host thread concurrently cancels/destroys it.

### Channel gates and logical transactions are distinct

`FlatChannel` currently has an `atomic_flag gate` protecting its internal data. This is not a pthread mutex and does not protect the runtime Channel registry or the task wait graph. The transaction owner is a logical `FlatTaskId`; no OS mutex is held across VM instructions. Worker mode should retain that distinction while adding real short-lived mutex protection and a single lock-order rule.

### Process-owned services

- Native plugin registry: the source has separate runtime and registry spinlocks. Lazy preparation and handler publication/lookup are synchronized, and handlers are invoked after those locks are released. This protects registry lifecycle, not plugin handler internals or arbitrary static state inside native modules.
- Environment staging: process staging is protected by an atomic lock and cloned into VM/runtime snapshots. External mutation of the host process environment remains outside that lock.
- Legacy path separator: atomic process-wide setting exists; worker VM operations should use their per-VM `FlatPathContext` snapshot.
- Numeric locale: shared C numeric locale initialization is synchronized and the POSIX helper selects it per thread with `uselocale()`; preserve this model and avoid process-global locale mutation.
- Runtime logger: global sequence, configuration, buffer and stdout writes are serialized by a process-wide atomic lock. Configure/shutdown only outside active worker lifetime; stdout is a shared sink.
- Fallback memory streams: the process lookup list is locked, but individual `FILE *` stream operations and caller-owned output pointers are not made generally shareable.
- WASM bridge: `flat_wasm.c` still contains shared last-result, panic, run-limit and staging globals; this bridge remains outside the first native pthread worker target.
- HTTP module: `http_ensure_boundary()` increments a function-static `unsigned long seq` without synchronization. This is a concrete race candidate under concurrent calls and must be fixed or serialized.

### Proposed synchronization baseline

The spec proposes a runtime scheduler mutex for task registry/state, queues, wait graph, IDs, pool lifecycle and Channel registry references; a per-Channel mutex for Channel storage and logical transaction metadata; and the only permitted nested order scheduler mutex → Channel mutex. No such lock may be held across VM execution, native/plugin handlers, blocking IO or host callbacks. Transient Channel references are required if a Channel pointer survives release of the registry lock.

A dispatch token (`worker_owned` plus `wake_pending`, or equivalent) is required because a task can become logically WAITING inside a native callback before its current `flat_vm_step()` returns; another worker could resolve that dependency during the same interval. The implementation must avoid both lost wakeups and duplicate ready-queue entries.

Cancellation must become a request handled at a safe VM boundary; a host thread must not call `flat_vm_cancel()` or destroy a VM that is still RUNNING on a worker. The exact public cancellation and shutdown API remains an open decision.

**Conclusion:** the source has useful per-VM ownership and several process-wide atomic/spin locks, but no complete worker-safe task scheduler protocol. Archive 291 is specification-only and makes no pthread/thread-safety claim.


## 21. Native module thread-safety audit (archive 292)

Archive 292 adds `plans/07_NATIVE_THREAD_SAFETY_AUDIT.md`, which inventories embedded native handlers, built-in optional plugin entry points, process-owned helper state, and external-resource boundaries.

### Source findings

- Embedded native handlers are predominantly context-based: collection/string/object/JSON/math/encoding/hash state is passed through `FlatNativeCtx`, VM pools, VM-owned RNG state, or per-request/per-VM runtime structures. This is a useful isolation boundary, but not by itself a worker-mode certification.
- `mod_task.c` and `mod_channel.c` call into the shared `FlatRuntime` task/Channel bridge. They remain blocked until the scheduler mutex, task registry, wait graph, Channel registry and cancellation protocol are implemented as one synchronized design.
- `mod_http.c` had a function-static non-atomic multipart sequence. Archive 292 moves allocation to `src/common/flat_http_boundary.c` and uses `atomic_uint_fast64_t` with relaxed fetch-add. The boundary string remains owned by the individual request slot.
- `mod_math_mdim.c` has a process-wide debug cache protected by atomic initialization. `flat_http_curl.c` protects libcurl global initialization with atomic state/lock. `flat_env.c`, `flat_path.c`, `flat_locale.c`, `flat_runtime_log.c`, and the fallback `flat_memstream.c` have explicit synchronization for their respective shared state, with the ownership limitations recorded in the new audit.
- `flat_rng.c` stores mutable RNG state in the `FlatRngState` passed by the VM; no process-global RNG state was found.
- The WASM bridge still contains mutable singleton globals and remains outside the worker-pool target.
- Process environment mutation, plugin path/configuration changes, logger reconfiguration, native registration/unload, shared FILE/callback targets and external library internals remain host/dependency boundaries.

### Changes

- Added `include/flat_http_boundary.h` and `src/common/flat_http_boundary.c`.
- Updated `src/lib/mod_http.c` to use the atomic sequence allocator.
- Added `test/cases/flvm/031_http_boundary_concurrency/` with eight pthread callers and 40,000 uniqueness/completeness checks.
- Added `plans/07_NATIVE_THREAD_SAFETY_AUDIT.md` with module-by-module inventory, process-global state table, plugin capability policy and acceptance status.
- Worker-mode plugin policy is specified as `THREAD_SAFE`, `SERIALIZED`, or `UNSUPPORTED`; unknown third-party plugins default to `UNSUPPORTED` until explicit metadata exists.

### Limitations

This is a source-level inventory and a targeted atomic primitive test. It does not execute two complete VMs concurrently and does not prove third-party plugins or external dependencies thread-safe. Scheduler/task/Channel runtime synchronization remains the primary blocker.


## 22. External dependency and HTTP transport preflight (archive 293)

- PCRE2 bundled version is 10.49, 8-bit, without JIT. FlatL's regex handlers allocate and free compiled patterns and match data within each call; no cross-VM regex object is shared. This is a strong candidate, pending complete-VM concurrent tests.
- Lexbor bundled version is 3.0.0. Each VM owns a `FlatDomRuntime`; document/parser objects are not shared across VMs. The amalgamation has process-global allocator function pointers configurable through `lexbor_memory_setup()`. FlatL does not call this setup API; host/plugin code must not change it while workers run. Upstream docs reviewed do not provide an explicit general thread-safety guarantee, so DOM remains not certified.
- Native libcurl uses a distinct easy handle and request-local callback state per request. `CURLOPT_NOSIGNAL=1L` is now set on each handle. `flat_http_prepare()` exposes the once-only global init so worker startup can initialize libcurl before creating worker threads. Resolver behavior and libcurl thread-safety feature availability remain deployment checks.
- WASM's `flat_http_prepare()` is a no-op and does not imply WASM worker support.

No runtime scheduler/task/Channel synchronization is added by archive 293.


## 23. Lexbor/DOM worker-mode status (archive 294)

`FlatDomRuntime` is per-VM. `flat_dom_runtime_create()` currently allocates the FlatL wrapper; Lexbor documents are created later by DOM operations. `flat_dom_runtime_destroy()` iterates the owned documents and calls `lxb_html_document_destroy()`, so Lexbor is touched during VM teardown as well as inside `dom.*` native handlers.

DOM is `PENDING_THREAD_SAFETY_VALIDATION`, not `UNSUPPORTED`. Independent-context concurrency must be tested on supported bundled/system Lexbor variants under ThreadSanitizer and AddressSanitizer/UndefinedBehaviorSanitizer. If this cannot be certified, a process-wide `SERIALIZED` gate must cover both handler calls and the teardown path. A gate around handlers only would leave teardown unprotected. No Lexbor global allocator setup may occur after workers start. Archive 294 changes the design decision and acceptance plan only; no worker gate is implemented.
