/**
 * Signal Injection Engine
 * Generates and injects signals across all supported protocols
 */
#pragma once

#include "core/config.h"

/* ── Injection Pattern ───────────────────────────────────────── */
typedef struct {
    char name[32];
    protocol_type_t protocol;
    signal_params_t signal;
    uint8_t *pattern_data;
    uint16_t pattern_len;
    uint32_t repeat_count;    /* 0 = infinite */
    uint32_t interval_ms;     /* Delay between repeats */
    bool active;
} injection_pattern_t;

/* ── Injection Sequence ──────────────────────────────────────── */
typedef struct {
    injection_pattern_t patterns[MAX_SIGNAL_PATTERNS];
    uint8_t pattern_count;
    bool sequential;          /* true = one after another, false = simultaneous */
    uint32_t loop_count;      /* 0 = infinite */
} injection_sequence_t;

/* ── Injector API ────────────────────────────────────────────── */
sig_err_t injector_init(void);
sig_err_t injector_deinit(void);

/* Pattern management */
sig_err_t injector_add_pattern(const injection_pattern_t *pattern, uint8_t *id);
sig_err_t injector_remove_pattern(uint8_t id);
sig_err_t injector_clear_patterns(void);

/* Signal generation */
sig_err_t injector_start(void);
sig_err_t injector_stop(void);
sig_err_t injector_pause(void);
sig_err_t injector_resume(void);

/* Waveform generation (DAC-based) */
sig_err_t injector_generate_waveform(const signal_params_t *params);
sig_err_t injector_stop_waveform(void);

/* Protocol-specific injection */
sig_err_t injector_send_uart(const uint8_t *data, uint16_t len);
sig_err_t injector_send_spi(const uint8_t *tx, uint8_t *rx, uint16_t len);
sig_err_t injector_send_i2c(uint8_t addr, const uint8_t *data, uint16_t len);
sig_err_t injector_send_can(uint32_t id, const uint8_t *data, uint8_t len, bool extended);
sig_err_t injector_send_modbus(uint8_t func, uint16_t reg, uint16_t value);
sig_err_t injector_set_pwm(uint8_t channel, float duty, uint32_t freq);
sig_err_t injector_set_gpio(uint8_t pin, bool level);
sig_err_t injector_pulse_gpio(uint8_t pin, uint32_t duration_us);

/* Built-in test patterns */
sig_err_t injector_send_walking_ones(protocol_type_t proto, uint16_t len);
sig_err_t injector_send_prbs(protocol_type_t proto, uint8_t order, uint32_t count);
sig_err_t injector_send_counter(protocol_type_t proto, uint16_t start, uint16_t count);

/* Status */
bool injector_is_running(void);
uint32_t injector_get_packets_sent(void);
