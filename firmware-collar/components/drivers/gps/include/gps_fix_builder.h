#pragma once

#include "gps_types.h"
#include "nmea_parser.h"

namespace cownect::sensors {

// Combines the sentences of one navigation epoch (same UTC time field) into one GpsFix
// (Material 7 sections 14, 21). An epoch is emitted once, when a sentence with a different time
// arrives or on flush(). Updates without a time field cannot be attributed to an epoch and are
// reported as unattributed (the caller counts them as invalid).
class GpsFixBuilder {
public:
    enum class FeedResult { MERGED, EMITTED, UNATTRIBUTED };

    void reset();

    // `now_offset_ms` is the capture-relative time the sentence was received.
    FeedResult feed(const GpsFixUpdate& u, uint32_t now_offset_ms, GpsFix& emitted, bool& emitted_is_duplicate);
    // Emits the pending epoch, if any.
    bool flush(GpsFix& emitted, bool& emitted_is_duplicate);

private:
    void start_epoch(const GpsFixUpdate& u, uint32_t now_offset_ms);
    void merge(const GpsFixUpdate& u);
    GpsFix build() const;

    bool pending_ = false;
    uint32_t pending_key_ = 0;
    uint32_t first_offset_ms_ = 0;
    GpsFixUpdate acc_ = {};
    bool rmc_seen_ = false;
    bool gga_seen_ = false;
    bool have_last_emitted_ = false;
    uint32_t last_emitted_key_ = 0;
};

}  // namespace cownect::sensors
