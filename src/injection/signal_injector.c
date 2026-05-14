/**
 * Signal Injection Engine Implementation
 * Generates and injects signals across all supported protocols
 */
#include "injection/signal_injector.h"
#include "protocols/protocol_driver.h"
#include "core/task_manager.h"
#include "driver/dac_oneshot.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <math.h>

static const char *TAG = "injector";

static injection_pattern_t s_patterns[MAX_SIGNAL_PATTERNS];
static uint8_t s_pattern_count = 0;
static bool s_running = false;
static bool s_paused = false;
static uint32_t s_packets_sent = 0;
static TaskHandle_t s_inject_task = NULL;
static dac_oneshot_handle_t s_dac_handle = NULL;
static esp_timer_handle_t s_wave_timer = NULL;

/* Waveform state */
static signal_params_t s_wave_params;
static float s_wave_phase = 0.0f;

/* ── PRBS Generator ──────────────────────────────────────────── */
static uint32_t s_prbs_state = 0x1;

static uint8_t prbs_next_bit(uint8_t order)
{
    uint32_t feedback;
    switch (order) {
        case 7:  feedback = ((s_prbs_state >> 6) ^ (s_prbs_state >> 5)) & 1; break;
        case 9:  feedback = ((s_prbs_state >> 8) ^ (s_prbs_state >> 4)) & 1; break;
        case 15: feedback = ((s_prbs_state >> 14) ^ (s_prbs_state >> 13)) & 1; break;
        case 23: feedback = ((s_prbs_state >> 22) ^ (s_prbs_state >> 17)) & 1; break;
        case 31: feedback = ((s_prbs_state >> 30) ^ (s_prbs_state >> 27)) & 1; break;
        default: feedback = ((s_prbs_state >> 10) ^ (s_prbs_state >> 6)) & 1; break;
    }
    s_prbs_state = (s_prbs_state << 1) | feedback;
    return feedback;
}

static uint8_t prbs_next_byte(uint8_t order)
{
    uint8_t byte = 0;
    for (int i = 0; i < 8; i++) {
        byte = (byte << 1) | prbs_next_bit(order);
    }
    return byte;
}

/* ── Waveform Timer Callback ─────────────────────────────────── */
static void waveform_timer_cb(void *arg)
{
    if (!s_dac_handle) return;

    float sample = 0.0f;
    switch (s_wave_params.type) {
        case WAVE_SINE:
            sample = sinf(2.0f * M_PI * s_wave_phase);
            break;
        case WAVE_SQUARE:
            sample = (s_wave_phase < s_wave_params.duty_cycle / 100.0f) ? 1.0f : -1.0f;
            break;
        case WAVE_TRIANGLE:
            sample = (s_wave_phase < 0.5f) ?
                     (4.0f * s_wave_phase - 1.0f) :
                     (3.0f - 4.0f * s_wave_phase);
            break;
        case WAVE_SAWTOOTH:
            sample = 2.0f * s_wave_phase - 1.0f;
            break;
        case WAVE_NOISE:
            sample = ((float)(esp_random() & 0xFFFF) / 32768.0f) - 1.0f;
            break;
        case WAVE_PULSE:
            sample = (s_wave_phase < s_wave_params.duty_cycle / 100.0f) ? 1.0f : 0.0f;
            break;
        default:
            break;
    }

    /* Scale to DAC range (0-255) */
    uint8_t dac_val = (uint8_t)((sample * s_wave_params.amplitude / 2.0f + s_wave_params.offset + 1.0f) * 127.5f);
    dac_oneshot_output_voltage(s_dac_handle, dac_val);

    /* Advance phase */
    float sample_rate = 10000.0f;  /* 10 kHz timer */
    s_wave_phase += s_wave_params.frequency / sample_rate;
    if (s_wave_phase >= 1.0f) s_wave_phase -= 1.0f;
}

