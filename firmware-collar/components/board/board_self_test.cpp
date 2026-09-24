#include "board_self_test.h"

#include <cstring>
#include "esp_heap_caps.h"
#include "esp_psram.h"

PsramReport board_psram_self_test(size_t test_bytes)
{
    PsramReport r = {};
    r.initialized = esp_psram_is_initialized();
    r.psram_size_bytes = r.initialized ? esp_psram_get_size() : 0;
    r.heap_total_bytes = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    r.heap_free_bytes = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    r.test_alloc_bytes = test_bytes;

    if (r.initialized && test_bytes >= 8) {
        auto* p = static_cast<uint8_t*>(heap_caps_malloc(test_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (p != nullptr) {
            bool ok = true;
            for (size_t i = 0; i < test_bytes; i += 4096) {
                p[i] = static_cast<uint8_t>(i >> 12);
            }
            p[test_bytes - 1] = 0xA5;
            for (size_t i = 0; i < test_bytes; i += 4096) {
                ok = ok && (p[i] == static_cast<uint8_t>(i >> 12));
            }
            ok = ok && (p[test_bytes - 1] == 0xA5);
            heap_caps_free(p);
            r.test_alloc_ok = ok;
        }
    }
    r.pass = r.initialized && r.test_alloc_ok;
    return r;
}
