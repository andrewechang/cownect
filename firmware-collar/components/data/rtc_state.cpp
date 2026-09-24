#include "rtc_state.h"

#include "esp_attr.h"

namespace cownect::data {
namespace {
constexpr uint32_t kMagic = 0x434E5254;  // "CNRT"
constexpr uint32_t kVersion = 1;
RTC_DATA_ATTR RtcSchedulerState s_rtc;
bool s_valid_at_boot = false;
}  // namespace

bool rtc_state_init_on_boot()
{
    if (s_rtc.magic == kMagic && s_rtc.version == kVersion && s_rtc.next_capture_id != 0) {
        s_valid_at_boot = true;
        return true;
    }
    s_rtc = {};
    s_rtc.magic = kMagic;
    s_rtc.version = kVersion;
    s_rtc.next_capture_id = 1;
    s_valid_at_boot = false;
    return false;
}

bool rtc_state_valid_at_boot()
{
    return s_valid_at_boot;
}

RtcSchedulerState& rtc_state()
{
    return s_rtc;
}

uint32_t capture_id_allocate()
{
    const uint32_t id = s_rtc.next_capture_id;
    s_rtc.next_capture_id = (id == UINT32_MAX) ? 1 : id + 1;
    s_rtc.last_capture_id = id;
    return id;
}

uint32_t cycle_sequence_next()
{
    return ++s_rtc.cycle_sequence;
}

}  // namespace cownect::data
