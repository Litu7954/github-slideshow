/**
 * Web Server & WebSocket Interface
 * Provides browser-based control and monitoring UI
 */
#pragma once

#include "core/config.h"

/* ── Web Server API ──────────────────────────────────────────── */
sig_err_t web_server_init(void);
sig_err_t web_server_deinit(void);
sig_err_t web_server_start(void);
sig_err_t web_server_stop(void);

/* WiFi AP management */
sig_err_t wifi_ap_init(const char *ssid, const char *password);
sig_err_t wifi_ap_get_ip(char *ip_buf, uint8_t buf_len);
uint8_t wifi_ap_get_client_count(void);

/* WebSocket */
sig_err_t ws_broadcast(const char *data, uint16_t len);
sig_err_t ws_send_json_status(void);
sig_err_t ws_send_capture_data(const uint8_t *data, uint16_t len);
sig_err_t ws_send_test_result(const char *result_json);
