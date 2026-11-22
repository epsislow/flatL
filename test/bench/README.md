# Performance benchmarks

Run the informational benchmark suite with:

```sh
make test-perf
# or
FLAT_PERF_ROUNDS=9 bin/run_tests --each --time --categ=perf
```

`FLAT_PERF_ROUNDS` controls the number of VM executions per scenario (default: 5). Each scenario compiles once, then reports compilation time separately from the best and average VM run time. The `run_tests` `time_ms exec=...` value is the outer shell-case duration and includes source generation, compilation, and all requested rounds; use the `perf:` line for the separated measurements.

The suite is informational and does not enforce timing thresholds. It is split into these groups:

- Existing general baselines: VM loop, string churn, collection churn, basic COW, native math variable access, nested collection access, and handle churn.
- Pool scan contrasts: same number of VM variables with numeric values versus long string values that occupy string-pool slots (256 and 1024 variables), plus 128 live lists and 128 live objects against a numeric control. These are VM-level contrasts, not direct isolated timings of `flat_pool_reap_idle` / `flat_coll_reap_idle`.
- VM variable lookup: repeatedly read the first or last variable in a 256-variable table.
- VCX variable lookup: repeatedly fetch the first or last variable in a 256-variable VCX context.
- Collection COW: list `set` across 8, 64, 256, and 1024 elements; object `set` across 8, 32, 64, and 96 properties. Larger object scenarios build their initial object with repeated `O.set` calls, so their measured runtime includes setup as well as the repeated update phase. Object scenarios are kept below the current object-pool churn limit.

The benchmark shell script checks compiler and VM exit statuses before parsing timing output. This is important: a failed compile or VM execution must fail the test rather than accidentally produce a zero-millisecond result.

These benchmarks are intended to locate promising areas for profiling. They do not by themselves prove that one internal function is responsible for a measured difference; use a profiler or a dedicated C-level microbenchmark before changing the implementation.
