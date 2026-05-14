/**
 * Signal Analyzer & Protocol Decoder
 * Captures, analyzes, and decodes signals from all supported protocols
 */
#pragma once

#include "core/config.h"

/* ── Capture Buffer ──────────────────────────────────────────── */
typedef struct {
    uint8_t *buffer;
    uint32_t size;
    uint32_t write_pos;
    uint32_t read_pos;
    uint32_t sample_rate;
    bool overflow;
    bool capturing;
} capture_buffer_t;

/* ── Decoded Frame ───────────────────────────────────────────── */
typedef struct {
    protocol_type_t protocol;
    uint32_t timestamp_us;
    uint8_t *raw_data;
    uint16_t raw_len;
    char decoded_text[256];
    bool has_error;
    uint8_t error_code;
    /* Protocol-specific fields */
    union {
        struct { uint32_t baud; uint8_t bits; uint8_t parity; } uart;
        struct { uint8_t addr; bool read; uint8_t reg; } i2c;
        struct { uint8_t mode; uint16_t word_size; } spi;
        struct { uint32_t id; bool ext; bool rtr; uint8_t dlc; } can;
        struct { uint8_t slave; uint8_t func; uint16_t reg; uint16_t val; } modbus;
        struct { float frequency; float duty; } pwm;
    } fields;
} decoded_frame_t;

/* ── Analysis Statistics ─────────────────────────────────────── */
typedef struct {
    uint32_t total_frames;
    uint32_t valid_frames;
    uint32_t error_frames;
    float avg_frame_rate;
    float min_interval_us;
    float max_interval_us;
    float avg_interval_us;
    uint32_t capture_duration_ms;
    /* Frequency analysis */
    float dominant_freq;
    float signal_amplitude;
    float snr_db;
} analysis_stats_t;

/* ── Trigger Configuration ───────────────────────────────────── */
typedef enum {
    TRIGGER_NONE = 0,
    TRIGGER_LEVEL,
    TRIGGER_EDGE_RISING,
    TRIGGER_EDGE_FALLING,
    TRIGGER_PATTERN,
    TRIGGER_PROTOCOL_ERROR,
} trigger_type_t;

typedef struct {
    trigger_type_t type;
    uint16_t threshold;
    uint8_t *pattern;
    uint8_t pattern_len;
    uint32_t pre_trigger_samples;
    uint32_t post_trigger_samples;
} trigger_config_t;

/* ── Analyzer API ────────────────────────────────────────────── */
sig_err_t analyzer_init(void);
sig_err_t analyzer_deinit(void);

/* Capture control */
sig_err_t analyzer_start_capture(protocol_type_t proto, uint32_t duration_ms);
sig_err_t analyzer_stop_capture(void);
sig_err_t analyzer_set_trigger(const trigger_config_t *trigger);
bool analyzer_is_capturing(void);

/* Data retrieval */
sig_err_t analyzer_get_frame(decoded_frame_t *frame);
sig_err_t analyzer_get_stats(analysis_stats_t *stats);
uint32_t analyzer_get_frame_count(void);

/* Protocol decoding */
sig_err_t analyzer_decode_uart(const uint8_t *raw, uint16_t len, decoded_frame_t *frame);
sig_err_t analyzer_decode_i2c(const uint8_t *raw, uint16_t len, decoded_frame_t *frame);
sig_err_t analyzer_decode_spi(const uint8_t *raw, uint16_t len, decoded_frame_t *frame);
sig_err_t analyzer_decode_can(const uint8_t *raw, uint16_t len, decoded_frame_t *frame);
sig_err_t analyzer_decode_modbus(const uint8_t *raw, uint16_t len, decoded_frame_t *frame);

/* Frequency analysis */
sig_err_t analyzer_measure_frequency(uint8_t gpio_pin, float *freq_hz);
sig_err_t analyzer_measure_duty_cycle(uint8_t gpio_pin, float *duty_pct);
sig_err_t analyzer_measure_pulse_width(uint8_t gpio_pin, uint32_t *width_us);

/* Bus scanning */
sig_err_t analyzer_scan_i2c(uint8_t *found_addrs, uint8_t *count, uint8_t max_count);
sig_err_t analyzer_scan_modbus(uint8_t start_id, uint8_t end_id,
                                uint8_t *found_ids, uint8_t *count);

/* Export */
sig_err_t analyzer_export_csv(const char *filename);
sig_err_t analyzer_export_json(char *buf, uint32_t buf_size, uint32_t *written);
