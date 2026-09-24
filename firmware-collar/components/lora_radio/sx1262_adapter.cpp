#include "sx1262_adapter.h"

#include <cstring>
#include "board_pins.h"
#include "cownect_config.h"
#include "cownect_err.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lora_config.h"
#include "sx126x.h"
#include "sx1262_hal_espidf.h"

namespace cownect::radio {
namespace {
constexpr const char* TAG = "LORA";
constexpr spi_host_device_t kSpiHost = SPI2_HOST;
constexpr size_t kDmaBytes = 272;  // 255-byte payload + command/offset bytes
constexpr uint16_t kIrqMask = SX126X_IRQ_TX_DONE | SX126X_IRQ_RX_DONE | SX126X_IRQ_TIMEOUT | SX126X_IRQ_CRC_ERROR |
                              SX126X_IRQ_HEADER_ERROR;

Sx1262HalContext s_ctx = {};

bool bw_to_enum(uint32_t hz, sx126x_lora_bw_t& out)
{
    switch (hz) {
    case 7810:   out = SX126X_LORA_BW_007; return true;
    case 10420:  out = SX126X_LORA_BW_010; return true;
    case 15630:  out = SX126X_LORA_BW_015; return true;
    case 20830:  out = SX126X_LORA_BW_020; return true;
    case 31250:  out = SX126X_LORA_BW_031; return true;
    case 41670:  out = SX126X_LORA_BW_041; return true;
    case 62500:  out = SX126X_LORA_BW_062; return true;
    case 125000: out = SX126X_LORA_BW_125; return true;
    case 250000: out = SX126X_LORA_BW_250; return true;
    case 500000: out = SX126X_LORA_BW_500; return true;
    default:     return false;
    }
}

sx126x_ramp_time_t ramp_to_enum(uint16_t us)
{
    switch (us) {
    case 10:   return SX126X_RAMP_10_US;
    case 20:   return SX126X_RAMP_20_US;
    case 40:   return SX126X_RAMP_40_US;
    case 80:   return SX126X_RAMP_80_US;
    case 200:  return SX126X_RAMP_200_US;
    case 800:  return SX126X_RAMP_800_US;
    case 1700: return SX126X_RAMP_1700_US;
    default:   return SX126X_RAMP_3400_US;
    }
}
}  // namespace

void IRAM_ATTR Sx1262Adapter::dio1_isr(void* arg)
{
    // Minimal ISR: count + notify. No SPI, logging or allocation (Material 12 section 11).
    auto* self = static_cast<Sx1262Adapter*>(arg);
    self->isr_count_ = self->isr_count_ + 1;
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(self->irq_sem_, &woken);
    if (woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

Sx1262Adapter& lora_radio()
{
    static Sx1262Adapter instance;
    return instance;
}

esp_err_t lora_radio_prepare_from_config()
{
    Sx1262Adapter& r = lora_radio();
    if (r.profile_applied() && r.state() == RadioState::STANDBY) {
        return ESP_OK;
    }
    LoraRfProfile p;
    LoraConfigReport rep;
    esp_err_t err = lora_load_rf_profile(p, rep);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "RF profile CONFIG_NOT_SET: %s", rep.missing);
        return err;
    }
    return r.init(p);
}

esp_err_t Sx1262Adapter::check(int sx_status)
{
    if (sx_status == SX126X_STATUS_OK) {
        return ESP_OK;
    }
    if (hal_ != nullptr && hal_->last_error == COWNECT_ERR_BUSY_TIMEOUT) {
        return COWNECT_ERR_BUSY_TIMEOUT;
    }
    return sx_status == SX126X_STATUS_UNKNOWN_VALUE ? ESP_ERR_INVALID_ARG : ESP_FAIL;
}

esp_err_t Sx1262Adapter::init_hardware()
{
    if (hw_ready_) {
        return ESP_OK;
    }
    LoraDriverConfig dcfg;
    LoraConfigReport rep;
    lora_load_driver_config(dcfg, rep);

    s_ctx = {};
    s_ctx.pin_cs = board::PIN_LORA_CS;
    s_ctx.pin_reset = board::PIN_LORA_RST;
    s_ctx.pin_busy = board::PIN_LORA_BUSY;
    s_ctx.busy_timeout_ms = dcfg.busy_timeout_ms;
    s_ctx.dma_capacity = kDmaBytes;
    s_ctx.dma_tx = static_cast<uint8_t*>(heap_caps_malloc(kDmaBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    s_ctx.dma_rx = static_cast<uint8_t*>(heap_caps_malloc(kDmaBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (s_ctx.dma_tx == nullptr || s_ctx.dma_rx == nullptr) {
        heap_caps_free(s_ctx.dma_tx);
        heap_caps_free(s_ctx.dma_rx);
        s_ctx.dma_tx = s_ctx.dma_rx = nullptr;
        return ESP_ERR_NO_MEM;
    }
    hal_ = &s_ctx;

    // Idle levels first: NSS high (deselected), NRESET high (released).
    gpio_set_level(static_cast<gpio_num_t>(board::PIN_LORA_CS), 1);
    gpio_set_level(static_cast<gpio_num_t>(board::PIN_LORA_RST), 1);
    gpio_config_t out = {};
    out.pin_bit_mask = (1ULL << board::PIN_LORA_CS) | (1ULL << board::PIN_LORA_RST);
    out.mode = GPIO_MODE_OUTPUT;
    esp_err_t err = gpio_config(&out);
    gpio_config_t busy = {};
    busy.pin_bit_mask = 1ULL << board::PIN_LORA_BUSY;
    busy.mode = GPIO_MODE_INPUT;
    if (err == ESP_OK) err = gpio_config(&busy);
    gpio_config_t dio1 = {};
    dio1.pin_bit_mask = 1ULL << board::PIN_LORA_DIO1;
    dio1.mode = GPIO_MODE_INPUT;
    dio1.intr_type = GPIO_INTR_POSEDGE;
    if (err == ESP_OK) err = gpio_config(&dio1);
    if (err != ESP_OK) {
        return err;
    }

    spi_bus_config_t bus = {};
    bus.mosi_io_num = board::PIN_LORA_MOSI;
    bus.miso_io_num = board::PIN_LORA_MISO;
    bus.sclk_io_num = board::PIN_LORA_SCK;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = kDmaBytes;
    err = spi_bus_initialize(kSpiHost, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        return err;
    }
    spi_device_interface_config_t dev = {};
    dev.mode = 0;
    dev.clock_speed_hz = static_cast<int>(dcfg.spi_clock_hz);
    dev.spics_io_num = -1;  // NSS driven manually so BUSY can be checked before selection
    dev.queue_size = 1;
    err = spi_bus_add_device(kSpiHost, &dev, &s_ctx.spi);
    if (err != ESP_OK) {
        spi_bus_free(kSpiHost);
        return err;
    }

    if (irq_sem_ == nullptr) {
        irq_sem_ = xSemaphoreCreateBinary();
    }
    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    err = gpio_isr_handler_add(static_cast<gpio_num_t>(board::PIN_LORA_DIO1), &Sx1262Adapter::dio1_isr, this);
    if (err != ESP_OK) {
        return err;
    }
    hw_ready_ = true;
    state_ = RadioState::HARDWARE_READY;
    ESP_LOGI(TAG, "hardware ready SPI=%uHz busy_bound=%ums", static_cast<unsigned>(dcfg.spi_clock_hz),
             static_cast<unsigned>(dcfg.busy_timeout_ms));
    return ESP_OK;
}

esp_err_t Sx1262Adapter::deinit()
{
    if (!hw_ready_) {
        return ESP_OK;
    }
    gpio_isr_handler_remove(static_cast<gpio_num_t>(board::PIN_LORA_DIO1));
    spi_bus_remove_device(s_ctx.spi);
    spi_bus_free(kSpiHost);
    heap_caps_free(s_ctx.dma_tx);
    heap_caps_free(s_ctx.dma_rx);
    s_ctx.dma_tx = s_ctx.dma_rx = nullptr;
    hw_ready_ = false;
    profile_applied_ = false;
    state_ = RadioState::UNINITIALIZED;
    return ESP_OK;
}

void Sx1262Adapter::mark_power_lost()
{
    profile_applied_ = false;
    rx_pending_ = false;
    if (hw_ready_) {
        state_ = RadioState::HARDWARE_READY;
    }
}

bool Sx1262Adapter::busy_level() const
{
    return gpio_get_level(static_cast<gpio_num_t>(board::PIN_LORA_BUSY)) != 0;
}

esp_err_t Sx1262Adapter::hardware_reset()
{
    esp_err_t err = init_hardware();
    if (err != ESP_OK) {
        return err;
    }
    profile_applied_ = false;
    err = sx1262_hardware_reset(s_ctx);
    state_ = err == ESP_OK ? RadioState::HARDWARE_READY : RadioState::ERROR;
    return err;
}

esp_err_t Sx1262Adapter::read_status(LoraChipStatus& out)
{
    out = {};
    if (!hw_ready_) {
        return ESP_ERR_INVALID_STATE;
    }
    sx126x_chip_status_t st = {};
    esp_err_t err = check(sx126x_get_status(hal_, &st));
    if (err != ESP_OK) {
        return err;
    }
    sx126x_errors_mask_t errors = 0;
    err = check(sx126x_get_device_errors(hal_, &errors));
    out.chip_mode = static_cast<uint8_t>(st.chip_mode);
    out.cmd_status = static_cast<uint8_t>(st.cmd_status);
    out.device_errors = errors;
    out.busy_level = busy_level();
    out.last_busy_wait_us = s_ctx.last_busy_wait_us;
    return err;
}

esp_err_t Sx1262Adapter::standby_without_profile()
{
    if (!hw_ready_) {
        return ESP_ERR_INVALID_STATE;
    }
    return check(sx126x_set_standby(hal_, SX126X_STANDBY_CFG_RC));
}

esp_err_t Sx1262Adapter::apply_packet_params(uint8_t payload_len)
{
    sx126x_pkt_params_lora_t pp = {};
    pp.preamble_len_in_symb = profile_.preamble_symbols;
    pp.header_type = SX126X_LORA_PKT_EXPLICIT;
    pp.pld_len_in_bytes = payload_len;
    pp.crc_is_on = profile_.crc_enabled;
    pp.invert_iq_is_on = profile_.invert_iq;
    return check(sx126x_set_lora_pkt_params(hal_, &pp));
}

esp_err_t Sx1262Adapter::init(const LoraRfProfile& p)
{
    profile_applied_ = false;
    sx126x_lora_bw_t bw = SX126X_LORA_BW_007;  // placeholder only; replaced from the profile below
    if (!bw_to_enum(p.bandwidth_hz, bw) || p.spreading_factor < 5 || p.spreading_factor > 12 || p.coding_rate < 5 ||
        p.coding_rate > 8 || p.frequency_hz == 0) {
        return COWNECT_ERR_NOT_CONFIGURED;
    }
    esp_err_t err = hardware_reset();
    if (err != ESP_OK) {
        return err;
    }
    profile_ = p;
    auto step = [&](int st) {
        if (err == ESP_OK) err = check(st);
    };
    step(sx126x_set_standby(hal_, SX126X_STANDBY_CFG_RC));
    step(sx126x_set_reg_mode(hal_, p.regulator == LoraRegulatorMode::DCDC ? SX126X_REG_MODE_DCDC : SX126X_REG_MODE_LDO));
    if (p.tcxo_enabled) {
        const uint32_t steps = (p.tcxo_startup_us * 64u + 999u) / 1000u;  // 15.625 us RTC steps
        step(sx126x_set_dio3_as_tcxo_ctrl(hal_, static_cast<sx126x_tcxo_ctrl_voltages_t>(p.tcxo_voltage_code), steps));
    }
    step(sx126x_cal(hal_, SX126X_CAL_ALL));
    step(sx126x_set_dio2_as_rf_sw_ctrl(hal_, p.dio2_rf_switch));
    step(sx126x_set_pkt_type(hal_, SX126X_PKT_TYPE_LORA));
    step(sx126x_set_rf_freq(hal_, p.frequency_hz));
    const uint16_t f_mhz = static_cast<uint16_t>(p.frequency_hz / 1000000u);
    step(sx126x_cal_img_in_mhz(hal_, f_mhz, f_mhz));
    sx126x_pa_cfg_params_t pa = {};
    pa.pa_duty_cycle = p.pa_duty_cycle;
    pa.hp_max = p.pa_hp_max;
    pa.device_sel = 0x00;  // SX1262 (datasheet SetPaConfig)
    pa.pa_lut = 0x01;      // datasheet: always 0x01
    step(sx126x_set_pa_cfg(hal_, &pa));
    step(sx126x_set_tx_params(hal_, p.tx_power_dbm, ramp_to_enum(p.ramp_time_us)));
    sx126x_mod_params_lora_t mp = {};
    mp.sf = static_cast<sx126x_lora_sf_t>(p.spreading_factor);
    mp.bw = bw;
    mp.cr = static_cast<sx126x_lora_cr_t>(p.coding_rate - 4);
    // Datasheet recommendation: LowDataRateOptimize when symbol time >= 16.38 ms.
    const double symbol_ms = static_cast<double>(1u << p.spreading_factor) * 1000.0 / p.bandwidth_hz;
    mp.ldro = symbol_ms >= 16.38 ? 1 : 0;
    step(sx126x_set_lora_mod_params(hal_, &mp));
    if (err == ESP_OK) err = apply_packet_params(255);
    step(sx126x_set_lora_sync_word(hal_, p.sync_word));
    step(sx126x_set_buffer_base_address(hal_, 0x00, 0x00));
    step(sx126x_set_dio_irq_params(hal_, kIrqMask, kIrqMask, SX126X_IRQ_NONE, SX126X_IRQ_NONE));
    step(sx126x_clear_irq_status(hal_, SX126X_IRQ_ALL));
    if (err != ESP_OK) {
        state_ = RadioState::ERROR;
        ESP_LOGE(TAG, "profile apply failed: %s", cownect_err_name(err));
        return err;
    }
    xSemaphoreTake(irq_sem_, 0);
    profile_applied_ = true;
    state_ = RadioState::STANDBY;
    ESP_LOGI(TAG, "profile applied f=%uHz bw=%u sf=%u cr=4/%u pwr=%ddBm", static_cast<unsigned>(p.frequency_hz),
             static_cast<unsigned>(p.bandwidth_hz), p.spreading_factor, p.coding_rate, p.tx_power_dbm);
    return ESP_OK;
}

esp_err_t Sx1262Adapter::enter_standby()
{
    if (!hw_ready_) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = check(sx126x_set_standby(hal_, SX126X_STANDBY_CFG_RC));
    if (err == ESP_OK && profile_applied_) {
        state_ = RadioState::STANDBY;
    }
    return err;
}

esp_err_t Sx1262Adapter::send_async(const uint8_t* data, size_t length)
{
    const char* reason = nullptr;
    esp_err_t gate = lora_tx_gate(&reason);
    if (gate != ESP_OK) {
        ESP_LOGW(TAG, "TX refused: %s", reason);
        return gate;
    }
    if (!profile_applied_ || state_ != RadioState::STANDBY) {
        return ESP_ERR_INVALID_STATE;
    }
    if (data == nullptr || length == 0 || length > config::SX126X_MAX_PAYLOAD_BYTES) {
        return ESP_ERR_INVALID_SIZE;
    }
    LoraDriverConfig dcfg;
    LoraConfigReport rep;
    lora_load_driver_config(dcfg, rep);
    esp_err_t err = apply_packet_params(static_cast<uint8_t>(length));
    if (err == ESP_OK) err = check(sx126x_write_buffer(hal_, 0x00, data, static_cast<uint8_t>(length)));
    if (err == ESP_OK) err = check(sx126x_clear_irq_status(hal_, SX126X_IRQ_ALL));
    xSemaphoreTake(irq_sem_, 0);
    if (err == ESP_OK) err = check(sx126x_set_tx(hal_, dcfg.tx_operation_timeout_ms));
    if (err != ESP_OK) {
        state_ = RadioState::ERROR;
        return err;
    }
    stats_.tx_started++;
    last_tx_result_ = LoraOpResult::NONE;
    op_start_us_ = esp_timer_get_time();
    op_timeout_ms_ = dcfg.tx_operation_timeout_ms;
    state_ = RadioState::TX_ACTIVE;
    return ESP_OK;
}

esp_err_t Sx1262Adapter::start_receive(uint32_t timeout_ms)
{
    if (!profile_applied_ || state_ != RadioState::STANDBY) {
        return ESP_ERR_INVALID_STATE;
    }
    if (timeout_ms == 0 || timeout_ms > SX126X_MAX_TIMEOUT_IN_MS) {
        return ESP_ERR_INVALID_ARG;  // RX windows must be bounded
    }
    esp_err_t err = apply_packet_params(255);
    if (err == ESP_OK) err = check(sx126x_clear_irq_status(hal_, SX126X_IRQ_ALL));
    xSemaphoreTake(irq_sem_, 0);
    if (err == ESP_OK) err = check(sx126x_set_rx(hal_, timeout_ms));
    if (err != ESP_OK) {
        state_ = RadioState::ERROR;
        return err;
    }
    rx_pending_ = false;
    last_rx_result_ = LoraOpResult::NONE;
    op_start_us_ = esp_timer_get_time();
    op_timeout_ms_ = timeout_ms;
    state_ = RadioState::RX_ACTIVE;
    return ESP_OK;
}

void Sx1262Adapter::to_standby_after_event()
{
    sx126x_set_standby(hal_, SX126X_STANDBY_CFG_RC);
    state_ = RadioState::STANDBY;
}

esp_err_t Sx1262Adapter::service()
{
    if (state_ != RadioState::TX_ACTIVE && state_ != RadioState::RX_ACTIVE) {
        return ESP_OK;
    }
    const bool event = xSemaphoreTake(irq_sem_, 0) == pdTRUE ||
                       gpio_get_level(static_cast<gpio_num_t>(board::PIN_LORA_DIO1)) != 0;
    const int64_t elapsed_ms = (esp_timer_get_time() - op_start_us_) / 1000;
    if (!event) {
        // Software watchdog in case a DIO1 edge is missed: radio timeout + BUSY bound.
        if (elapsed_ms > static_cast<int64_t>(op_timeout_ms_) + static_cast<int64_t>(s_ctx.busy_timeout_ms)) {
            if (state_ == RadioState::TX_ACTIVE) {
                stats_.tx_timeout++;
                last_tx_result_ = LoraOpResult::TIMEOUT;
            } else {
                stats_.rx_timeout++;
                last_rx_result_ = LoraOpResult::TIMEOUT;
            }
            to_standby_after_event();
        }
        return ESP_OK;
    }
    sx126x_irq_mask_t irq = 0;
    esp_err_t err = check(sx126x_get_and_clear_irq_status(hal_, &irq));
    if (err != ESP_OK) {
        state_ = RadioState::ERROR;
        return err;
    }
    if (state_ == RadioState::TX_ACTIVE) {
        if (irq & SX126X_IRQ_TX_DONE) {
            stats_.tx_done++;
            last_tx_result_ = LoraOpResult::OK;
            state_ = RadioState::STANDBY;  // chip falls back to STDBY_RC after TX_DONE
        } else if (irq & SX126X_IRQ_TIMEOUT) {
            stats_.tx_timeout++;
            last_tx_result_ = LoraOpResult::TIMEOUT;
            to_standby_after_event();
        } else if (irq != 0) {
            stats_.unexpected_irq_count++;
        }
        return ESP_OK;
    }
    // RX_ACTIVE
    if (irq & SX126X_IRQ_RX_DONE) {
        if (irq & (SX126X_IRQ_CRC_ERROR | SX126X_IRQ_HEADER_ERROR)) {
            stats_.rx_crc_error++;
            last_rx_result_ = LoraOpResult::CRC_ERROR;
        } else {
            sx126x_rx_buffer_status_t bs = {};
            sx126x_pkt_status_lora_t ps = {};
            err = check(sx126x_get_rx_buffer_status(hal_, &bs));
            // The radio-reported length is bounded by rx_buf_ (uint8 length <= 255).
            if (err == ESP_OK && bs.pld_len_in_bytes > 0) {
                err = check(sx126x_read_buffer(hal_, bs.buffer_start_pointer, rx_buf_, bs.pld_len_in_bytes));
            }
            if (err == ESP_OK) err = check(sx126x_get_lora_pkt_status(hal_, &ps));
            if (err == ESP_OK) {
                rx_meta_.length = bs.pld_len_in_bytes;
                rx_meta_.rssi_dbm = ps.rssi_pkt_in_dbm;
                rx_meta_.snr_db = ps.snr_pkt_in_db;
                rx_meta_.signal_rssi_dbm = ps.signal_rssi_pkt_in_dbm;
                rx_pending_ = true;
                stats_.rx_done++;
                last_rx_result_ = LoraOpResult::OK;
            } else {
                last_rx_result_ = LoraOpResult::ERROR;
            }
        }
        to_standby_after_event();
    } else if (irq & SX126X_IRQ_TIMEOUT) {
        stats_.rx_timeout++;
        last_rx_result_ = LoraOpResult::TIMEOUT;
        to_standby_after_event();
    } else if (irq & (SX126X_IRQ_CRC_ERROR | SX126X_IRQ_HEADER_ERROR)) {
        stats_.rx_crc_error++;
        last_rx_result_ = LoraOpResult::CRC_ERROR;
        to_standby_after_event();
    } else if (irq != 0) {
        stats_.unexpected_irq_count++;
    }
    return ESP_OK;
}

esp_err_t Sx1262Adapter::wait_for_event(uint32_t timeout_ms)
{
    if (state_ != RadioState::TX_ACTIVE && state_ != RadioState::RX_ACTIVE) {
        return ESP_OK;
    }
    if (xSemaphoreTake(irq_sem_, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
        xSemaphoreGive(irq_sem_);  // let service() observe the event
    }
    return service();
}

esp_err_t Sx1262Adapter::send_blocking(const uint8_t* data, size_t length, uint32_t& elapsed_ms)
{
    const int64_t t0 = esp_timer_get_time();
    esp_err_t err = send_async(data, length);
    if (err != ESP_OK) {
        elapsed_ms = 0;
        return err;
    }
    // Bounded: the software watchdog in service() ends TX_ACTIVE after timeout + BUSY bound.
    while (tx_in_progress()) {
        err = wait_for_event(100);
        if (err != ESP_OK) {
            break;
        }
    }
    elapsed_ms = static_cast<uint32_t>((esp_timer_get_time() - t0) / 1000);
    if (err != ESP_OK) {
        return err;
    }
    return last_tx_result_ == LoraOpResult::OK ? ESP_OK : ESP_ERR_TIMEOUT;
}

bool Sx1262Adapter::take_last_rx(uint8_t* dst, size_t capacity, LoraPacketRx& meta)
{
    if (!rx_pending_ || dst == nullptr || capacity < rx_meta_.length) {
        return false;
    }
    std::memcpy(dst, rx_buf_, rx_meta_.length);
    meta = rx_meta_;
    rx_pending_ = false;
    return true;
}

const LoraRadioStats& Sx1262Adapter::stats() const
{
    stats_.irq_count = isr_count_;
    stats_.busy_timeout_count = s_ctx.busy_timeout_count;
    stats_.spi_error_count = s_ctx.spi_error_count;
    stats_.reset_count = s_ctx.reset_count;
    return stats_;
}

void Sx1262Adapter::reset_stats()
{
    stats_ = {};
    isr_count_ = 0;
    s_ctx.busy_timeout_count = 0;
    s_ctx.spi_error_count = 0;
    s_ctx.reset_count = 0;
}

}  // namespace cownect::radio
