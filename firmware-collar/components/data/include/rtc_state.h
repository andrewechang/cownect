#pragma once

#include <cstdint>

namespace cownect::data {

// Deep-sleep-retained scheduler state (Material 10 section 28). RTC slow memory only:
// survives timer deep sleep, NOT full power loss. Not a permanent identity scheme.
struct RtcSchedulerState {
    uint32_t magic;
    uint32_t version;
    uint32_t cycle_sequence;
    uint32_t next_capture_id;
    uint32_t last_sleep_requested_ms;
    uint32_t last_cycle_overrun_ms;
    uint32_t last_capture_id;
};

// Validates magic/version after boot; re-initializes (next_capture_id = 1) when invalid.
// Returns true if retained contents were valid.
bool rtc_state_init_on_boot();
// Result of the rtc_state_init_on_boot() call made during this boot.
bool rtc_state_valid_at_boot();
RtcSchedulerState& rtc_state();

// Increments once per new capture.
uint32_t capture_id_allocate();
uint32_t cycle_sequence_next();

}  // namespace cownect::data
