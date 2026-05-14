/**
 * Loopback & BER Testing Module
 * Comprehensive signal integrity and protocol testing
 */
#pragma once

#include "core/config.h"

/* ── Test Configuration ──────────────────────────────────────── */
typedef struct {
    protocol_type_t protocol;
    uint32_t packet_count;       /* Number of test packets */
    uint16_t packet_size;        /* Bytes per packet */
    uint32_t interval_ms;        /* Delay between packets */
    uint32_t timeout_ms;         /* Per-packet timeout */
    bool verify_data;            /* Compare TX vs RX data */
    bool measure_latency;
    uint8_t test_pattern;        /* 0=counter, 1=PRBS, 2=walking 1s, 3=all 0xAA, 4=all 0x55 */
} test_config_t;

/* ── Loopback Test Types ─────────────────────────────────────── */
typedef enum {
    LOOPBACK_INTERNAL,      /* Internal loopback (no external wiring) */
    LOOPBACK_EXTERNAL,      /* TX connected to RX externally */
    LOOPBACK_ECHO,          /* DUT echoes back */
} loopback_mode_t;

/* ── BER Test Parameters ─────────────────────────────────────── */
typedef struct {
    protocol_type_t protocol;
    uint32_t total_bits;         /* Total bits to test (0 = time-based) */
    uint32_t duration_sec;       /* Test duration in seconds */
    uint8_t prbs_order;          /* PRBS polynomial order (7, 9, 11, 15, 23, 31) */
    bool real_time_display;      /* Update results in real-time */
} ber_config_t;

/* ── BER Test Results ────────────────────────────────────────── */
typedef struct {
    uint64_t total_bits_sent;
    uint64_t total_bits_received;
    uint64_t bit_errors;
    double bit_error_rate;
    uint32_t sync_losses;
    uint32_t sync_recoveries;
    float elapsed_seconds;
    bool test_complete;
    bool synced;
} ber_result_t;

/* ── Stress Test Config ──────────────────────────────────────── */
typedef struct {
    protocol_type_t protocol;
    uint32_t duration_sec;
    float load_percent;          /* 0-100, percentage of max throughput */
    bool vary_packet_size;       /* Randomize packet sizes */
    bool vary_timing;            /* Randomize inter-packet timing */
    bool inject_errors;          /* Enable fault injection during test */
} stress_config_t;

/* ── Tester API ──────────────────────────────────────────────── */
sig_err_t tester_init(void);
sig_err_t tester_deinit(void);

/* Loopback testing */
sig_err_t tester_run_loopback(const test_config_t *config, loopback_mode_t mode,
                               test_result_t *result);
sig_err_t tester_stop_loopback(void);

/* BER testing */
sig_err_t tester_run_ber(const ber_config_t *config, ber_result_t *result);
sig_err_t tester_stop_ber(void);
sig_err_t tester_get_ber_status(ber_result_t *result);

/* Stress testing */
sig_err_t tester_run_stress(const stress_config_t *config, test_result_t *result);
sig_err_t tester_stop_stress(void);

/* Protocol-specific tests */
sig_err_t tester_uart_loopback(uint32_t baud, uint32_t packets, test_result_t *result);
sig_err_t tester_spi_loopback(uint32_t clock, uint32_t packets, test_result_t *result);
sig_err_t tester_i2c_probe(uint8_t start_addr, uint8_t end_addr, test_result_t *result);
sig_err_t tester_can_echo(uint32_t baud, uint32_t packets, test_result_t *result);
sig_err_t tester_modbus_poll(uint8_t slave_id, uint16_t reg, uint16_t count,
                              test_result_t *result);

/* Utility */
bool tester_is_running(void);
sig_err_t tester_get_progress(float *percent);
sig_err_t tester_generate_report(char *buf, uint32_t buf_size, uint32_t *written);