/* ── Injection Task ──────────────────────────────────────────── */
static void injection_task(void *pvParameter)
{
    ESP_LOGI(TAG, "Injection task started");

    while (s_running) {
        if (s_paused) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        for (uint8_t i = 0; i < s_pattern_count; i++) {
            injection_pattern_t *pat = &s_patterns[i];
            if (!pat->active) continue;

            protocol_driver_t *drv = protocol_get_driver(pat->protocol);
            if (!drv || !drv->is_ready()) continue;

            if (pat->pattern_data && pat->pattern_len > 0) {
                drv->send(pat->pattern_data, pat->pattern_len);
                s_packets_sent++;
            }

            if (pat->repeat_count > 0) {
                pat->repeat_count--;
                if (pat->repeat_count == 0) {
                    pat->active = false;
                }
            }

            if (pat->interval_ms > 0) {
                vTaskDelay(pdMS_TO_TICKS(pat->interval_ms));
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }

    ESP_LOGI(TAG, "Injection task stopped, %lu packets sent", s_packets_sent);
    s_inject_task = NULL;
    vTaskDelete(NULL);
}

/* ── Public API ──────────────────────────────────────────────── */

sig_err_t injector_init(void)
{
    memset(s_patterns, 0, sizeof(s_patterns));
    s_pattern_count = 0;
    s_running = false;
    s_paused = false;
    s_packets_sent = 0;

    /* Initialize DAC for waveform generation */
    dac_oneshot_config_t dac_cfg = {
        .chan_id = DAC_CHAN_0,  /* GPIO25 */
    };
    dac_oneshot_new_channel(&dac_cfg, &s_dac_handle);

    ESP_LOGI(TAG, "Signal injector initialized");
    return SIG_OK;
}

sig_err_t injector_deinit(void)
{
    injector_stop();
    injector_stop_waveform();
    if (s_dac_handle) {
        dac_oneshot_del_channel(s_dac_handle);
        s_dac_handle = NULL;
    }
    return SIG_OK;
}

sig_err_t injector_add_pattern(const injection_pattern_t *pattern, uint8_t *id)
{
    if (s_pattern_count >= MAX_SIGNAL_PATTERNS) return SIG_ERR_BUFFER_FULL;
    memcpy(&s_patterns[s_pattern_count], pattern, sizeof(injection_pattern_t));
    *id = s_pattern_count;
    s_pattern_count++;
    return SIG_OK;
}

sig_err_t injector_remove_pattern(uint8_t id)
{
    if (id >= s_pattern_count) return SIG_ERR_INVALID_PARAM;
    s_patterns[id].active = false;
    return SIG_OK;
}

sig_err_t injector_clear_patterns(void)
{
    memset(s_patterns, 0, sizeof(s_patterns));
    s_pattern_count = 0;
    return SIG_OK;
}

sig_err_t injector_start(void)
{
    if (s_running) return SIG_ERR_BUSY;
    s_running = true;
    s_paused = false;

    xTaskCreate(injection_task, "inject", TASK_STACK_INJECTION,
                NULL, TASK_PRIORITY_INJECTION, &s_inject_task);
    return SIG_OK;
}

sig_err_t injector_stop(void)
{
    s_running = false;
    if (s_inject_task) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return SIG_OK;
}

sig_err_t injector_pause(void)
{
    s_paused = true;
    return SIG_OK;
}

sig_err_t injector_resume(void)
{
    s_paused = false;
    return SIG_OK;
}

sig_err_t injector_generate_waveform(const signal_params_t *params)
{
    if (!params) return SIG_ERR_INVALID_PARAM;

    memcpy(&s_wave_params, params, sizeof(signal_params_t));
    s_wave_phase = params->phase / 360.0f;

    if (s_wave_timer) {
        esp_timer_stop(s_wave_timer);
        esp_timer_delete(s_wave_timer);
    }

    esp_timer_create_args_t timer_args = {
        .callback = waveform_timer_cb,
        .arg = NULL,
        .name = "waveform",
    };
    esp_timer_create(&timer_args, &s_wave_timer);
    esp_timer_start_periodic(s_wave_timer, 100);  /* 10 kHz sample rate */

    ESP_LOGI(TAG, "Waveform started: type=%d, freq=%.1f Hz, amp=%.2f",
             params->type, params->frequency, params->amplitude);
    return SIG_OK;
}

sig_err_t injector_stop_waveform(void)
{
    if (s_wave_timer) {
        esp_timer_stop(s_wave_timer);
        esp_timer_delete(s_wave_timer);
        s_wave_timer = NULL;
    }
    return SIG_OK;
}

/* ── Protocol-Specific Injection ─────────────────────────────── */

sig_err_t injector_send_uart(const uint8_t *data, uint16_t len)
{
    protocol_driver_t *drv = protocol_get_driver(PROTO_UART);
    if (!drv || !drv->is_ready()) return SIG_ERR_NOT_INITIALIZED;
    s_packets_sent++;
    return drv->send(data, len);
}

sig_err_t injector_send_spi(const uint8_t *tx, uint8_t *rx, uint16_t len)
{
    protocol_driver_t *drv = protocol_get_driver(PROTO_SPI);
    if (!drv || !drv->is_ready()) return SIG_ERR_NOT_INITIALIZED;
    sig_err_t err = drv->send(tx, len);
    if (err == SIG_OK && rx) {
        uint16_t rx_len = len;
        drv->receive(rx, &rx_len, 100);
    }
    s_packets_sent++;
    return err;
}

sig_err_t injector_send_i2c(uint8_t addr, const uint8_t *data, uint16_t len)
{
    protocol_driver_t *drv = protocol_get_driver(PROTO_I2C);
    if (!drv || !drv->is_ready()) return SIG_ERR_NOT_INITIALIZED;

    uint8_t buf[256];
    buf[0] = addr;
    uint16_t copy_len = (len > sizeof(buf) - 1) ? sizeof(buf) - 1 : len;
    memcpy(&buf[1], data, copy_len);
    s_packets_sent++;
    return drv->send(buf, copy_len + 1);
}

sig_err_t injector_send_can(uint32_t id, const uint8_t *data, uint8_t len, bool extended)
{
    protocol_driver_t *drv = protocol_get_driver(PROTO_CAN);
    if (!drv || !drv->is_ready()) return SIG_ERR_NOT_INITIALIZED;

    uint8_t buf[12];
    buf[0] = (id >> 24) & 0xFF;
    buf[1] = (id >> 16) & 0xFF;
    buf[2] = (id >> 8) & 0xFF;
    buf[3] = id & 0xFF;
    uint8_t copy_len = (len > 8) ? 8 : len;
    memcpy(&buf[4], data, copy_len);
    s_packets_sent++;
    return drv->send(buf, 4 + copy_len);
}

sig_err_t injector_send_modbus(uint8_t func, uint16_t reg, uint16_t value)
{
    protocol_driver_t *drv = protocol_get_driver(PROTO_MODBUS_RTU);
    if (!drv || !drv->is_ready()) return SIG_ERR_NOT_INITIALIZED;

    uint8_t request[6] = {
        1,  /* Default slave ID */
        func,
        (reg >> 8) & 0xFF, reg & 0xFF,
        (value >> 8) & 0xFF, value & 0xFF,
    };
    s_packets_sent++;
    return drv->send(request, sizeof(request));
}

sig_err_t injector_set_pwm(uint8_t channel, float duty, uint32_t freq)
{
    protocol_driver_t *drv = protocol_get_driver(PROTO_PWM);
    if (!drv || !drv->is_ready()) return SIG_ERR_NOT_INITIALIZED;

    uint8_t data[2] = { channel, (uint8_t)(duty * 2.55f) };
    return drv->send(data, 2);
}

sig_err_t injector_set_gpio(uint8_t pin, bool level)
{
    extern sig_err_t gpio_set(uint8_t pin, bool level);
    return gpio_set(pin, level);
}

sig_err_t injector_pulse_gpio(uint8_t pin, uint32_t duration_us)
{
    extern sig_err_t gpio_pulse(uint8_t pin, uint32_t duration_us);
    return gpio_pulse(pin, duration_us);
}

/* ── Built-in Test Patterns ──────────────────────────────────── */

sig_err_t injector_send_walking_ones(protocol_type_t proto, uint16_t len)
{
    protocol_driver_t *drv = protocol_get_driver(proto);
    if (!drv || !drv->is_ready()) return SIG_ERR_NOT_INITIALIZED;

    uint8_t buf[256];
    uint16_t send_len = (len > sizeof(buf)) ? sizeof(buf) : len;

    for (uint16_t i = 0; i < send_len; i++) {
        buf[i] = 1 << (i % 8);
    }
    s_packets_sent++;
    return drv->send(buf, send_len);
}

sig_err_t injector_send_prbs(protocol_type_t proto, uint8_t order, uint32_t count)
{
    protocol_driver_t *drv = protocol_get_driver(proto);
    if (!drv || !drv->is_ready()) return SIG_ERR_NOT_INITIALIZED;

    s_prbs_state = 0x1;
    uint8_t buf[256];

    for (uint32_t i = 0; i < count; i++) {
        uint16_t len = (count > sizeof(buf)) ? sizeof(buf) : 64;
        for (uint16_t j = 0; j < len; j++) {
            buf[j] = prbs_next_byte(order);
        }
        drv->send(buf, len);
        s_packets_sent++;
    }
    return SIG_OK;
}

sig_err_t injector_send_counter(protocol_type_t proto, uint16_t start, uint16_t count)
{
    protocol_driver_t *drv = protocol_get_driver(proto);
    if (!drv || !drv->is_ready()) return SIG_ERR_NOT_INITIALIZED;

    uint8_t buf[256];
    uint16_t send_len = (count > sizeof(buf)) ? sizeof(buf) : count;

    for (uint16_t i = 0; i < send_len; i++) {
        buf[i] = (start + i) & 0xFF;
    }
    s_packets_sent++;
    return drv->send(buf, send_len);
}

bool injector_is_running(void)
{
    return s_running;
}

uint32_t injector_get_packets_sent(void)
{
    return s_packets_sent;
}
