/**
 * Protocol Driver Registry
 * Central registry for all protocol drivers
 */
#include "protocols/protocol_driver.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "proto_reg";

/* External driver declarations */
extern protocol_driver_t uart_driver;
extern protocol_driver_t rs232_driver;
extern protocol_driver_t rs485_driver;
extern protocol_driver_t spi_driver;
extern protocol_driver_t i2c_driver;
extern protocol_driver_t can_driver;
extern protocol_driver_t modbus_rtu_driver;
extern protocol_driver_t pwm_driver;

static protocol_driver_t *s_drivers[PROTO_MAX] = {0};

static const char *s_proto_names[] = {
    [PROTO_NONE]        = "None",
    [PROTO_UART]        = "UART",
    [PROTO_SPI]         = "SPI",
    [PROTO_I2C]         = "I2C",
    [PROTO_RS232]       = "RS-232",
    [PROTO_RS485]       = "RS-485",
    [PROTO_CAN]         = "CAN",
    [PROTO_LIN]         = "LIN",
    [PROTO_MODBUS_RTU]  = "Modbus RTU",
    [PROTO_MODBUS_TCP]  = "Modbus TCP",
    [PROTO_HART]        = "HART",
    [PROTO_PWM]         = "PWM",
    [PROTO_GPIO]        = "GPIO",
    [PROTO_FREQUENCY]   = "Frequency",
    [PROTO_ANALOG]      = "Analog",
};

sig_err_t protocol_registry_init(void)
{
    memset(s_drivers, 0, sizeof(s_drivers));

    s_drivers[PROTO_UART]       = &uart_driver;
    s_drivers[PROTO_RS232]      = &rs232_driver;
    s_drivers[PROTO_RS485]      = &rs485_driver;
    s_drivers[PROTO_SPI]        = &spi_driver;
    s_drivers[PROTO_I2C]        = &i2c_driver;
    s_drivers[PROTO_CAN]        = &can_driver;
    s_drivers[PROTO_MODBUS_RTU] = &modbus_rtu_driver;
    s_drivers[PROTO_PWM]        = &pwm_driver;

    ESP_LOGI(TAG, "Protocol registry initialized with %d drivers", 8);
    return SIG_OK;
}

protocol_driver_t *protocol_get_driver(protocol_type_t type)
{
    if (type >= PROTO_MAX) return NULL;
    return s_drivers[type];
}

const char *protocol_get_name(protocol_type_t type)
{
    if (type >= PROTO_MAX) return "Unknown";
    return s_proto_names[type];
}

sig_err_t protocol_init_driver(protocol_type_t type, void *config)
{
    protocol_driver_t *drv = protocol_get_driver(type);
    if (!drv) return SIG_ERR_NOT_SUPPORTED;
    if (!drv->init) return SIG_ERR_NOT_SUPPORTED;

    ESP_LOGI(TAG, "Initializing %s driver", drv->name);
    return drv->init(config);
}

sig_err_t protocol_deinit_driver(protocol_type_t type)
{
    protocol_driver_t *drv = protocol_get_driver(type);
    if (!drv) return SIG_ERR_NOT_SUPPORTED;
    if (!drv->deinit) return SIG_ERR_NOT_SUPPORTED;

    ESP_LOGI(TAG, "Deinitializing %s driver", drv->name);
    return drv->deinit();
}
