# Task runtime — initial VM audit

**Baseline:** `flatL-main(57)` archive. This is an architectural audit, not a task implementation.

## Findings

### VM state lifetime

`flat_vm_run_opts()` creates a local `VmState st` and initializes the runtime for one complete run. The interpreter loop operates on that stack-owned state and all normal termination paths call `vm_finish()` / `vm_teardown()`.

This is suitable for the current one-shot VM invocation, but it is not yet a resumable task object. A cooperative task implementation will need a persistent execution state and an explicit suspend/resume boundary. Simply invoking `flat_vm_run_opts()` again cannot resume an instruction pointer, call stack, loop frames, or native execution state.

### Executor state

`src/common/flat_executor.c` currently owns process-global state:

- `g_bind` / `g_bind_ok` — VM binding used by executor native operations;
- `g_frames` / `g_frame_sp` — executor frame stack.

`flat_executor_reset_session()` clears that state, while `flat_executor_bind_vm()` replaces the current binding. This prevents independent VM/task executor stacks from coexisting. The first task-runtime refactor should move the binding and frame stack into an explicit executor context owned by the VM/task. Panic draining must operate on that context only.

### VM call-instruction state

`src/common/vm.c` also uses process-global call-instruction bookkeeping:

- `g_vm_call_instr`;
- `g_instr_stack` and `g_instr_stack_sp`;
- `g_instr_push_call_sp`;
- `g_last_call_function_sp`.

The VCX call-instruction resolver depends on the active VM pointer. These values must become per-execution state before multiple VM tasks can be interleaved or run on separate threads.

### VCX active runtime

`src/common/flat_vcx.c` stores `g_vcx_active` and `g_call_instr_resolver` globally. This is another single-active-VM assumption. Task isolation requires the active runtime and call-instruction resolver to be explicit in the execution context, or supplied through a safe scoped binding. A process-global active VCX is not sufficient for worker threads.

### Other global runtime state

The VM run setup calls `setlocale(LC_ALL, "C")` and `flat_native_runtime_reset()`. `src/common/flat_rng.c` has process-global RNG state. These and other native modules must be audited for process-global mutable state before the parallel worker-pool milestone. The initial cooperative runtime can proceed only with a clearly defined single-thread execution model and must not claim thread safety.

### Panic handling

`vm_jump_panic_any()` currently:

1. binds the current VM into the global executor binding;
2. drains executor frames and pending call instruction contexts;
3. resolves the panic location and records the caught panic;
4. looks for `#panic.<loc>` and then `#panic.any`;
5. marks the panic unhandled if no handler exists.

The current executor drain is therefore coupled to the global executor context. When the executor becomes per-task, the drain must be scoped to the failing task. A panic in one task must not drain another task's frames or destroy its VCX contexts.

## Initial implementation decision

The first foundation change adds an opt-in central runtime logger and documents the global-state constraints. It deliberately does not introduce task scheduling, Channel operations, `.any` panic resolution changes, or worker threads. Those require the per-task execution-context refactor and dedicated semantic tests.
