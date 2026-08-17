#include "dashboard_server.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"

#ifndef CONFIG_DASHBOARD_MAX_WS_FRAME_LEN
#define CONFIG_DASHBOARD_MAX_WS_FRAME_LEN 4096
#endif

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
static constexpr size_t MAX_HTTPD_CLIENT_SOCKETS = 7;

static void close_session(httpd_handle_t handle, int fd);

// ─── HTML handler ────────────────────────────────────────────────────────────

static esp_err_t html_get_handler(httpd_req_t* req) {
    auto* dashboard = static_cast<ESPDashboardPlus*>(
        httpd_get_global_user_ctx(req->handle));
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Cache-Control",    "no-cache");
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
        return ESP_OK;
    }

    httpd_ws_frame_t pkt = {};
    pkt.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_recv_frame(req, &pkt, 0);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "WS frame header receive fd=%d: %s",
                 httpd_req_to_sockfd(req), esp_err_to_name(ret));
        close_session(req->handle, httpd_req_to_sockfd(req));
        return ret;
    }
    if (pkt.len == 0) return ESP_OK;
    if (pkt.len > CONFIG_DASHBOARD_MAX_WS_FRAME_LEN) {
        ESP_LOGW(TAG, "Rejecting oversized WS frame: %u bytes (max %u)",
                 static_cast<unsigned>(pkt.len),
                 static_cast<unsigned>(CONFIG_DASHBOARD_MAX_WS_FRAME_LEN));
        close_session(req->handle, httpd_req_to_sockfd(req));
        return ESP_ERR_INVALID_SIZE;
    }

    auto* buf = static_cast<uint8_t*>(malloc(pkt.len + 1));
    if (!buf) return ESP_ERR_NO_MEM;

    pkt.payload = buf;
    ret = httpd_ws_recv_frame(req, &pkt, pkt.len);
    if (ret == ESP_OK) {
        buf[pkt.len] = '\0';
        dashboard->_handleWebSocketMessage(reinterpret_cast<char*>(buf), pkt.len,
                                           req->handle,
                                           httpd_req_to_sockfd(req));
    }

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "WS frame payload receive fd=%d: %s",
                 httpd_req_to_sockfd(req), esp_err_to_name(ret));
        close_session(req->handle, httpd_req_to_sockfd(req));
    }

    free(buf);
    return ret;
}

// ─── Broadcast work function ─────────────────────────────────────────────────

struct BroadcastArg {
    httpd_handle_t hd;
    char* json;
};

static void close_session(httpd_handle_t handle, int fd) {
    esp_err_t err = httpd_sess_trigger_close(handle, fd);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "session close fd=%d: %s", fd, esp_err_to_name(err));
    }
}

static void broadcast_work_fn(void* arg) {
    auto* ba = static_cast<BroadcastArg*>(arg);

    httpd_ws_frame_t pkt = {};
    pkt.final      = true;
    pkt.fragmented = false;
    pkt.type       = HTTPD_WS_TYPE_TEXT;
    pkt.payload    = reinterpret_cast<uint8_t*>(ba->json);
    pkt.len        = strlen(ba->json);

    size_t client_cnt = MAX_HTTPD_CLIENT_SOCKETS;
    int client_fds[MAX_HTTPD_CLIENT_SOCKETS];

    if (httpd_get_client_list(ba->hd, &client_cnt, client_fds) == ESP_OK) {
        size_t websocket_clients = 0;
        for (size_t i = 0; i < client_cnt; i++) {
            if (httpd_ws_get_fd_info(ba->hd, client_fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
                if (websocket_clients++ >= CONFIG_DASHBOARD_MAX_CLIENTS) continue;
                esp_err_t err = httpd_ws_send_frame_async(ba->hd, client_fds[i], &pkt);
                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "send_frame_async fd=%d: %s",
                             client_fds[i], esp_err_to_name(err));
                    close_session(ba->hd, client_fds[i]);
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
        close_session(a->hd, a->fd);
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
