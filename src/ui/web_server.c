/**
 * Web Server & WebSocket Interface Implementation
 * Provides browser-based control and monitoring UI
 */
#include "ui/web_server.h"
#include "core/task_manager.h"
#include "protocols/protocol_driver.h"
#include "injection/signal_injector.h"
#include "analysis/signal_analyzer.h"
#include "fault/fault_injector.h"
#include "testing/loopback_tester.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#include <string.h>
#include <stdio.h>

static const char *TAG = "web_srv";

static httpd_handle_t s_server = NULL;
static int s_ws_fd = -1;

/* ── Embedded HTML UI ────────────────────────────────────────── */
extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[]   asm("_binary_index_html_end");

/* ── HTTP Handlers ───────────────────────────────────────────── */

static esp_err_t root_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, index_html_start, index_html_end - index_html_start);
    return ESP_OK;
}

static esp_err_t api_status_handler(httpd_req_t *req)
{
    char buf[512];
    snprintf(buf, sizeof(buf),
        "{\"device\":\"%s\",\"version\":\"%s\","
        "\"mode\":%d,\"protocol\":%d,"
        "\"protocol_name\":\"%s\","
        "\"injector_running\":%s,"
        "\"analyzer_capturing\":%s,"
        "\"fault_active\":%s,"
        "\"tester_running\":%s,"
        "\"packets_sent\":%lu}",
        DEVICE_NAME, DEVICE_VERSION,
        task_manager_get_mode(),
        task_manager_get_active_protocol(),
        protocol_get_name(task_manager_get_active_protocol()),
        injector_is_running() ? "true" : "false",
        analyzer_is_capturing() ? "true" : "false",
        fault_injector_is_active() ? "true" : "false",
        tester_is_running() ? "true" : "false",
        injector_get_packets_sent());

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, strlen(buf));
    return ESP_OK;
}

static esp_err_t api_protocol_handler(httpd_req_t *req)
{
    char buf[128];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) return ESP_FAIL;
    buf[len] = '\0';

    int proto = 0;
    if (sscanf(buf, "{\"protocol\":%d}", &proto) == 1) {
        /* Deinit previous protocol */
        protocol_type_t current = task_manager_get_active_protocol();
        if (current != PROTO_NONE) {
            protocol_deinit_driver(current);
        }

        /* Init new protocol with defaults */
        sig_err_t err = protocol_init_driver((protocol_type_t)proto, NULL);
        if (err == SIG_OK) {
            task_manager_set_active_protocol((protocol_type_t)proto);
            httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
        } else {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Init failed");
        }
    } else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
    }
    return ESP_OK;
}

