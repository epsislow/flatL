# VM Guard Scratch Ownership — archive 314

## Finding

Archive 313's `vm_state_execute()` declared two `FLAT_MAX_VARS`-sized automatic arrays inside `OP_GUARD`:

- `uint8_t guard_has_ty[FLAT_MAX_VARS]`
- `FlatSlotType guard_slot_ty[FLAT_MAX_VARS]`

`FLAT_MAX_VARS` is 32768 and `FlatSlotType` contains the fixed-size `dynamic_expr` buffer. The resulting stack frame was too large for Android pthread stacks. AddressSanitizer on Termux confirmed `stack-overflow` at entry to `vm_state_execute()` while `flat_runtime_schedule()` executed on scheduler thread T2. This was a VM stack-footprint defect, not evidence of a scheduler mutex or teardown race.

## Change in archive 314

Guard scratch is now owned by `VmState`, alongside the existing `guard_names` and `guard_vals` buffers. `vm_state_initialize()` allocates the two additional buffers using `scratch_n` (`var_slots_max`, with a minimum allocation of one), and `vm_teardown()` releases them. `OP_GUARD` fills and uses these buffers instead of declaring large automatic arrays.

This keeps the scratch lifetime aligned with the persistent VM state, avoids per-guard allocation, and removes the large fixed stack allocation from `vm_state_execute()`.

## Validation status

- Termux ASan reproduction from archive 313: confirmed stack overflow in `vm_state_execute()`.
- Archive 314: regression validation must include `flvm[38]` through `flvm[46]` on Termux, with `flvm[38]` rerun under AddressSanitizer.
- Do not infer that all worker-pool thread-safety concerns are resolved from this stack-footprint fix.
