/**
 * Multi-Protocol Signal Injector & Testing Device
 * Core Configuration
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

/* ── Device Info ─────────────────────────────────────────────── */
#define DEVICE_NAME             "ESP32 Signal Injector"
#define DEVICE_VERSION          "1.0.0"
#define DEVICE_MANUFACTURER     "Open Source"

/* ── Pin Assignments (ESP32-WROOM-32) ────────────────────────── */

/* UART Channels */
#define PIN_UART1_TX            17
#define PIN_UART1_RX            16
#define PIN_UART2_TX            1
#define PIN_UART2_RX            3

/* RS-485 (via MAX485 transceiver) */
#define PIN_RS485_TX            17  /* Shared with UART1 */
#define PIN_RS485_RX            16
#define PIN_RS485_DE_RE         4   /* Driver Enable / Receiver Enable */

/* RS-232 (via MAX3232 transceiver) */
#define PIN_RS232_TX            17  /* Shared with UART1 */
#define PIN_RS232_RX            16

/* I2C Bus */
#define PIN_I2C_SDA             21
#define PIN_I2C_SCL             22

/* SPI Bus */
#define PIN_SPI_MOSI            23
#define PIN_SPI_MISO            19
#define PIN_SPI_SCLK            18
#define PIN_SPI_CS0             5

/* CAN Bus (via MCP2551 / SN65HVD230 transceiver) */
#define PIN_CAN_TX              25
#define PIN_CAN_RX              26

/* PWM Outputs */
#define PIN_PWM_CH0             27
#define PIN_PWM_CH1             14
#define PIN_PWM_CH2             12
#define PIN_PWM_CH3             13

/* GPIO / Digital I/O */
#define PIN_GPIO_0              32
#define PIN_GPIO_1              33
#define PIN_GPIO_2              34  /* Input only */
#define PIN_GPIO_3              35  /* Input only */
#define PIN_GPIO_4              36  /* Input only (VP) */
#define PIN_GPIO_5              39  /* Input only (VN) */

/* DAC Outputs (for analog signal injection) */
#define PIN_DAC1                25  /* GPIO25 / DAC1 */
#define PIN_DAC2                26  /* GPIO26 / DAC2 */

/* ADC Inputs (for signal analysis) */
#define PIN_ADC_CH0             36  /* ADC1_CH0 */
#define PIN_ADC_CH1             39  /* ADC1_CH3 */
#define PIN_ADC_CH2             34  /* ADC1_CH6 */
#define PIN_ADC_CH3             35  /* ADC1_CH7 */

/* Status LEDs */
#define PIN_LED_STATUS          2   /* Built-in LED */
#define PIN_LED_ACTIVITY        15

/* ── Protocol Defaults ───────────────────────────────────────── */
#define DEFAULT_UART_BAUD       115200
#define DEFAULT_I2C_FREQ        100000   /* 100 kHz */
#define DEFAULT_SPI_FREQ        1000000  /* 1 MHz */
#define DEFAULT_CAN_BAUD        500000   /* 500 kbps */
#define DEFAULT_PWM_FREQ        1000     /* 1 kHz */
#define DEFAULT_PWM_RESOLUTION  8        /* 8-bit (0-255) */

/* ── WiFi Configuration ──────────────────────────────────────── */
#define WIFI_AP_SSID            "SignalInjector"
#define WIFI_AP_PASS            "signal1234"
#define WIFI_AP_CHANNEL         1
#define WIFI_AP_MAX_CONN        4

/* ── Web Server ──────────────────────────────────────────────── */
#define WEB_SERVER_PORT         80
#define WS_SERVER_PORT          81

/* ── Buffer Sizes ────────────────────────────────────────────── */
#define PROTOCOL_BUF_SIZE       4096
#define CAPTURE_BUF_SIZE        8192
#define CMD_BUF_SIZE            256
#define MAX_SIGNAL_PATTERNS     32
#define MAX_FAULT_RULES         16

/* ── Task Priorities ─────────────────────────────────────────── */
#define TASK_PRIORITY_PROTOCOL  5
#define TASK_PRIORITY_INJECTION 4
#define TASK_PRIORITY_ANALYSIS  4
#define TASK_PRIORITY_FAULT     6
#define TASK_PRIORITY_UI        3
#define TASK_PRIORITY_TESTING   4

