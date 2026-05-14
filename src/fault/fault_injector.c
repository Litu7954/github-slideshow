/**
 * Fault Injection Module Implementation
 * Introduces controlled faults for robustness testing
 */
#include "fault/fault_injector.h"
#include "core/task_manager.h"
#include "esp_random.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "fault_inj";

static fault_rule_t s_rules[MAX_FAULT_RULES];
static uint8_t s_rule_count = 0;
static bool s_active = false;
static fault_stats_t s_stats;
static TaskHandle_t s_fault_task = NULL;

static float random_float(void)
{
    return (float)(esp_random() & 0xFFFF) / 65535.0f;
}

/* ── Fault Application Functions ─────────────────────────────── */

static void apply_bit_flip(uint8_t *data, uint16_t len, uint8_t bit_pos)
{
    if (bit_pos / 8 < len) {
        data[bit_pos / 8] ^= (1 << (bit_pos % 8));
        s_stats.bit_flips++;
    }
}

static void apply_delay(uint32_t delay_us)
{
    esp_rom_delay_us(delay_us);
    s_stats.delays_added++;
}

static void apply_corruption(uint8_t *data, uint16_t len)
{
    if (len >= 2) {
        /* Corrupt last 2 bytes (typically CRC) */
        data[len - 1] ^= 0xFF;
        data[len - 2] ^= 0xFF;
        s_stats.crc_corruptions++;
    }
}

static void apply_noise(uint8_t *data, uint16_t len, uint8_t level)
{
    for (uint16_t i = 0; i < len; i++) {
        if (random_float() < level / 255.0f) {
            data[i] ^= (uint8_t)(esp_random() & 0xFF);
        }
    }
    s_stats.noise_injections++;
}

/* ── Periodic Fault Task ─────────────────────────────────────── */

