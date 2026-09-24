#include "gps_fix_builder.h"

namespace cownect::sensors {

void GpsFixBuilder::reset()
{
    pending_ = false;
    have_last_emitted_ = false;
    acc_ = {};
    rmc_seen_ = false;
    gga_seen_ = false;
}

void GpsFixBuilder::start_epoch(const GpsFixUpdate& u, uint32_t now_offset_ms)
{
    pending_ = true;
    pending_key_ = u.time_key;
    first_offset_ms_ = now_offset_ms;
    acc_ = {};
    acc_.has_time = true;
    acc_.time_key = u.time_key;
    acc_.hour = u.hour;
    acc_.minute = u.minute;
    acc_.second = u.second;
    acc_.millisecond = u.millisecond;
    rmc_seen_ = false;
    gga_seen_ = false;
    merge(u);
}

void GpsFixBuilder::merge(const GpsFixUpdate& u)
{
    if (u.kind == GpsFixUpdate::Kind::RMC) rmc_seen_ = true;
    if (u.kind == GpsFixUpdate::Kind::GGA) gga_seen_ = true;
    if (u.has_position) {
        acc_.has_position = true;
        acc_.latitude_e7 = u.latitude_e7;
        acc_.longitude_e7 = u.longitude_e7;
    }
    if (u.has_rmc_status) {
        acc_.has_rmc_status = true;
        acc_.rmc_status_active = u.rmc_status_active;
    }
    if (u.has_speed) {
        acc_.has_speed = true;
        acc_.speed_mps = u.speed_mps;
    }
    if (u.has_course) {
        acc_.has_course = true;
        acc_.course_deg = u.course_deg;
    }
    if (u.has_date) {
        acc_.has_date = true;
        acc_.day = u.day;
        acc_.month = u.month;
        acc_.year = u.year;
    }
    if (u.has_gga_quality) {
        acc_.has_gga_quality = true;
        acc_.gga_quality = u.gga_quality;
    }
    if (u.has_satellites) {
        acc_.has_satellites = true;
        acc_.satellites = u.satellites;
    }
}

GpsFix GpsFixBuilder::build() const
{
    GpsFix f = {};
    f.capture_offset_ms = first_offset_ms_;
    // Valid only when every observed validity indicator of the epoch says valid.
    bool valid = acc_.has_position && (rmc_seen_ || gga_seen_);
    if (rmc_seen_) valid = valid && acc_.has_rmc_status && acc_.rmc_status_active;
    if (gga_seen_) valid = valid && acc_.has_gga_quality && acc_.gga_quality > 0;
    f.valid = valid;
    if (valid) {
        f.latitude_e7 = acc_.latitude_e7;
        f.longitude_e7 = acc_.longitude_e7;
        f.speed_mps = acc_.has_speed ? acc_.speed_mps : 0.0f;
        f.course_deg = acc_.has_course ? acc_.course_deg : 0.0f;
    }
    f.fix_quality = acc_.has_gga_quality ? acc_.gga_quality : 0;
    f.satellites = acc_.has_satellites ? acc_.satellites : 0;
    f.utc_valid = acc_.has_time;
    f.utc_hour = acc_.hour;
    f.utc_minute = acc_.minute;
    f.utc_second = acc_.second;
    f.utc_millisecond = acc_.millisecond;
    f.date_valid = acc_.has_date;
    f.utc_day = acc_.day;
    f.utc_month = acc_.month;
    f.utc_year = acc_.year;
    return f;
}

GpsFixBuilder::FeedResult GpsFixBuilder::feed(const GpsFixUpdate& u, uint32_t now_offset_ms, GpsFix& emitted,
                                              bool& emitted_is_duplicate)
{
    emitted_is_duplicate = false;
    if (!u.has_time) {
        return FeedResult::UNATTRIBUTED;
    }
    if (pending_ && u.time_key == pending_key_) {
        merge(u);
        return FeedResult::MERGED;
    }
    bool did_emit = false;
    if (pending_) {
        did_emit = flush(emitted, emitted_is_duplicate);
    }
    start_epoch(u, now_offset_ms);
    return did_emit ? FeedResult::EMITTED : FeedResult::MERGED;
}

bool GpsFixBuilder::flush(GpsFix& emitted, bool& emitted_is_duplicate)
{
    if (!pending_) {
        return false;
    }
    pending_ = false;
    emitted = build();
    emitted_is_duplicate = have_last_emitted_ && last_emitted_key_ == pending_key_;
    have_last_emitted_ = true;
    last_emitted_key_ = pending_key_;
    return true;
}

}  // namespace cownect::sensors
