# Task Runtime — audit tehnic inițial

**Baseline auditat:** `flatL-main(57).zip`  
**Reconfirmat în:** `flatL-main-251-task-runtime-foundation.zip`  
**Scop:** constatări arhitecturale pentru pregătirea task-urilor; nu reprezintă implementare de task runtime.

## 1. Durata de viață VM

`flat_vm_run_opts()` creează un `VmState st` local și inițializează runtime-ul pentru o execuție completă. Bucla interpreter operează pe această stare locală, iar căile normale de terminare apelează `vm_finish()` / `vm_teardown()`.

Modelul este potrivit pentru invocarea VM one-shot actuală, dar nu este încă un obiect task persistent și reluabil. Pentru scheduler cooperativ trebuie să păstrăm explicit starea necesară suspendării/reluării: instruction pointer, call stack, loop frames și starea necesară native execution. Apelarea repetată a `flat_vm_run_opts()` nu poate relua o execuție întreruptă.

## 2. Executor

`src/common/flat_executor.c` folosește stare globală:

- `g_bind` / `g_bind_ok` — binding-ul VM pentru operațiile native executor;
- `g_frames` / `g_frame_sp` — stack-ul de frame-uri executor.

`flat_executor_reset_session()` resetează această stare, iar `flat_executor_bind_vm()` înlocuiește binding-ul curent. Două contexte independente nu pot avea stack-uri executor coexistente. Refactorul trebuie să mute binding-ul și frame stack-ul într-un context explicit per VM/task. Panic drain trebuie să opereze doar pe contextul task-ului care a eșuat.

## 3. Bookkeeping VM call instruction

`src/common/vm.c` folosește bookkeeping global pentru call instructions:

- `g_vm_call_instr`;
- `g_instr_stack` și `g_instr_stack_sp`;
- `g_instr_push_call_sp`;
- `g_last_call_function_sp`.

Resolver-ul call instruction VCX depinde de pointer-ul VM activ. Aceste valori trebuie să devină per-execution înainte de interleaving sau execuție pe thread-uri diferite.

## 4. VCX active runtime

`src/common/flat_vcx.c` păstrează `g_vcx_active` și `g_call_instr_resolver` global. Este o presupunere de un singur VM activ. Task isolation cere ca runtime-ul activ și resolver-ul să fie explicite în context sau furnizate printr-un binding scoped sigur.

## 5. Alte globale

Setup-ul VM apelează `setlocale(LC_ALL, "C")` și `flat_native_runtime_reset()`. `src/common/flat_rng.c` are stare RNG globală. Native modules și subsisteme precum stdio, date/time, locale și runtime reset trebuie auditate pentru stare globală mutabilă înainte de worker pool.

Runtime-ul cooperativ inițial trebuie să aibă un model clar single-thread și nu trebuie declarat thread-safe.

## 6. Panic handling

`vm_jump_panic_any()` în forma auditată:

1. leagă VM-ul curent în binding-ul global executor;
2. golește frame-urile executorului și contexte call instruction pending;
3. rezolvă locația panic-ului și înregistrează panic-ul prins;
4. caută `#panic.<loc>`, apoi `#panic.any`;
5. marchează panic-ul nepreluat dacă nu există handler.

Drain-ul este cuplat la executorul global. Când executorul devine per-task, drain-ul trebuie scoped la task-ul care a eșuat. Un panic într-un task nu trebuie să golească frame-urile altui task sau să distrugă VCX-ul lui.

Extinderea dorită `#panic.#worker.any` este documentată în [01_DECISIONS.md](01_DECISIONS.md); implementarea trebuie să respecte mecanismul actual și să fie acoperită de teste.

## 7. Concluzie

Primul refactor structural major după F0 este contextul explicit de execuție: executor binding/frame stack, VM call-instruction bookkeeping și VCX active runtime/resolver. Schedulerul nu trebuie introdus înainte ca ownership-ul și cleanup-ul acestor stări să fie clare.
