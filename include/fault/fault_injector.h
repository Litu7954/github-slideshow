/**
 * Fault Injection Module
 * Introduces controlled faults for robustness testing
 */
#pragma once

#include "core/config.h"

/* ── Fault Rule ──────────────────────────────────────────────── */
typedef struct {
    uint8_t id;
    fault_type_t type;
    protocol_type_t target_protocol;
    bool enabled;
    float probability;          /* 0.0 - 1.0 */
    uint32_t interval_ms;       /* For periodic faults */
    uint32_t duration_us;       /* Fault duration */
    union {
        struct { uint8_t bit_pos; } bit_flip;
        struct { uint32_t delay_us; } delay;
        struct { float drop_rate; } drop;
        struct { uint8_t noise_level; } noise;
        struct { uint32_t glitch_us; uint8_t level; } glitch;
        struct { float voltage_pct; } voltage;
        struct { int32_t offset_us; } timing;
    } params;
    uint32_t trigger_count;
    uint32_t max_triggers;      /* 0 = unlimited */
} fault_rule_t;

/* ── Fault Statistics ────────────────────────────────────────── */
typedef struct {
    uint32_t total_faults_injected;
    uint32_t bit_flips;
    uint32_t delays_added;
    uint32_t packets_dropped;
    uint32_t crc_corruptions;
    uint32_t glitches;
    uint32_t noise_injections;
    uint32_t timing_errors;
    uint32_t bus_off_events;
} fault_stats_t;

/* ── Fault Injector API ──────────────────────────────────────── */
sig_err_t fault_injector_init(void);
sig_err_t fault_injector_deinit(void);

/* Rule management */
sig_err_t fault_add_rule(const fault_rule_t *rule, uint8_t *id);
sig_err_t fault_remove_rule(uint8_t id);
sig_err_t fault_enable_rule(uint8_t id, bool enable);
sig_err_t fault_clear_rules(void);

/* Control */
sig_err_t fault_injector_start(void);
sig_err_t fault_injector_stop(void);
bool fault_injector_is_active(void);

/* Apply fault to data (called by protocol drivers) */
sig_err_t fault_process_packet(uint8_t *data, uint16_t *len, protocol_type_t proto);

/* Statistics */
sig_err_t fault_get_stats(fault_stats_t *stats);
sig_err_t fault_reset_stats(void);

/* Convenience fault creators */
sig_err_t fault_create_bit_flip(protocol_type_t proto, float probability, uint8_t *id);
sig_err_t fault_create_delay(protocol_type_t proto, uint32_t delay_us, uint8_t *id);
sig_err_t fault_create_drop(protocol_type_t proto, float drop_rate, uint8_t *id);
sig_err_t fault_create_noise(protocol_type_t proto, uint8_t level, uint8_t *id);
sig_err_t fault_create_glitch(protocol_type_t proto, uint32_t glitch_us, uint8_t *id);
