/**
 * Protocol Driver Interface
 * Common interface for all protocol implementations
 */
#pragma once

#include "core/config.h"

/* ── Protocol Driver Interface ───────────────────────────────── */
typedef struct protocol_driver {
    protocol_type_t type;
    const char *name;

    sig_err_t (*init)(void *config);
    sig_err_t (*deinit)(void);
    sig_err_t (*send)(const uint8_t *data, uint16_t len);
    sig_err_t (*receive)(uint8_t *buf, uint16_t *len, uint32_t timeout_ms);
    sig_err_t (*configure)(void *config);
    bool      (*is_ready)(void);
    sig_err_t (*get_status)(void *status);
} protocol_driver_t;

/* ── UART Configuration ──────────────────────────────────────── */
typedef struct {
    uint32_t baud_rate;
    uint8_t  data_bits;    /* 5, 6, 7, 8 */
    uint8_t  stop_bits;    /* 1, 2 */
    uint8_t  parity;       /* 0=none, 1=odd, 2=even */
    bool     flow_control; /* RTS/CTS */
    uint8_t  uart_num;     /* 0, 1, 2 */
} uart_config_params_t;

/* ── SPI Configuration ───────────────────────────────────────── */
typedef struct {
    uint32_t clock_hz;
    uint8_t  mode;         /* 0-3 (CPOL/CPHA) */
    uint8_t  bit_order;    /* 0=MSB first, 1=LSB first */
    int      cs_pin;
    bool     full_duplex;
} spi_config_params_t;

/* ── I2C Configuration ───────────────────────────────────────── */
typedef struct {
    uint32_t clock_hz;
    uint8_t  address;      /* 7-bit device address */
    bool     is_master;
    bool     addr_10bit;
} i2c_config_params_t;

/* ── CAN Configuration ───────────────────────────────────────── */
typedef struct {
    uint32_t baud_rate;    /* 125k, 250k, 500k, 1M */
    bool     listen_only;
    uint32_t filter_id;
    uint32_t filter_mask;
    bool     extended_frame;
} can_config_params_t;

/* ── RS-485 Configuration ────────────────────────────────────── */
typedef struct {
    uint32_t baud_rate;
    uint8_t  data_bits;
    uint8_t  stop_bits;
    uint8_t  parity;
    uint8_t  slave_addr;   /* Modbus slave address */
} rs485_config_params_t;

/* ── PWM Configuration ───────────────────────────────────────── */
typedef struct {
    uint32_t frequency;
    uint8_t  resolution;   /* bits: 1-16 */
    uint8_t  channel;      /* LEDC channel 0-7 */
    float    duty_cycle;   /* 0.0 - 100.0 */
    int      gpio_pin;
} pwm_config_params_t;

/* ── Modbus Configuration ────────────────────────────────────── */
typedef struct {
    uint32_t baud_rate;
    uint8_t  slave_id;
    bool     is_master;
    uint8_t  parity;
    bool     is_tcp;       /* RTU vs TCP */
    uint16_t tcp_port;
} modbus_config_params_t;

/* ── Protocol Driver Registry ────────────────────────────────── */
sig_err_t protocol_registry_init(void);
protocol_driver_t *protocol_get_driver(protocol_type_t type);
const char *protocol_get_name(protocol_type_t type);
sig_err_t protocol_init_driver(protocol_type_t type, void *config);
sig_err_t protocol_deinit_driver(protocol_type_t type);