/* ── Task Stack Sizes ────────────────────────────────────────── */
#define TASK_STACK_PROTOCOL     4096
#define TASK_STACK_INJECTION    4096
#define TASK_STACK_ANALYSIS     8192
#define TASK_STACK_FAULT        4096
#define TASK_STACK_UI           8192
#define TASK_STACK_TESTING      4096

/* ── Sampling ────────────────────────────────────────────────── */
#define ADC_SAMPLE_RATE_MAX     100000  /* 100 kSps */
#define CAPTURE_DURATION_MAX_MS 10000   /* 10 seconds max */

/* ── Protocol Enumeration ────────────────────────────────────── */
typedef enum {
    PROTO_NONE = 0,
    PROTO_UART,
    PROTO_SPI,
    PROTO_I2C,
    PROTO_RS232,
    PROTO_RS485,
    PROTO_CAN,
    PROTO_LIN,
    PROTO_MODBUS_RTU,
    PROTO_MODBUS_TCP,
    PROTO_HART,
    PROTO_PWM,
    PROTO_GPIO,
    PROTO_FREQUENCY,
    PROTO_ANALOG,
    PROTO_MAX
} protocol_type_t;

/* ── Device Operating Mode ───────────────────────────────────── */
typedef enum {
    MODE_IDLE = 0,
    MODE_INJECT,
    MODE_ANALYZE,
    MODE_FAULT_INJECT,
    MODE_LOOPBACK_TEST,
    MODE_BER_TEST,
    MODE_PROTOCOL_BRIDGE,
    MODE_SIGNAL_GEN,
} device_mode_t;

/* ── Signal Waveform Types ───────────────────────────────────── */
typedef enum {
    WAVE_NONE = 0,
    WAVE_SINE,
    WAVE_SQUARE,
    WAVE_TRIANGLE,
    WAVE_SAWTOOTH,
    WAVE_NOISE,
    WAVE_PULSE,
    WAVE_CUSTOM,
} waveform_type_t;

/* ── Fault Injection Types ───────────────────────────────────── */
typedef enum {
    FAULT_NONE = 0,
    FAULT_BIT_FLIP,
    FAULT_DELAY,
    FAULT_DROP_PACKET,
    FAULT_CORRUPT_CRC,
    FAULT_GLITCH,
    FAULT_NOISE_INJECT,
    FAULT_VOLTAGE_DROP,
    FAULT_TIMING_ERROR,
    FAULT_BUS_OFF,
} fault_type_t;

/* ── Return Codes ────────────────────────────────────────────── */
typedef enum {
    SIG_OK = 0,
    SIG_ERR_INVALID_PARAM,
    SIG_ERR_NOT_INITIALIZED,
    SIG_ERR_BUSY,
    SIG_ERR_TIMEOUT,
    SIG_ERR_BUFFER_FULL,
    SIG_ERR_PROTOCOL,
    SIG_ERR_HARDWARE,
    SIG_ERR_NOT_SUPPORTED,
} sig_err_t;

/* ── Common Data Structures ──────────────────────────────────── */

typedef struct {
    uint8_t *data;
    uint16_t length;
    uint32_t timestamp_us;
    protocol_type_t protocol;
    bool is_valid;
    uint8_t channel;
} signal_packet_t;

typedef struct {
    waveform_type_t type;
    float frequency;
    float amplitude;
    float offset;
    float duty_cycle;      /* For PWM/pulse */
    float phase;
    uint32_t duration_ms;  /* 0 = continuous */
    uint8_t *custom_data;
    uint16_t custom_len;
} signal_params_t;

typedef struct {
    protocol_type_t protocol;
    uint32_t total_packets;
    uint32_t error_packets;
    uint32_t dropped_packets;
    float bit_error_rate;
    float throughput_bps;
    uint32_t min_latency_us;
    uint32_t max_latency_us;
    uint32_t avg_latency_us;
    uint32_t test_duration_ms;
} test_result_t;
