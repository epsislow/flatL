#ifndef FLAT_CLOCK_RUNTIME_H
#define FLAT_CLOCK_RUNTIME_H

#include <stdint.h>

#define FLAT_CLOCK_MAX_STOPWATCHES 64u
#define FLAT_CLOCK_MAX_LAPS 256u

typedef struct {
    int live;
    int running;
    double accumulated_ms;
    double started_ms;
    double last_lap_ms;
    uint32_t lap_count;
    double laps[FLAT_CLOCK_MAX_LAPS]; /* cumulative elapsed time at each lap */
} FlatClockStopwatch;

typedef struct FlatClockRuntime {
    FlatClockStopwatch sw[FLAT_CLOCK_MAX_STOPWATCHES + 1u];
} FlatClockRuntime;

int flat_clock_stopwatch_live(const FlatClockRuntime *rt, uint32_t id);

#endif
