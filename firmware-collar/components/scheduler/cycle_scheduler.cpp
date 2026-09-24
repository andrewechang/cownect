#include "cycle_scheduler.h"

#include "board_power.h"
#include "capture_session.h"
#include "cownect_config.h"
#include "cownect_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "feature_processor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "power_manager.h"
#include "rtc_state.h"
#include "sensor_manager.h"
#include "sleep_manager.h"
#include "sx1262_adapter.h"

namespace cownect::scheduler {
namespace {
constexpr const char* TAG = "CYCLE";
constexpr uint32_t kAwakePollMs = 100;

uint32_t ms(int64_t us)
{
    return us > 0 ? static_cast<uint32_t>(us / 1000) : 0;
}

// Powers the switched rail down and tells rail users their state is lost.
void release_switched_rail(void*)
{
    sensors::sensor_manager().ensure_stopped();
    if (radio::lora_radio().tx_in_progress() || radio::lora_radio().rx_in_progress()) {
        return;  // never remove power while the radio is active
    }
    power::power_manager().peripherals_off();
    sensors::sensor_manager().notify_peripheral_power_lost();
    radio::lora_radio().mark_power_lost();
}
}  // namespace

const char* cycle_result_name(CycleResult r)
{
    switch (r) {
    case CycleResult::COMPLETE: return "COMPLETE";
    case CycleResult::DEGRADED: return "DEGRADED";
    case CycleResult::FAILED:   return "FAILED";
    case CycleResult::OVERRUN:  return "OVERRUN";
    }
    return "?";
}

SchedulerConfig scheduler_production_config()
{
    SchedulerConfig c = {};
    c.cycle_target_ms = config::CYCLE_TARGET_MS;
    c.capture_ms = config::CAPTURE_TIME_MS;
    c.allow_deep_sleep = true;
    c.wait_until_boundary = true;
    c.communication_enabled = comm::communication_mode_from_config(c.communication_mode);
    c.artificial_post_capture_delay_ms = 0;
    return c;
}

CycleScheduler& cycle_scheduler()
{
    static CycleScheduler instance;
    return instance;
}

esp_err_t CycleScheduler::init(const SchedulerConfig& config)
{
    if (config.capture_ms == 0 || config.cycle_target_ms == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    cfg_ = config;
    state_ = CycleState::BOOT;
    sensors::sensor_manager().init();
    return ESP_OK;
}

esp_err_t CycleScheduler::run_one_cycle()
{
    metrics_ = {};
    telemetry_ = {};
    state_ = CycleState::PREPARE_CYCLE;
    const int64_t cycle_start = esp_timer_get_time();
    const int64_t target_us = static_cast<int64_t>(cfg_.cycle_target_ms) * 1000;
    metrics_.cycle_sequence = data::cycle_sequence_next();
    metrics_.mode_at_cycle_start = board_get_operation_mode();
    ESP_LOGI(TAG, "seq=%u mode=%s cycle=%ums capture=%ums", static_cast<unsigned>(metrics_.cycle_sequence),
             operation_mode_name(metrics_.mode_at_cycle_start), static_cast<unsigned>(cfg_.cycle_target_ms),
             static_cast<unsigned>(cfg_.capture_ms));

    data::CaptureSession& session = data::capture_session();

    // Power + capture (SensorManager enables the rail through the board API and waits the
    // centralized stabilization delay).
    state_ = CycleState::POWERING_PERIPHERALS;
    esp_err_t err = power::power_manager().peripherals_on();
    state_ = CycleState::CAPTURING;
    const int64_t t_cap = esp_timer_get_time();
    if (err == ESP_OK) {
        err = sensors::sensor_manager().run_capture(session, cfg_.capture_ms);
    }
    metrics_.capture_time_ms = ms(esp_timer_get_time() - t_cap);
    metrics_.capture_complete = err == ESP_OK && session.state == data::CaptureState::COMPLETE;
    session.cycle_start_us = static_cast<uint64_t>(cycle_start);
    metrics_.capture_id = session.id.capture_id;
    if (!metrics_.capture_complete) {
        state_ = CycleState::ERROR_RECOVERY;
        ESP_LOGW(TAG, "capture not completed (%s)", cownect_err_name(err));
    }

    // Post-capture processing (Material 11).
    state_ = CycleState::POST_CAPTURE;
    const int64_t t_proc = esp_timer_get_time();
    if (metrics_.capture_complete) {
        features::SimpleFeatureProcessor().process(session, telemetry_);
    }
    if (cfg_.artificial_post_capture_delay_ms > 0) {
        ESP_LOGW(TAG, "DEVELOPMENT fault injection: +%ums post-capture delay",
                 static_cast<unsigned>(cfg_.artificial_post_capture_delay_ms));
        vTaskDelay(pdMS_TO_TICKS(cfg_.artificial_post_capture_delay_ms));
    }
    metrics_.processing_time_ms = ms(esp_timer_get_time() - t_proc);

    // Communication (switched rail stays ON while LoRa may use it).
    state_ = CycleState::COMMUNICATION;
    const int64_t t_comm = esp_timer_get_time();
    if (cfg_.communication_enabled && metrics_.capture_complete) {
        comm::CommunicationManager& cm = comm::communication_manager();
        cm.init(cfg_.communication_mode);
        cm.submit_capture(session, telemetry_, metrics_.comm, &release_switched_rail, nullptr);
        metrics_.communication_implemented = true;
        metrics_.communication_complete = metrics_.comm.success;
    } else {
        metrics_.communication_implemented = false;  // SKIPPED_NOT_IMPLEMENTED / CONFIG_NOT_SET
        metrics_.communication_complete = false;
    }
    metrics_.communication_time_ms = ms(esp_timer_get_time() - t_comm);

    // Cleanup guard: runs after partial failures too.
    state_ = CycleState::POWERING_DOWN;
    const int64_t t_pd = esp_timer_get_time();
    release_switched_rail(nullptr);
    metrics_.power_down_time_ms = ms(esp_timer_get_time() - t_pd);
    metrics_.peripherals_commanded_on_before_idle = board_peripherals_commanded_enabled();

    // Dynamic remaining time: no fixed 90 s assumption, no signed underflow.
    const int64_t awake_us = esp_timer_get_time() - cycle_start;
    metrics_.awake_elapsed_ms = ms(awake_us);
    uint64_t remaining_us = 0;
    if (awake_us < target_us) {
        remaining_us = static_cast<uint64_t>(target_us - awake_us);
    } else {
        metrics_.cycle_overrun_ms = ms(awake_us - target_us);
        telemetry_.status_flags |= features::TELEMETRY_STATUS_CYCLE_OVERRUN;  // local record only
    }
    data::RtcSchedulerState& rtc = data::rtc_state();
    rtc.last_cycle_overrun_ms = metrics_.cycle_overrun_ms;

    const bool degraded = !metrics_.capture_complete || (metrics_.communication_implemented &&
                                                         !metrics_.communication_complete) ||
                          session.status.accel_degraded || session.status.microphone_degraded ||
                          session.status.gps_degraded || !session.status.accel_ok || !session.status.microphone_ok;
    metrics_.result = metrics_.cycle_overrun_ms > 0   ? CycleResult::OVERRUN
                      : !metrics_.capture_complete    ? CycleResult::FAILED
                      : degraded                      ? CycleResult::DEGRADED
                                                      : CycleResult::COMPLETE;

    metrics_.mode_before_idle = board_get_operation_mode();
    if (remaining_us == 0) {
        rtc.last_sleep_requested_ms = 0;
        log_summary();
        ESP_LOGI(TAG, "no sleep; next cycle after cleanup (no compensation)");
        return ESP_OK;
    }
    if (metrics_.mode_before_idle == OperationMode::NORMAL && cfg_.allow_deep_sleep) {
        state_ = CycleState::PREPARING_SLEEP;
        // Re-check after PERIPH_EN low (Material 10 section 22 step 5).
        const OperationMode recheck = board_get_operation_mode();
        if (recheck == OperationMode::NORMAL) {
            const int64_t now_awake = esp_timer_get_time() - cycle_start;
            if (now_awake < target_us) {
                enter_sleep(static_cast<uint64_t>(target_us - now_awake));  // no return on success
            }
            log_summary();
            return ESP_OK;
        }
        metrics_.mode_before_idle = recheck;  // UNSTABLE/PROGRAMMING: never sleep blindly
    }
    log_summary();
    if (cfg_.wait_until_boundary) {
        state_ = CycleState::WAITING_AWAKE;
        wait_awake_until_boundary(cycle_start, target_us);
    }
    return ESP_OK;
}

void CycleScheduler::enter_sleep(uint64_t remaining_us)
{
    metrics_.sleep_requested_ms = static_cast<uint32_t>(remaining_us / 1000);
    metrics_.entered_deep_sleep = true;
    data::rtc_state().last_sleep_requested_ms = metrics_.sleep_requested_ms;
    log_summary();
    ESP_LOGI(TAG, "sleep requested_ms=%u", static_cast<unsigned>(metrics_.sleep_requested_ms));
    power::SleepManager& sm = power::sleep_manager();
    if (sm.configure_timer_wakeup(remaining_us) == ESP_OK) {
        sm.enter_deep_sleep();
    }
    metrics_.entered_deep_sleep = false;
    ESP_LOGE(TAG, "timer wake configuration failed; staying awake");
}

void CycleScheduler::wait_awake_until_boundary(int64_t cycle_start_us, int64_t target_us)
{
    // Bounded, yielding wait; mode re-checked so UNSTABLE -> NORMAL can still sleep the remainder.
    while (true) {
        const int64_t awake = esp_timer_get_time() - cycle_start_us;
        if (awake >= target_us) {
            return;
        }
        const OperationMode mode = board_get_operation_mode();
        if (mode == OperationMode::NORMAL && cfg_.allow_deep_sleep &&
            metrics_.mode_before_idle == OperationMode::UNSTABLE) {
            enter_sleep(static_cast<uint64_t>(target_us - awake));
        }
        if (mode == OperationMode::UNSTABLE) {
            metrics_.mode_before_idle = OperationMode::UNSTABLE;
        }
        vTaskDelay(pdMS_TO_TICKS(kAwakePollMs));
    }
}

void CycleScheduler::log_summary() const
{
    const data::CaptureSession& s = data::capture_session();
    ESP_LOGI(TAG, "seq=%u capture_id=%u mode_start=%s mode_idle=%s", static_cast<unsigned>(metrics_.cycle_sequence),
             static_cast<unsigned>(metrics_.capture_id), operation_mode_name(metrics_.mode_at_cycle_start),
             operation_mode_name(metrics_.mode_before_idle));
    ESP_LOGI(TAG, "capture_ms=%u result=%s processing_ms=%u", static_cast<unsigned>(metrics_.capture_time_ms),
             s.state == data::CaptureState::COMPLETE ? "COMPLETE" : "INCOMPLETE",
             static_cast<unsigned>(metrics_.processing_time_ms));
    if (metrics_.communication_implemented) {
        ESP_LOGI(TAG, "comm=%s success=%d comm_ms=%u", comm::communication_mode_name(metrics_.comm.mode),
                 metrics_.comm.success, static_cast<unsigned>(metrics_.communication_time_ms));
    } else {
        ESP_LOGI(TAG, "comm=NOT_IMPLEMENTED/CONFIG_NOT_SET comm_ms=%u", static_cast<unsigned>(metrics_.communication_time_ms));
    }
    ESP_LOGI(TAG, "periph_cmd=%s awake_ms=%u overrun_ms=%u cycle_result=%s",
             metrics_.peripherals_commanded_on_before_idle ? "ON" : "OFF", static_cast<unsigned>(metrics_.awake_elapsed_ms),
             static_cast<unsigned>(metrics_.cycle_overrun_ms), cycle_result_name(metrics_.result));
}

}  // namespace cownect::scheduler
