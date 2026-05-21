#pragma once
#include "esp_dashboard_plus.h"
#include "esp_err.h"
#include "esp_http_server.h"

// Start the HTTPS+WSS server and register URI handlers.
// dashboard is stored as the global user context so handlers can reach it.
esp_err_t server_start(ESPDashboardPlus* dashboard, uint16_t port,
                        httpd_handle_t* out_handle);

// Stop the server
void server_stop(httpd_handle_t handle);

// Broadcast a null-terminated JSON string to all WebSocket clients.
// Safe to call from any task — uses httpd_queue_work internally.
void server_broadcast(httpd_handle_t handle, const char* json);

// Send JSON to a single client asynchronously (via httpd_queue_work).
// Safe to call from any context including the WS upgrade handler.
void server_send_to_fd_async(httpd_handle_t handle, int fd, const char* json);

// Send JSON to a single client synchronously.
// Must be called from the server task (e.g., inside a URI handler).
void server_send_to_client(httpd_req_t* req, const char* json);
