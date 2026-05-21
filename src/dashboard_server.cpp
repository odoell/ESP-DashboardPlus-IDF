#include "dashboard_server.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"

#if CONFIG_DASHBOARD_USE_HTTP
#  include "esp_http_server.h"
#else
#  include "esp_https_server.h"
extern const uint8_t _binary_server_crt_start[] asm("_binary_server_crt_start");
extern const uint8_t _binary_server_crt_end[]   asm("_binary_server_crt_end");
extern const uint8_t _binary_server_key_start[] asm("_binary_server_key_start");
extern const uint8_t _binary_server_key_end[]   asm("_binary_server_key_end");
#endif

static const char* TAG = "dashboard_srv";

// ─── HTML handler ────────────────────────────────────────────────────────────

static esp_err_t html_get_handler(httpd_req_t* req) {
    auto* dashboard = static_cast<ESPDashboardPlus*>(
        httpd_get_global_user_ctx(req->handle));
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Cache-Control",    "max-age=86400");
    httpd_resp_send(req,
        reinterpret_cast<const char*>(dashboard->_htmlData),
        static_cast<ssize_t>(dashboard->_htmlSize));
    return ESP_OK;
}

// ─── WebSocket handler ───────────────────────────────────────────────────────

static esp_err_t ws_handler(httpd_req_t* req) {
    auto* dashboard = static_cast<ESPDashboardPlus*>(
        httpd_get_global_user_ctx(req->handle));

    if (req->method == HTTP_GET) {
        int fd = httpd_req_to_sockfd(req);
        ESP_LOGI(TAG, "WS client connected, fd=%d", fd);
        // Defer init so the 101 handshake completes before we send frames.
        // _onClientConnect(req) would call httpd_ws_send_frame synchronously
        // while the upgrade is still in-flight; the frame gets dropped.
        // Instead build the JSON now and queue an async send.
        dashboard->_queueInitForFd(req->handle, fd);
        return ESP_OK;
    }

    httpd_ws_frame_t pkt = {};
    pkt.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_recv_frame(req, &pkt, 0);
    if (ret != ESP_OK) return ret;
    if (pkt.len == 0) return ESP_OK;

    auto* buf = static_cast<uint8_t*>(malloc(pkt.len + 1));
    if (!buf) return ESP_ERR_NO_MEM;

    pkt.payload = buf;
    ret = httpd_ws_recv_frame(req, &pkt, pkt.len);
    if (ret == ESP_OK) {
        buf[pkt.len] = '\0';
        dashboard->_handleWebSocketMessage(reinterpret_cast<char*>(buf), pkt.len);
    }

    free(buf);
    return ret;
}

// ─── Broadcast work function ─────────────────────────────────────────────────

struct BroadcastArg {
    httpd_handle_t hd;
    char* json;
};

static void broadcast_work_fn(void* arg) {
    auto* ba = static_cast<BroadcastArg*>(arg);

    httpd_ws_frame_t pkt = {};
    pkt.final      = true;
    pkt.fragmented = false;
    pkt.type       = HTTPD_WS_TYPE_TEXT;
    pkt.payload    = reinterpret_cast<uint8_t*>(ba->json);
    pkt.len        = strlen(ba->json);

    size_t max_fds    = CONFIG_DASHBOARD_MAX_CLIENTS + 2;
    size_t client_cnt = max_fds;
    int client_fds[CONFIG_DASHBOARD_MAX_CLIENTS + 2];

    if (httpd_get_client_list(ba->hd, &client_cnt, client_fds) == ESP_OK) {
        for (size_t i = 0; i < client_cnt; i++) {
            if (httpd_ws_get_fd_info(ba->hd, client_fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
                esp_err_t err = httpd_ws_send_frame_async(ba->hd, client_fds[i], &pkt);
                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "send_frame_async fd=%d: %s",
                             client_fds[i], esp_err_to_name(err));
                }
            }
        }
    }

    free(ba->json);
    free(ba);
}

// ─── Per-client async send ───────────────────────────────────────────────────

struct SendToFdArg {
    httpd_handle_t hd;
    int            fd;
    char*          json;
};

static void send_to_fd_work_fn(void* arg) {
    auto* a = static_cast<SendToFdArg*>(arg);
    httpd_ws_frame_t pkt = {};
    pkt.final      = true;
    pkt.fragmented = false;
    pkt.type       = HTTPD_WS_TYPE_TEXT;
    pkt.payload    = reinterpret_cast<uint8_t*>(a->json);
    pkt.len        = strlen(a->json);
    esp_err_t err  = httpd_ws_send_frame_async(a->hd, a->fd, &pkt);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "send_to_fd fd=%d: %s", a->fd, esp_err_to_name(err));
    }
    free(a->json);
    free(a);
}

