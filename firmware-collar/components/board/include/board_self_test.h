#pragma once

#include <cstddef>
#include <cstdint>

// PSRAM runtime verification (Material 4 section 8).
struct PsramReport {
    bool initialized;
    size_t psram_size_bytes;
    size_t heap_total_bytes;
    size_t heap_free_bytes;
    size_t test_alloc_bytes;
    bool test_alloc_ok;
    bool pass;
};

// Attempts a PSRAM allocation of `test_bytes`, writes/verifies a pattern, frees it.
PsramReport board_psram_self_test(size_t test_bytes);
