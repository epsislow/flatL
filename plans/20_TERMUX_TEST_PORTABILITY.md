# Termux test portability fixes — archive 308

## Context

On Termux/Android, `make test` reported four failures:

- `flvm[38]` — runtime scheduler mutex / task registry / single-driver / unlocked teardown.
- `flvm[40]` — request-only cancellation across CREATED, RUNNABLE, WAITING and RUNNING.
- `types[72]` — SCHM trailing-garbage rejection.
- `types[108]` — AM-NUM comparison and conversion regression test.

The supplied Termux output showed linker errors for `flat_strtod` and `flat_snprintf` in `types[108]`. The exact detailed failure output for `flvm[38]` and `[40]` was not included.

## Changes

### `types[108]`: complete test link inputs

The test compiled `flat_number.c` alone even though that implementation calls `flat_strtod()` and `flat_snprintf()`, both implemented in `src/common/flat_locale.c`. The command now compiles both sources and defines `_GNU_SOURCE` so the POSIX locale APIs are declared under strict C11 mode.

### `types[72]`: corrupt the intended section

The previous test appended `0xff` at EOF. In the current format that byte belongs to the trailing BPAR section, so the loader correctly reported `invalid BPAR section`; it did not exercise SCHM trailing-data validation.

A small C helper now inserts one byte exactly at the SCHM/JBMX boundary and increments the little-endian `jbmx_off` header field (bytes 32–35). This makes the SCHM section one byte longer while leaving the following sections correctly aligned. The loader must reject it with `invalid SCHM section`.

### `flvm[38]`: deterministic RUNNING observation

The original test depended on a polling thread observing a very short VM instruction while the scheduler was executing. That observation is timing-sensitive on mobile/single-core systems.

The regression now uses task 0's DOM native call as a controlled hold point. The main test thread holds the serialized DOM gate, task 0 enters RUNNING and blocks at that gate, and the query thread performs the cancellation/destroy rejection and concurrent-scheduler exclusion checks while RUNNING is stable. The main thread then releases the gate and allows all tasks to finish. The DOM operation is the first instruction in the fixture so the hold point is reached without depending on a long instruction sequence.

### `flvm[40]`: mobile-friendly wait deadline

The RUNNING-state wait around the intentionally blocked DOM native call was increased from 5 seconds to 30 seconds. The test still requires the same state transition and cancellation semantics; only the deadline was relaxed to tolerate slower or heavily loaded Android devices.

## Validation in the archive build environment

- `bin/run_tests 'flvm[38]' -v -e` — PASS.
- `bin/run_tests 'flvm[40]' -v -e` — PASS.
- `bin/run_tests 'types[72]' -v -e` — PASS.
- `bin/run_tests 'types[108]' -v -e` — PASS.
- `flvm[38]` dedicated test binary, five consecutive executions — PASS.
- `flvm[40]` dedicated test binary, five consecutive executions — PASS.
- `make all -j2` — PASS.
- `make docs` — PASS (50 sections).

The full 1132-case Termux suite was not rerun here, and this environment is not Termux/Android. The two FLVM tests passed on the available POSIX host, but the exact Termux-only failure mode remains unconfirmed until the user reruns the focused tests with verbose diagnostics. No runtime implementation semantics were changed in this archive.

## Next

After confirming these four cases on Termux, resume the existing worker-mode audit roadmap: HTTP/DOM lifecycle under multiple workers, native plugin preparation/load races, and deterministic partial worker-start failure injection. Do not treat this portability patch as worker-mode thread-safety certification.
