# Worker Channel / Shutdown Stress — archive 321

## Scope

This checkpoint advances the still-open v2.0 stress item in `02_ROADMAP.md`. It combines real worker execution, independent Channel transactions, concurrent request-only cancellation, DRAIN completion, worker join, and post-join CANCEL cleanup.

This is focused evidence for the tested interleavings, not a general thread-safety certification. No production scheduler or Channel synchronization change was made.

## Test shape

New test: `flvm[48]`, `test/cases/flvm/048_worker_channel_shutdown_stress`.

### Multi-writer / multi-Channel DRAIN

- Create four runtime-owned Channels and initialize one numeric `counter` in each.
- Create eight writer tasks per Channel (32 tasks total), each using a transaction (`channel.lock`, read, increment, write, `channel.unlock`).
- Run with four real workers and request DRAIN.
- Verify every Channel counter is exactly eight, all tasks are COMPLETED, workers join, and task/Channel/runtime cleanup succeeds.

This verifies independent Channel transaction progress and serialized read-modify-write under contention without relying on timing-only assertions.

### Concurrent cancellation / DRAIN-to-CANCEL

- Create four Channels, each held by a distinct CREATED owner task.
- Start one waiter task per Channel and cooperatively advance it until it is WAITING on the transaction owner.
- Start four workers and request DRAIN; verify DRAIN reaches BLOCKED.
- Four host threads synchronize on a barrier and concurrently publish cancellation requests for the four waiters.
- DRAIN then reaches STOPPED after the started waiters become CANCELLED. The four CREATED owners remain CREATED because they are outside the DRAIN set.
- Join the exited worker pool, escalate to CANCEL, and drive the remaining CREATED-task cleanup cooperatively.
- Verify owners and waiters are CANCELLED, then destroy all tasks, Channels and the runtime.

The ordering is intentional: workers exit when DRAIN reaches STOPPED. CREATED tasks are not implicitly part of DRAIN, so if the host later escalates to CANCEL after DRAIN completion, it must first join the stopped pool and then drive cancellation through the cooperative scheduler. This matches the existing shutdown contract and the cooperative regression in `flvm[41]`.

## Validation (archive 321, local POSIX host)

- `make all -j2`: PASS.
- `make docs`: PASS (50 sections).
- `flvm[48]`: PASS.
- Dedicated harness: five consecutive direct executions PASS.
- Dedicated harness under AddressSanitizer (`detect_leaks=0`): PASS.
- Dedicated harness under UndefinedBehaviorSanitizer (`halt_on_error=1`): PASS.
- Dedicated harness under ThreadSanitizer (`halt_on_error=1`): PASS.
- `flvm[39]`, `flvm[41]`, `flvm[42]`, `flvm[44]`, `flvm[45]`, `flvm[46]`, `flvm[47]`, and `flvm[48]`: PASS in completed individual or grouped runs. The grouped selection `flvm[39] flvm[41-48]` exceeded the command limit after reporting PASS for `[39]`, `[41]`, `[42]`, and `[43]`; `[44]`–`[48]` were subsequently run individually and passed. The grouped invocation is not counted as a complete run.
- Full 1187-test suite was not executed. Windows worker execution and actual Emscripten linking remain unverified.

## Findings

No production fix was justified. The test confirms the documented distinction between DRAIN's started-work set and CREATED tasks. A worker pool that has reached STOPPED must be joined before the host switches back to cooperative scheduling for post-DRAIN CANCEL cleanup.

The v2.0 broader stress item remains open: randomized wait-graph topologies, longer mixed Channel/HTTP/DOM stress, and platform validation are still outstanding.