// ─── URI table (shared between HTTP and HTTPS modes) ─────────────────────────

static const httpd_uri_t s_html_uri = {
    .uri                      = "/",
    .method                   = HTTP_GET,
    .handler                  = html_get_handler,
    .user_ctx                 = nullptr,
    .is_websocket             = false,
    .handle_ws_control_frames = false,
    .supported_subprotocol    = nullptr,
};

static const httpd_uri_t s_ws_uri = {
    .uri                      = "/ws",
    .method                   = HTTP_GET,
    .handler                  = ws_handler,
    .user_ctx                 = nullptr,
    .is_websocket             = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol    = nullptr,
};

// ─── Public API ──────────────────────────────────────────────────────────────

esp_err_t server_start(ESPDashboardPlus* dashboard, uint16_t port,
                        httpd_handle_t* out_handle) {
#if CONFIG_DASHBOARD_USE_HTTP
    httpd_config_t conf        = HTTPD_DEFAULT_CONFIG();
    conf.server_port           = port;
    conf.max_open_sockets      = CONFIG_DASHBOARD_MAX_CLIENTS + 2;
    conf.global_user_ctx       = dashboard;
    conf.global_user_ctx_free_fn = nullptr;
    conf.lru_purge_enable      = true;

    ESP_RETURN_ON_ERROR(httpd_start(out_handle, &conf),
                        TAG, "httpd_start failed");

    ESP_LOGI(TAG, "HTTP+WS server started on port %u", port);
#else
    httpd_ssl_config_t conf = HTTPD_SSL_CONFIG_DEFAULT();

    conf.servercert     = _binary_server_crt_start;
    conf.servercert_len = static_cast<size_t>(
        _binary_server_crt_end - _binary_server_crt_start);
    conf.prvtkey_pem    = _binary_server_key_start;
    conf.prvtkey_len    = static_cast<size_t>(
        _binary_server_key_end - _binary_server_key_start);

    conf.httpd.server_port           = port;
    conf.httpd.max_open_sockets      = CONFIG_DASHBOARD_MAX_CLIENTS + 2;
    conf.httpd.global_user_ctx       = dashboard;
    conf.httpd.global_user_ctx_free_fn = nullptr;
    conf.httpd.lru_purge_enable      = true;

    ESP_RETURN_ON_ERROR(httpd_ssl_start(out_handle, &conf),
                        TAG, "httpd_ssl_start failed");

    ESP_LOGI(TAG, "HTTPS+WSS server started on port %u", port);
#endif

    httpd_register_uri_handler(*out_handle, &s_html_uri);
    httpd_register_uri_handler(*out_handle, &s_ws_uri);
    return ESP_OK;
}

void server_stop(httpd_handle_t handle) {
    if (!handle) return;
#if CONFIG_DASHBOARD_USE_HTTP
    httpd_stop(handle);
#else
    httpd_ssl_stop(handle);
#endif
}

void server_broadcast(httpd_handle_t handle, const char* json) {
    if (!handle || !json) return;

    auto* ba = static_cast<BroadcastArg*>(malloc(sizeof(BroadcastArg)));
    if (!ba) return;
    ba->hd   = handle;
    ba->json = strdup(json);
    if (!ba->json) { free(ba); return; }

    esp_err_t err = httpd_queue_work(handle, broadcast_work_fn, ba);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "httpd_queue_work failed: %s", esp_err_to_name(err));
        free(ba->json);
        free(ba);
    }
}

void server_send_to_fd_async(httpd_handle_t handle, int fd, const char* json) {
    if (!handle || fd < 0 || !json) return;
    auto* a = static_cast<SendToFdArg*>(malloc(sizeof(SendToFdArg)));
    if (!a) return;
    a->hd   = handle;
    a->fd   = fd;
    a->json = strdup(json);
    if (!a->json) { free(a); return; }
    esp_err_t err = httpd_queue_work(handle, send_to_fd_work_fn, a);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "httpd_queue_work (init) failed: %s", esp_err_to_name(err));
        free(a->json);
        free(a);
    }
}

void server_send_to_client(httpd_req_t* req, const char* json) {
    httpd_ws_frame_t pkt = {};
    pkt.final   = true;
    pkt.type    = HTTPD_WS_TYPE_TEXT;
    pkt.payload = reinterpret_cast<uint8_t*>(const_cast<char*>(json));
    pkt.len     = strlen(json);
    httpd_ws_send_frame(req, &pkt);
}