static esp_err_t api_inject_handler(httpd_req_t *req)
{
    char buf[512];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) return ESP_FAIL;
    buf[len] = '\0';

    /* Parse injection command */
    int action = 0;
    sscanf(buf, "{\"action\":%d", &action);

    switch (action) {
        case 0: /* Start injection */
            injector_start();
            break;
        case 1: /* Stop injection */
            injector_stop();
            break;
        case 2: /* Send test pattern */
        {
            int pattern = 0, count = 1;
            sscanf(buf, "{\"action\":2,\"pattern\":%d,\"count\":%d}", &pattern, &count);
            protocol_type_t proto = task_manager_get_active_protocol();
            switch (pattern) {
                case 0: injector_send_walking_ones(proto, 32); break;
                case 1: injector_send_prbs(proto, 15, count); break;
                case 2: injector_send_counter(proto, 0, 256); break;
            }
            break;
        }
        case 3: /* Generate waveform */
        {
            signal_params_t params = {
                .type = WAVE_SINE,
                .frequency = 1000.0f,
                .amplitude = 1.0f,
                .offset = 0.0f,
                .duty_cycle = 50.0f,
                .phase = 0.0f,
                .duration_ms = 0,
            };
            int wave_type = 0;
            float freq = 1000.0f, amp = 1.0f;
            sscanf(buf, "{\"action\":3,\"type\":%d,\"freq\":%f,\"amp\":%f}",
                   &wave_type, &freq, &amp);
            params.type = (waveform_type_t)wave_type;
            params.frequency = freq;
            params.amplitude = amp;
            injector_generate_waveform(&params);
            break;
        }
        case 4: /* Stop waveform */
            injector_stop_waveform();
            break;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

static esp_err_t api_analyze_handler(httpd_req_t *req)
{
    char buf[128];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) return ESP_FAIL;
    buf[len] = '\0';

    int action = 0;
    sscanf(buf, "{\"action\":%d}", &action);

    switch (action) {
        case 0: /* Start capture */
        {
            uint32_t duration = 5000;
            sscanf(buf, "{\"action\":0,\"duration\":%lu}", &duration);
            analyzer_start_capture(task_manager_get_active_protocol(), duration);
            break;
        }
        case 1: /* Stop capture */
            analyzer_stop_capture();
            break;
        case 2: /* Get data */
        {
            char *json = malloc(4096);
            if (json) {
                uint32_t written;
                analyzer_export_json(json, 4096, &written);
                httpd_resp_set_type(req, "application/json");
                httpd_resp_send(req, json, written);
                free(json);
                return ESP_OK;
            }
            break;
        }
        case 3: /* Scan I2C */
        {
            uint8_t addrs[127];
            uint8_t count = 0;
            analyzer_scan_i2c(addrs, &count, 127);

            char resp[512] = "{\"devices\":[";
            for (int i = 0; i < count; i++) {
                char addr_str[8];
                snprintf(addr_str, sizeof(addr_str), "%s\"0x%02X\"", i > 0 ? "," : "", addrs[i]);
                strncat(resp, addr_str, sizeof(resp) - strlen(resp) - 1);
            }
            strncat(resp, "]}", sizeof(resp) - strlen(resp) - 1);
            httpd_resp_set_type(req, "application/json");
            httpd_resp_sendstr(req, resp);
            return ESP_OK;
        }
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

static esp_err_t api_fault_handler(httpd_req_t *req)
{
    char buf[256];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) return ESP_FAIL;
    buf[len] = '\0';

    int action = 0;
    sscanf(buf, "{\"action\":%d}", &action);

    switch (action) {
        case 0: /* Start fault injection */
            fault_injector_start();
            break;
        case 1: /* Stop fault injection */
            fault_injector_stop();
            break;
        case 2: /* Add bit flip fault */
        {
            float prob = 0.01f;
            sscanf(buf, "{\"action\":2,\"prob\":%f}", &prob);
            uint8_t id;
            fault_create_bit_flip(task_manager_get_active_protocol(), prob, &id);
            break;
        }
        case 3: /* Add delay fault */
        {
            uint32_t delay = 1000;
            sscanf(buf, "{\"action\":3,\"delay\":%lu}", &delay);
            uint8_t id;
            fault_create_delay(task_manager_get_active_protocol(), delay, &id);
            break;
        }
        case 4: /* Add drop fault */
        {
            float rate = 0.05f;
            sscanf(buf, "{\"action\":4,\"rate\":%f}", &rate);
            uint8_t id;
            fault_create_drop(task_manager_get_active_protocol(), rate, &id);
            break;
        }
        case 5: /* Get stats */
        {
            fault_stats_t stats;
            fault_get_stats(&stats);
            char resp[256];
            snprintf(resp, sizeof(resp),
                     "{\"total\":%lu,\"bit_flips\":%lu,\"delays\":%lu,"
                     "\"drops\":%lu,\"crc\":%lu,\"glitches\":%lu,\"noise\":%lu}",
                     stats.total_faults_injected, stats.bit_flips,
                     stats.delays_added, stats.packets_dropped,
                     stats.crc_corruptions, stats.glitches, stats.noise_injections);
            httpd_resp_set_type(req, "application/json");
            httpd_resp_sendstr(req, resp);
            return ESP_OK;
        }
        case 6: /* Clear rules */
            fault_clear_rules();
            fault_reset_stats();
            break;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

static esp_err_t api_test_handler(httpd_req_t *req)
{
    char buf[256];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) return ESP_FAIL;
    buf[len] = '\0';

    int action = 0;
    sscanf(buf, "{\"action\":%d}", &action);

    static test_result_t result;
    static ber_result_t ber_result;

    switch (action) {
        case 0: /* Run loopback test */
        {
            uint32_t packets = 100;
            uint16_t size = 32;
            sscanf(buf, "{\"action\":0,\"packets\":%lu,\"size\":%hu}", &packets, &size);

            test_config_t cfg = {
                .protocol = task_manager_get_active_protocol(),
                .packet_count = packets, .packet_size = size,
                .interval_ms = 10, .timeout_ms = 500,
                .verify_data = true, .measure_latency = true,
                .test_pattern = 0,
            };
            tester_run_loopback(&cfg, LOOPBACK_EXTERNAL, &result);

            char resp[512];
            uint32_t written;
            tester_generate_report(resp, sizeof(resp), &written);
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(req, resp, written);
            return ESP_OK;
        }
        case 1: /* Run BER test */
        {
            uint32_t duration = 10;
            sscanf(buf, "{\"action\":1,\"duration\":%lu}", &duration);

            ber_config_t cfg = {
                .protocol = task_manager_get_active_protocol(),
                .total_bits = 0, .duration_sec = duration,
                .prbs_order = 15, .real_time_display = true,
            };
            tester_run_ber(&cfg, &ber_result);

            char resp[256];
            snprintf(resp, sizeof(resp),
                     "{\"bits_sent\":%llu,\"bits_received\":%llu,"
                     "\"errors\":%llu,\"ber\":%.2e,\"elapsed\":%.1f}",
                     ber_result.total_bits_sent, ber_result.total_bits_received,
                     ber_result.bit_errors, ber_result.bit_error_rate,
                     ber_result.elapsed_seconds);
            httpd_resp_set_type(req, "application/json");
            httpd_resp_sendstr(req, resp);
            return ESP_OK;
        }
        case 2: /* Stop test */
            tester_stop_loopback();
            tester_stop_ber();
            break;
        case 3: /* Get progress */
        {
            float progress;
            tester_get_progress(&progress);
            char resp[64];
            snprintf(resp, sizeof(resp), "{\"progress\":%.1f,\"running\":%s}",
                     progress, tester_is_running() ? "true" : "false");
            httpd_resp_set_type(req, "application/json");
            httpd_resp_sendstr(req, resp);
            return ESP_OK;
        }
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

/* ── WiFi AP Setup ───────────────────────────────────────────── */

sig_err_t wifi_ap_init(const char *ssid, const char *password)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.ap.ssid, ssid, sizeof(wifi_config.ap.ssid) - 1);
    strncpy((char *)wifi_config.ap.password, password, sizeof(wifi_config.ap.password) - 1);
    wifi_config.ap.ssid_len = strlen(ssid);
    wifi_config.ap.channel = WIFI_AP_CHANNEL;
    wifi_config.ap.max_connection = WIFI_AP_MAX_CONN;
    wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;

    if (strlen(password) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    esp_wifi_start();

    ESP_LOGI(TAG, "WiFi AP started: SSID=%s, Channel=%d", ssid, WIFI_AP_CHANNEL);
    return SIG_OK;
}

sig_err_t wifi_ap_get_ip(char *ip_buf, uint8_t buf_len)
{
    esp_netif_ip_info_t ip_info;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
        snprintf(ip_buf, buf_len, IPSTR, IP2STR(&ip_info.ip));
        return SIG_OK;
    }
    return SIG_ERR_HARDWARE;
}

uint8_t wifi_ap_get_client_count(void)
{
    wifi_sta_list_t sta_list;
    esp_wifi_ap_get_sta_list(&sta_list);
    return sta_list.num;
}

/* ── Web Server Setup ────────────────────────────────────────── */

sig_err_t web_server_init(void)
{
    return SIG_OK;
}

sig_err_t web_server_deinit(void)
{
    return web_server_stop();
}

sig_err_t web_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 16;
    config.stack_size = TASK_STACK_UI;

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start server: %s", esp_err_to_name(err));
        return SIG_ERR_HARDWARE;
    }

    /* Register URI handlers */
    httpd_uri_t uri_root = {
        .uri = "/", .method = HTTP_GET, .handler = root_handler,
    };
    httpd_uri_t uri_status = {
        .uri = "/api/status", .method = HTTP_GET, .handler = api_status_handler,
    };
    httpd_uri_t uri_protocol = {
        .uri = "/api/protocol", .method = HTTP_POST, .handler = api_protocol_handler,
    };
    httpd_uri_t uri_inject = {
        .uri = "/api/inject", .method = HTTP_POST, .handler = api_inject_handler,
    };
    httpd_uri_t uri_analyze = {
        .uri = "/api/analyze", .method = HTTP_POST, .handler = api_analyze_handler,
    };
    httpd_uri_t uri_fault = {
        .uri = "/api/fault", .method = HTTP_POST, .handler = api_fault_handler,
    };
    httpd_uri_t uri_test = {
        .uri = "/api/test", .method = HTTP_POST, .handler = api_test_handler,
    };

    httpd_register_uri_handler(s_server, &uri_root);
    httpd_register_uri_handler(s_server, &uri_status);
    httpd_register_uri_handler(s_server, &uri_protocol);
    httpd_register_uri_handler(s_server, &uri_inject);
    httpd_register_uri_handler(s_server, &uri_analyze);
    httpd_register_uri_handler(s_server, &uri_fault);
    httpd_register_uri_handler(s_server, &uri_test);

    char ip[16];
    wifi_ap_get_ip(ip, sizeof(ip));
    ESP_LOGI(TAG, "Web server started at http://%s:%d", ip, WEB_SERVER_PORT);

    return SIG_OK;
}

sig_err_t web_server_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
    return SIG_OK;
}

sig_err_t ws_broadcast(const char *data, uint16_t len)
{
    if (s_ws_fd < 0 || !s_server) return SIG_ERR_NOT_INITIALIZED;

    httpd_ws_frame_t ws_pkt = {
        .payload = (uint8_t *)data,
        .len = len,
        .type = HTTPD_WS_TYPE_TEXT,
    };
    httpd_ws_send_frame_async(s_server, s_ws_fd, &ws_pkt);
    return SIG_OK;
}

sig_err_t ws_send_json_status(void)
{
    char buf[256];
    snprintf(buf, sizeof(buf),
             "{\"type\":\"status\",\"mode\":%d,\"proto\":\"%s\",\"pkts\":%lu}",
             task_manager_get_mode(),
             protocol_get_name(task_manager_get_active_protocol()),
             injector_get_packets_sent());
    return ws_broadcast(buf, strlen(buf));
}