static void fault_task(void *pvParameter)
{
    ESP_LOGI(TAG, "Fault injection task started");

    while (s_active) {
        for (uint8_t i = 0; i < s_rule_count; i++) {
            fault_rule_t *rule = &s_rules[i];
            if (!rule->enabled) continue;
            if (rule->max_triggers > 0 && rule->trigger_count >= rule->max_triggers) continue;

            if (rule->type == FAULT_GLITCH && random_float() < rule->probability) {
                /* Generate a glitch on a GPIO */
                extern sig_err_t gpio_pulse(uint8_t pin, uint32_t duration_us);
                gpio_pulse(PIN_GPIO_0, rule->params.glitch.glitch_us);
                rule->trigger_count++;
                s_stats.glitches++;
                s_stats.total_faults_injected++;
            }

            if (rule->interval_ms > 0) {
                vTaskDelay(pdMS_TO_TICKS(rule->interval_ms));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    s_fault_task = NULL;
    vTaskDelete(NULL);
}

/* ── Public API ──────────────────────────────────────────────── */

sig_err_t fault_injector_init(void)
{
    memset(s_rules, 0, sizeof(s_rules));
    memset(&s_stats, 0, sizeof(s_stats));
    s_rule_count = 0;
    s_active = false;
    ESP_LOGI(TAG, "Fault injector initialized");
    return SIG_OK;
}

sig_err_t fault_injector_deinit(void)
{
    fault_injector_stop();
    return SIG_OK;
}

sig_err_t fault_add_rule(const fault_rule_t *rule, uint8_t *id)
{
    if (s_rule_count >= MAX_FAULT_RULES) return SIG_ERR_BUFFER_FULL;
    memcpy(&s_rules[s_rule_count], rule, sizeof(fault_rule_t));
    s_rules[s_rule_count].id = s_rule_count;
    *id = s_rule_count;
    s_rule_count++;
    return SIG_OK;
}

sig_err_t fault_remove_rule(uint8_t id)
{
    if (id >= s_rule_count) return SIG_ERR_INVALID_PARAM;
    s_rules[id].enabled = false;
    return SIG_OK;
}

sig_err_t fault_enable_rule(uint8_t id, bool enable)
{
    if (id >= s_rule_count) return SIG_ERR_INVALID_PARAM;
    s_rules[id].enabled = enable;
    return SIG_OK;
}

sig_err_t fault_clear_rules(void)
{
    memset(s_rules, 0, sizeof(s_rules));
    s_rule_count = 0;
    return SIG_OK;
}

sig_err_t fault_injector_start(void)
{
    if (s_active) return SIG_ERR_BUSY;
    s_active = true;
    xTaskCreate(fault_task, "fault", TASK_STACK_FAULT,
                NULL, TASK_PRIORITY_FAULT, &s_fault_task);
    return SIG_OK;
}

sig_err_t fault_injector_stop(void)
{
    s_active = false;
    if (s_fault_task) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return SIG_OK;
}

bool fault_injector_is_active(void)
{
    return s_active;
}

sig_err_t fault_process_packet(uint8_t *data, uint16_t *len, protocol_type_t proto)
{
    if (!s_active) return SIG_OK;

    for (uint8_t i = 0; i < s_rule_count; i++) {
        fault_rule_t *rule = &s_rules[i];
        if (!rule->enabled) continue;
        if (rule->target_protocol != PROTO_NONE && rule->target_protocol != proto) continue;
        if (rule->max_triggers > 0 && rule->trigger_count >= rule->max_triggers) continue;

        if (random_float() > rule->probability) continue;

        switch (rule->type) {
            case FAULT_BIT_FLIP:
                apply_bit_flip(data, *len, rule->params.bit_flip.bit_pos);
                break;
            case FAULT_DELAY:
                apply_delay(rule->params.delay.delay_us);
                break;
            case FAULT_DROP_PACKET:
                *len = 0;  /* Signal to caller to drop this packet */
                s_stats.packets_dropped++;
                break;
            case FAULT_CORRUPT_CRC:
                apply_corruption(data, *len);
                break;
            case FAULT_NOISE_INJECT:
                apply_noise(data, *len, rule->params.noise.noise_level);
                break;
            case FAULT_TIMING_ERROR:
                apply_delay(rule->params.timing.offset_us > 0 ?
                           (uint32_t)rule->params.timing.offset_us : 0);
                s_stats.timing_errors++;
                break;
            default:
                break;
        }

        rule->trigger_count++;
        s_stats.total_faults_injected++;
    }
    return SIG_OK;
}

sig_err_t fault_get_stats(fault_stats_t *stats)
{
    if (!stats) return SIG_ERR_INVALID_PARAM;
    memcpy(stats, &s_stats, sizeof(fault_stats_t));
    return SIG_OK;
}

sig_err_t fault_reset_stats(void)
{
    memset(&s_stats, 0, sizeof(s_stats));
    return SIG_OK;
}

/* ── Convenience Fault Creators ──────────────────────────────── */

sig_err_t fault_create_bit_flip(protocol_type_t proto, float probability, uint8_t *id)
{
    fault_rule_t rule = {
        .type = FAULT_BIT_FLIP,
        .target_protocol = proto,
        .enabled = true,
        .probability = probability,
        .params.bit_flip.bit_pos = (uint8_t)(esp_random() % 64),
    };
    return fault_add_rule(&rule, id);
}

sig_err_t fault_create_delay(protocol_type_t proto, uint32_t delay_us, uint8_t *id)
{
    fault_rule_t rule = {
        .type = FAULT_DELAY,
        .target_protocol = proto,
        .enabled = true,
        .probability = 1.0f,
        .params.delay.delay_us = delay_us,
    };
    return fault_add_rule(&rule, id);
}

sig_err_t fault_create_drop(protocol_type_t proto, float drop_rate, uint8_t *id)
{
    fault_rule_t rule = {
        .type = FAULT_DROP_PACKET,
        .target_protocol = proto,
        .enabled = true,
        .probability = drop_rate,
    };
    return fault_add_rule(&rule, id);
}

sig_err_t fault_create_noise(protocol_type_t proto, uint8_t level, uint8_t *id)
{
    fault_rule_t rule = {
        .type = FAULT_NOISE_INJECT,
        .target_protocol = proto,
        .enabled = true,
        .probability = 1.0f,
        .params.noise.noise_level = level,
    };
    return fault_add_rule(&rule, id);
}

sig_err_t fault_create_glitch(protocol_type_t proto, uint32_t glitch_us, uint8_t *id)
{
    fault_rule_t rule = {
        .type = FAULT_GLITCH,
        .target_protocol = proto,
        .enabled = true,
        .probability = 1.0f,
        .params.glitch.glitch_us = glitch_us,
        .params.glitch.level = 1,
    };
    return fault_add_rule(&rule, id);
}
