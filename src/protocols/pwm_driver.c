/**
 * PWM / GPIO / Frequency Driver
 * LEDC-based PWM generation, GPIO control, and frequency measurement
 */
#include "protocols/protocol_driver.h"
#include "core/config.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "pwm_drv";

static bool s_initialized = false;
static pwm_config_params_t s_config;

/* PWM channel to GPIO mapping */
static const int pwm_pins[] = { PIN_PWM_CH0, PIN_PWM_CH1, PIN_PWM_CH2, PIN_PWM_CH3 };
#define NUM_PWM_CHANNELS 4

static sig_err_t pwm_drv_init(void *config)
{
    if (s_initialized) return SIG_ERR_BUSY;

    pwm_config_params_t *cfg = (pwm_config_params_t *)config;
    if (!cfg) {
        s_config.frequency = DEFAULT_PWM_FREQ;
        s_config.resolution = DEFAULT_PWM_RESOLUTION;
        s_config.channel = 0;
        s_config.duty_cycle = 50.0f;
        s_config.gpio_pin = PIN_PWM_CH0;
    } else {
        memcpy(&s_config, cfg, sizeof(pwm_config_params_t));
    }

    /* Configure LEDC timer */
    ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = s_config.resolution,
        .freq_hz = s_config.frequency,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LEDC timer config failed: %s", esp_err_to_name(err));
        return SIG_ERR_HARDWARE;
    }

    /* Configure all PWM channels */
    for (int i = 0; i < NUM_PWM_CHANNELS; i++) {
        ledc_channel_config_t ch_cfg = {
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = (ledc_channel_t)i,
            .timer_sel = LEDC_TIMER_0,
            .intr_type = LEDC_INTR_DISABLE,
            .gpio_num = pwm_pins[i],
            .duty = 0,
            .hpoint = 0,
        };
        ledc_channel_config(&ch_cfg);
    }

    s_initialized = true;
    ESP_LOGI(TAG, "PWM initialized @ %lu Hz, %d-bit resolution",
             s_config.frequency, s_config.resolution);
    return SIG_OK;
}

static sig_err_t pwm_drv_deinit(void)
{
    if (!s_initialized) return SIG_ERR_NOT_INITIALIZED;
    for (int i = 0; i < NUM_PWM_CHANNELS; i++) {
        ledc_stop(LEDC_LOW_SPEED_MODE, (ledc_channel_t)i, 0);
    }
    s_initialized = false;
    return SIG_OK;
}

static sig_err_t pwm_drv_send(const uint8_t *data, uint16_t len)
{
    if (!s_initialized) return SIG_ERR_NOT_INITIALIZED;
    if (len < 2) return SIG_ERR_INVALID_PARAM;

    /* data[0] = channel, data[1] = duty (0-255 for 8-bit) */
    uint8_t ch = data[0];
    if (ch >= NUM_PWM_CHANNELS) return SIG_ERR_INVALID_PARAM;

    uint32_t duty = data[1];
    if (len >= 4) {
        duty = (data[1] << 8) | data[2];
    }

    ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)ch, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)ch);
    return SIG_OK;
}

static sig_err_t pwm_drv_receive(uint8_t *buf, uint16_t *len, uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (!s_initialized) return SIG_ERR_NOT_INITIALIZED;
    /* Return current duty for all channels */
    for (int i = 0; i < NUM_PWM_CHANNELS && i * 4 + 3 < *len; i++) {
        uint32_t duty = ledc_get_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)i);
        buf[i * 4 + 0] = i;
        buf[i * 4 + 1] = (duty >> 16) & 0xFF;
        buf[i * 4 + 2] = (duty >> 8) & 0xFF;
        buf[i * 4 + 3] = duty & 0xFF;
    }
    *len = NUM_PWM_CHANNELS * 4;
    return SIG_OK;
}

static sig_err_t pwm_drv_configure(void *config)
{
    pwm_drv_deinit();
    return pwm_drv_init(config);
}

static bool pwm_drv_is_ready(void) { return s_initialized; }

static sig_err_t pwm_drv_get_status(void *status)
{
    if (!status) return SIG_ERR_INVALID_PARAM;
    memcpy(status, &s_config, sizeof(pwm_config_params_t));
    return SIG_OK;
}

/* ── PWM Utilities ───────────────────────────────────────────── */

sig_err_t pwm_set_duty(uint8_t channel, float duty_percent)
{
    if (!s_initialized) return SIG_ERR_NOT_INITIALIZED;
    if (channel >= NUM_PWM_CHANNELS) return SIG_ERR_INVALID_PARAM;

    uint32_t max_duty = (1 << s_config.resolution) - 1;
    uint32_t duty_val = (uint32_t)(duty_percent / 100.0f * max_duty);

    ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)channel, duty_val);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)channel);
    return SIG_OK;
}

sig_err_t pwm_set_frequency(uint32_t freq_hz)
{
    if (!s_initialized) return SIG_ERR_NOT_INITIALIZED;
    ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, freq_hz);
    s_config.frequency = freq_hz;
    return SIG_OK;
}

/* ── GPIO Control ────────────────────────────────────────────── */

sig_err_t gpio_output_init(uint8_t pin)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return (gpio_config(&io_conf) == ESP_OK) ? SIG_OK : SIG_ERR_HARDWARE;
}

sig_err_t gpio_input_init(uint8_t pin, bool pullup)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = pullup ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return (gpio_config(&io_conf) == ESP_OK) ? SIG_OK : SIG_ERR_HARDWARE;
}

sig_err_t gpio_set(uint8_t pin, bool level)
{
    return (gpio_set_level(pin, level ? 1 : 0) == ESP_OK) ? SIG_OK : SIG_ERR_HARDWARE;
}

bool gpio_get(uint8_t pin)
{
    return gpio_get_level(pin) != 0;
}

sig_err_t gpio_pulse(uint8_t pin, uint32_t duration_us)
{
    gpio_set_level(pin, 1);
    esp_rom_delay_us(duration_us);
    gpio_set_level(pin, 0);
    return SIG_OK;
}

protocol_driver_t pwm_driver = {
    .type       = PROTO_PWM,
    .name       = "PWM",
    .init       = pwm_drv_init,
    .deinit     = pwm_drv_deinit,
    .send       = pwm_drv_send,
    .receive    = pwm_drv_receive,
    .configure  = pwm_drv_configure,
    .is_ready   = pwm_drv_is_ready,
    .get_status = pwm_drv_get_status,
};
