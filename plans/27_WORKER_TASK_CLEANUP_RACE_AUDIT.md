# Worker Task Cleanup Ownership Race — archive 315

## Finding

`flat_runtime_task_destroy()` marks a task `cleanup_owned` under `scheduler_mutex`, then releases the mutex before calling `flat_vm_destroy()`. This is required because VM teardown can enter serialized native-module gates, including the DOM/Lexbor gate. The task record remains registered until teardown completes.

`flat_runtime_task_cancel_request()` previously did not inspect `cleanup_owned`. A concurrent request against a CREATED task being destroyed could therefore set `cancel_requested` and return success even though destruction already owned cleanup and would free the task record without running scheduler cancellation.

## Change

- Terminal tasks still return `1`, including while their terminal VM is being destroyed.
- A nonterminal task with `cleanup_owned` and no pending cancellation request returns `-1`; destruction or synchronous cancellation already owns cleanup.
- If `cancel_requested` is already set, repeated request-only cancellation remains idempotent and returns `0`, including while the scheduler is performing its cancellation cleanup.
- No VM teardown was moved under `scheduler_mutex`.

## Regression

`flvm[46]` now adds a worker-mode interleaving:

1. Start a worker with a DOM task and hold the serialized DOM gate so the VM remains RUNNING inside the native call.
2. Create a second task but leave it CREATED.
3. Start a host thread that destroys the CREATED task; its VM teardown blocks on the same DOM gate after publishing `cleanup_owned`.
4. Issue `flat_runtime_task_cancel_request()` for the task being destroyed; it must return `-1` rather than falsely accepting cancellation.
5. Release the DOM gate, allow the running task to finish, request DRAIN, join workers, and verify both task lifecycles and runtime teardown.

## Validation on the available Linux host

- `make test-worker-dom TEST_BIN=/tmp/worker_dom_test` — PASS.
- `bin/flc` compile of the DOM fixture and direct harness execution — PASS.
- 10 consecutive direct harness executions of the final source — PASS.
- `bin/run_tests 'flvm[46]' -v -e` — PASS.
- `make all -j2` — PASS.
- `make docs` — PASS (50 sections).
- A combined `flvm[38-46]` + `port[2-5]` invocation exceeded the command time limit after `flvm[40]`; no complete grouped result is claimed.
- User's Termux validation applies to archive 314, not this archive 315 change. Termux rerun remains required.

## Limits / next audit

This closes one cleanup-ownership inconsistency and adds evidence for one worker-mode DOM interleaving. It does not make concurrent `flat_runtime_destroy()` safe against new API entry; the host must retain exclusive lifetime control. Continue with native plugin preflight/load races, deterministic partial worker-start failure injection, and platform validation. Do not infer general worker-mode thread safety from this test.
