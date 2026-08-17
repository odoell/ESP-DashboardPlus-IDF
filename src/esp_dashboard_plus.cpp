#include "esp_dashboard_plus.h"
#include "dashboard_server.h"
#include "dashboard_cards.h"
#if CONFIG_DASHBOARD_ENABLE_OTA
#include "dashboard_ota.h"
#endif
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"

static const char* TAG = "dashboard";

// Milliseconds since boot
static inline int64_t _millis() {
    return esp_timer_get_time() / 1000LL;
}

// ─── Constructor / Destructor ────────────────────────────────────────────────

ESPDashboardPlus::ESPDashboardPlus(const char* title)
    : _htmlData(nullptr), _htmlSize(0),
      _cards(nullptr), _maxCards(0),
      _groupCount(0),
      _server(nullptr),
      _enableOTA(true), _enableConsole(true),
      _lastHeartbeatMs(0),
#if CONFIG_DASHBOARD_ENABLE_OTA
      _otaHandle(nullptr), _otaPartition(nullptr),
      _otaSize(0), _otaReceived(0),
#endif
      _knownFdCount(0)
{
    strncpy(_title, title, sizeof(_title) - 1);
    _title[sizeof(_title) - 1] = '\0';
    _subtitle[0]   = '\0';
    _version[0]    = '\0';
    _lastUpdate[0] = '\0';
    memset(_groups,    0, sizeof(_groups));
    memset(_knownFds,  0, sizeof(_knownFds));
}

ESPDashboardPlus::~ESPDashboardPlus() {
    if (_cards) {
        for (size_t i = 0; i < _maxCards; i++) {
            if (_cards[i].active) cards_free_entry(&_cards[i]);
        }
        delete[] _cards;
        _cards = nullptr;
    }
    server_stop(static_cast<httpd_handle_t>(_server));
}

// ─── begin() ────────────────────────────────────────────────────────────────

esp_err_t ESPDashboardPlus::begin(const uint8_t* htmlData, size_t htmlSize,
                                   size_t maxCards, bool enableOTA,
                                   bool enableConsole, uint16_t port) {
    _htmlData      = htmlData;
    _htmlSize      = htmlSize;
#if CONFIG_DASHBOARD_ENABLE_OTA
    _enableOTA     = enableOTA;
#else
    _enableOTA     = false;
    (void)enableOTA;
#endif
#if CONFIG_DASHBOARD_ENABLE_CONSOLE
    _enableConsole = enableConsole;
#else
    _enableConsole = false;
    (void)enableConsole;
#endif

    // Allocate card pool — single contiguous allocation
    _cards    = new DashboardCardEntry[maxCards];
    _maxCards = maxCards;
    cards_pool_init(_cards, _maxCards);

    // Start HTTPS + WSS server
    httpd_handle_t handle = nullptr;
    esp_err_t err = server_start(this, port, &handle);
    if (err != ESP_OK) {
        delete[] _cards;
        _cards = nullptr;
        return err;
    }
    _server = handle;

    ESP_LOGI(TAG, "Dashboard started on port %u (OTA:%s Console:%s)",
             port,
             enableOTA     ? "on" : "off",
             enableConsole ? "on" : "off");
    return ESP_OK;
}

// ─── loop() ─────────────────────────────────────────────────────────────────

void ESPDashboardPlus::loop() {
    _sendInitToNewClients();

    int64_t now = _millis();
    if (now - _lastHeartbeatMs >= 2500) {
        _lastHeartbeatMs = now;

        cJSON* doc = cJSON_CreateObject();
        cJSON_AddStringToObject(doc, "type", "heartbeat");
        cJSON_AddNumberToObject(doc, "timestamp", (double)now);
        char* json = cJSON_PrintUnformatted(doc);
        cJSON_Delete(doc);
        if (json) {
            _broadcastJson(json);
            free(json);
        }
    }
}

// ─── setters ────────────────────────────────────────────────────────────────

void ESPDashboardPlus::setTitle(const char* title) {
    strncpy(_title, title, sizeof(_title) - 1);
}

void ESPDashboardPlus::setSubtitle(const char* subtitle) {
    strncpy(_subtitle, subtitle, sizeof(_subtitle) - 1);
}

void ESPDashboardPlus::setVersionInfo(const char* version, const char* lastUpdate) {
    strncpy(_version, version, sizeof(_version) - 1);
    if (lastUpdate) strncpy(_lastUpdate, lastUpdate, sizeof(_lastUpdate) - 1);
}

void ESPDashboardPlus::onCommand(std::function<void(const char*)> handler) {
#if CONFIG_DASHBOARD_ENABLE_CONSOLE
    _onCommandCb = handler;
#else
    (void)handler;
#endif
}

// ─── Internal helpers ────────────────────────────────────────────────────────

DashboardCardEntry* ESPDashboardPlus::_findSlot() {
    return cards_alloc_slot(_cards, _maxCards);
}

DashboardCardEntry* ESPDashboardPlus::_findById(const char* id) {
    return cards_find_by_id(_cards, _maxCards, id);
}

void ESPDashboardPlus::_broadcastJson(const char* json) {
    server_broadcast(static_cast<httpd_handle_t>(_server), json);
}

void ESPDashboardPlus::_broadcastUpdate(const char* cardId, const char* dataJson) {
    cJSON* doc = cJSON_CreateObject();
    cJSON_AddStringToObject(doc, "type",   "update");
    cJSON_AddStringToObject(doc, "cardId", cardId);
    // dataJson is already a JSON object string; embed it raw
    cJSON* data = cJSON_Parse(dataJson);
    if (data) cJSON_AddItemToObject(doc, "data", data);
    char* out = cJSON_PrintUnformatted(doc);
    cJSON_Delete(doc);
    if (out) { _broadcastJson(out); free(out); }
}

void ESPDashboardPlus::_broadcastLog(LogLevel level, const char* message) {
#if CONFIG_DASHBOARD_ENABLE_CONSOLE
    if (!_enableConsole) return;

    int64_t ms   = _millis();
    int64_t secs = ms / 1000;
    int64_t mins = secs / 60;
    int64_t hrs  = mins / 60;

    char ts[24];
    snprintf(ts, sizeof(ts), "%02lld:%02lld:%02lld.%03lld",
             hrs % 24, mins % 60, secs % 60, ms % 1000);

    cJSON* doc = cJSON_CreateObject();
    cJSON_AddStringToObject(doc, "type",      "log");
    cJSON_AddStringToObject(doc, "timestamp", ts);
    cJSON_AddStringToObject(doc, "level",     log_level_to_str(level));
    cJSON_AddStringToObject(doc, "message",   message);
    char* out = cJSON_PrintUnformatted(doc);
    cJSON_Delete(doc);
    if (out) { _broadcastJson(out); free(out); }
#else
    (void)level;
    (void)message;
#endif
}

// ─── _sendInitToClient() ─────────────────────────────────────────────────────

void ESPDashboardPlus::_sendInitToClient(httpd_req_t* req) {
    int activeCount = 0;
    for (size_t i = 0; i < _maxCards; i++) {
        if (_cards[i].active) activeCount++;
    }
    ESP_LOGI(TAG, "sendInit: %d active cards, %d groups", activeCount, _groupCount);

    cJSON* doc = cJSON_CreateObject();
    cJSON_AddStringToObject(doc, "type",  "init");
    cJSON_AddStringToObject(doc, "title", _title);
    if (_subtitle[0])   cJSON_AddStringToObject(doc, "subtitle",   _subtitle);
    if (_version[0])    cJSON_AddStringToObject(doc, "version",    _version);
    if (_lastUpdate[0]) cJSON_AddStringToObject(doc, "lastUpdate", _lastUpdate);
    cJSON_AddBoolToObject(doc, "enableOTA",     _enableOTA);
    cJSON_AddBoolToObject(doc, "enableConsole", _enableConsole);

    cJSON* cards = cJSON_AddArrayToObject(doc, "cards");
    for (size_t i = 0; i < _maxCards; i++) {
        if (_cards[i].active) {
            cJSON_AddItemToArray(cards, cards_to_cjson(&_cards[i]));
        }
    }

    if (_groupCount > 0) {
        cJSON* groups = cJSON_AddArrayToObject(doc, "groups");
        for (int i = 0; i < _groupCount; i++) {
            cJSON* g = cJSON_CreateObject();
            cJSON_AddStringToObject(g, "id",    _groups[i].id);
            cJSON_AddStringToObject(g, "title", _groups[i].title);
            cJSON* ids = cJSON_AddArrayToObject(g, "cardIds");
            for (int j = 0; j < _groups[i].cardCount; j++) {
                cJSON_AddItemToArray(ids, cJSON_CreateString(_groups[i].cardIds[j]));
            }
            cJSON_AddItemToArray(groups, g);
        }
    }

    char* out = cJSON_PrintUnformatted(doc);
    cJSON_Delete(doc);
    if (out) {
        ESP_LOGI(TAG, "sendInit: JSON %d bytes, sending...", (int)strlen(out));
        httpd_ws_frame_t pkt = {};
        pkt.final      = true;
        pkt.fragmented = false;
        pkt.type       = HTTPD_WS_TYPE_TEXT;
        pkt.payload    = reinterpret_cast<uint8_t*>(out);
        pkt.len        = strlen(out);
        esp_err_t err  = httpd_ws_send_frame(req, &pkt);
        ESP_LOGI(TAG, "sendInit: httpd_ws_send_frame -> %s", esp_err_to_name(err));
        free(out);
    } else {
        ESP_LOGE(TAG, "sendInit: cJSON_PrintUnformatted returned NULL");
    }
}

// ─── _onClientConnect() ──────────────────────────────────────────────────────

void ESPDashboardPlus::_onClientConnect(httpd_req_t* req) {
    _sendInitToClient(req);
}

// ─── _buildInitJson() ────────────────────────────────────────────────────────

char* ESPDashboardPlus::_buildInitJson() {
    cJSON* doc = cJSON_CreateObject();
    cJSON_AddStringToObject(doc, "type",  "init");
    cJSON_AddStringToObject(doc, "title", _title);
    if (_subtitle[0])   cJSON_AddStringToObject(doc, "subtitle",   _subtitle);
    if (_version[0])    cJSON_AddStringToObject(doc, "version",    _version);
    if (_lastUpdate[0]) cJSON_AddStringToObject(doc, "lastUpdate", _lastUpdate);
    cJSON_AddBoolToObject(doc, "enableOTA",     _enableOTA);
    cJSON_AddBoolToObject(doc, "enableConsole", _enableConsole);

    cJSON* cards = cJSON_AddArrayToObject(doc, "cards");
    for (size_t i = 0; i < _maxCards; i++) {
        if (_cards[i].active)
            cJSON_AddItemToArray(cards, cards_to_cjson(&_cards[i]));
    }

    if (_groupCount > 0) {
        cJSON* groups = cJSON_AddArrayToObject(doc, "groups");
        for (int i = 0; i < _groupCount; i++) {
            cJSON* g = cJSON_CreateObject();
            cJSON_AddStringToObject(g, "id",    _groups[i].id);
            cJSON_AddStringToObject(g, "title", _groups[i].title);
            cJSON* ids = cJSON_AddArrayToObject(g, "cardIds");
            for (int j = 0; j < _groups[i].cardCount; j++)
                cJSON_AddItemToArray(ids, cJSON_CreateString(_groups[i].cardIds[j]));
            cJSON_AddItemToArray(groups, g);
        }
    }

    char* out = cJSON_PrintUnformatted(doc);
    cJSON_Delete(doc);
    return out;  // caller must free()
}

// ─── _queueInitForFd() ───────────────────────────────────────────────────────

void ESPDashboardPlus::_queueInitForFd(void* hd, int fd) {
    char* out = _buildInitJson();
    if (out) {
        server_send_to_fd_async(static_cast<httpd_handle_t>(hd), fd, out);
        free(out);
    }
}

// ─── _sendInitToNewClients() ─────────────────────────────────────────────────
// Called every loop(). Detects newly connected WebSocket clients by comparing
// the current httpd client list against _knownFds. On new client: broadcasts
// the full init message to all clients so the newcomer gets full state.

void ESPDashboardPlus::_sendInitToNewClients() {
    if (!_server) return;
    httpd_handle_t hd = static_cast<httpd_handle_t>(_server);

    size_t max_fds    = CONFIG_DASHBOARD_MAX_CLIENTS + 2;
    size_t client_cnt = max_fds;
    int    client_fds[CONFIG_DASHBOARD_MAX_CLIENTS + 2];

    if (httpd_get_client_list(hd, &client_cnt, client_fds) != ESP_OK) return;

    bool newClient = false;
    for (size_t i = 0; i < client_cnt; i++) {
        if (httpd_ws_get_fd_info(hd, client_fds[i]) != HTTPD_WS_CLIENT_WEBSOCKET) continue;
        bool known = false;
        for (int j = 0; j < _knownFdCount; j++) {
            if (_knownFds[j] == client_fds[i]) { known = true; break; }
        }
        if (!known) {
            if (_knownFdCount < (int)(CONFIG_DASHBOARD_MAX_CLIENTS + 2))
                _knownFds[_knownFdCount++] = client_fds[i];
            newClient = true;
            ESP_LOGI(TAG, "New WS client fd=%d", client_fds[i]);
        }
    }

    // Prune fds that have since disconnected
    for (int j = _knownFdCount - 1; j >= 0; j--) {
        bool found = false;
        for (size_t i = 0; i < client_cnt; i++) {
            if (client_fds[i] == _knownFds[j]) { found = true; break; }
        }
        if (!found) _knownFds[j] = _knownFds[--_knownFdCount];
    }

    if (newClient) {
        char* out = _buildInitJson();
        if (out) { _broadcastJson(out); free(out); }
    }
}

// --- Base64 decode (reused from original library) ----------------------------

#if CONFIG_DASHBOARD_ENABLE_OTA
static size_t _b64_decoded_len(const char* input, size_t inLen) {
    if (inLen < 2) return 0;
    size_t padding = 0;
    if (inLen >= 1 && input[inLen - 1] == '=') padding++;
    if (inLen >= 2 && input[inLen - 2] == '=') padding++;
    return (inLen * 3 / 4) - padding;
}

static void _b64_decode(const char* input, size_t inLen, uint8_t* output) {
    static const char* b64chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t j = 0;
    uint32_t buf = 0;
    int bits = 0;
    for (size_t i = 0; i < inLen; i++) {
        char c = input[i];
        if (c == '=') break;
        const char* p = strchr(b64chars, c);
        if (!p) continue;
        buf = (buf << 6) | (uint32_t)(p - b64chars);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            output[j++] = (uint8_t)((buf >> bits) & 0xFF);
        }
    }
}
#endif

// --- _handleWebSocketMessage() -----------------------------------------------

void ESPDashboardPlus::_handleWebSocketMessage(const char* data, size_t len) {
    if (!data || len == 0) return;

    cJSON* doc = cJSON_ParseWithLength(data, len);
    if (!doc) {
        ESP_LOGW(TAG, "WS JSON parse failed");
        return;
    }

    cJSON* typeItem = cJSON_GetObjectItem(doc, "type");
    if (!typeItem || !cJSON_IsString(typeItem)) {
        cJSON_Delete(doc);
        return;
    }
    const char* type = typeItem->valuestring;

    if (strcmp(type, "init") == 0) {
        cJSON_Delete(doc);
        return;
    }

    if (strcmp(type, "action") == 0) {
        cJSON* cardIdItem = cJSON_GetObjectItem(doc, "cardId");
        cJSON* actionItem = cJSON_GetObjectItem(doc, "action");
        cJSON* dataObj    = cJSON_GetObjectItem(doc, "data");
        if (!cardIdItem || !actionItem) { cJSON_Delete(doc); return; }

        const char* cardId = cardIdItem->valuestring;
        const char* action = actionItem->valuestring;
        DashboardCardEntry* e = _findById(cardId);

        if (e) {
            if (strcmp(action, "click") == 0 || strcmp(action, "confirm") == 0) {
                if (e->onButton) e->onButton();
            } else if (strcmp(action, "change") == 0 || strcmp(action, "submit") == 0) {
                cJSON* val = dataObj ? cJSON_GetObjectItem(dataObj, "value") : nullptr;
                if (val && cJSON_IsString(val) && e->onInput)
                    e->onInput(val->valuestring);
                if (val && cJSON_IsBool(val) && e->onToggle)
                    e->onToggle(cJSON_IsTrue(val));
                if (val && cJSON_IsNumber(val) && e->onSlider)
                    e->onSlider((int)val->valuedouble);
            } else if (strcmp(action, "color") == 0) {
                cJSON* col = dataObj ? cJSON_GetObjectItem(dataObj, "color") : nullptr;
                if (col && e->onColor) e->onColor(col->valuestring);
            } else if (strcmp(action, "timezone") == 0 && dataObj) {
                cJSON* tz  = cJSON_GetObjectItem(dataObj, "timezone");
                cJSON* off = cJSON_GetObjectItem(dataObj, "offset");
                cJSON* ofs = cJSON_GetObjectItem(dataObj, "offsetString");
                if (tz && e->onTimezone) {
                    e->onTimezone(
                        tz->valuestring,
                        off ? (int)off->valuedouble : 0,
                        ofs ? ofs->valuestring : "");
                    snprintf(e->data.timezone.value, sizeof(e->data.timezone.value),
                             "%s", tz->valuestring);
                }
            } else if (strcmp(action, "location") == 0 && dataObj) {
                cJSON* lat = cJSON_GetObjectItem(dataObj, "latitude");
                cJSON* lon = cJSON_GetObjectItem(dataObj, "longitude");
                if (lat && lon) {
                    e->data.location.latitude  = (float)lat->valuedouble;
                    e->data.location.longitude = (float)lon->valuedouble;
                    if (e->onLocation)
                        e->onLocation(e->data.location.latitude,
                                      e->data.location.longitude);
                }
            } else if (strcmp(action, "date") == 0) {
                cJSON* val = dataObj ? cJSON_GetObjectItem(dataObj, "value") : nullptr;
                if (val && cJSON_IsString(val)) {
                    snprintf(e->data.date_field.value, sizeof(e->data.date_field.value),
                             "%s", val->valuestring);
                    if (e->onDate) e->onDate(val->valuestring);
                }
            } else if (strcmp(action, "time") == 0) {
                cJSON* val = dataObj ? cJSON_GetObjectItem(dataObj, "value") : nullptr;
                if (val && cJSON_IsString(val)) {
                    snprintf(e->data.time_field.value, sizeof(e->data.time_field.value),
                             "%s", val->valuestring);
                    if (e->onTime) e->onTime(val->valuestring);
                }
            } else if (strcmp(action, "dropdown") == 0) {
                cJSON* val = dataObj ? cJSON_GetObjectItem(dataObj, "value") : nullptr;
                if (val && cJSON_IsString(val)) {
                    snprintf(e->data.dropdown.value, sizeof(e->data.dropdown.value),
                             "%s", val->valuestring);
                    if (e->onDropdown) e->onDropdown(val->valuestring);
                }
            }

#if CONFIG_DASHBOARD_ENABLE_OTA
            if (_enableOTA) {
                if (strcmp(action, "ota_start") == 0 && dataObj) {
                    cJSON* sz = cJSON_GetObjectItem(dataObj, "size");
                    if (sz) {
                        _otaSize     = (size_t)sz->valuedouble;
                        _otaReceived = 0;
                        ota_begin(_otaSize);
                    }
                } else if (strcmp(action, "ota_chunk") == 0 && dataObj) {
                    cJSON* b64 = cJSON_GetObjectItem(dataObj, "data");
                    if (b64 && ota_in_progress()) {
                        size_t dlen = _b64_decoded_len(b64->valuestring,
                                                       strlen(b64->valuestring));
                        if (dlen > 0) {
                            auto* decoded = new uint8_t[dlen];
                            _b64_decode(b64->valuestring, strlen(b64->valuestring), decoded);
                            ota_write(decoded, dlen);
                            _otaReceived += dlen;
                            delete[] decoded;
                        }
                    }
                } else if (strcmp(action, "ota_end") == 0) {
                    ota_end(_otaSize, _otaReceived);
                }
            }
#endif
        }
        cJSON_Delete(doc);
        return;
    }

#if CONFIG_DASHBOARD_ENABLE_CONSOLE
    if (strcmp(type, "command") == 0) {
        cJSON* cmd = cJSON_GetObjectItem(doc, "command");
        if (cmd && cJSON_IsString(cmd) && _onCommandCb) {
            _onCommandCb(cmd->valuestring);
        }
        cJSON_Delete(doc);
        return;
    }
#endif

#if CONFIG_DASHBOARD_ENABLE_OTA
    if (_enableOTA) {
        if (strcmp(type, "ota_start") == 0) {
            cJSON* sz = cJSON_GetObjectItem(doc, "size");
            if (sz) {
                _otaSize = (size_t)sz->valuedouble;
                _otaReceived = 0;
                ota_begin(_otaSize);
            }
        } else if (strcmp(type, "ota_chunk") == 0 && ota_in_progress()) {
            cJSON* b64 = cJSON_GetObjectItem(doc, "data");
            if (b64) {
                size_t dlen = _b64_decoded_len(b64->valuestring,
                                               strlen(b64->valuestring));
                if (dlen > 0) {
                    auto* decoded = new uint8_t[dlen];
                    _b64_decode(b64->valuestring, strlen(b64->valuestring), decoded);
                    ota_write(decoded, dlen);
                    _otaReceived += dlen;
                    delete[] decoded;
                }
            }
        } else if (strcmp(type, "ota_end") == 0) {
            ota_end(_otaSize, _otaReceived);
        }
    }
#endif

    cJSON_Delete(doc);
}

// ═══════════════════════════════════════════════════════
// add*Card methods
// ═══════════════════════════════════════════════════════

DashboardCardEntry* ESPDashboardPlus::addStatCard(const char* id, const char* title,
                                                   const char* value, const char* unit) {
    DashboardCardEntry* e = _findSlot();
    if (!e) { ESP_LOGW(TAG, "Card pool full"); return nullptr; }
    e->type = CardType::STAT;
    strncpy(e->id,    id,    sizeof(e->id)    - 1);
    strncpy(e->title, title, sizeof(e->title) - 1);
    strncpy(e->data.stat.value, value ? value : "", sizeof(e->data.stat.value) - 1);
    strncpy(e->data.stat.unit,  unit  ? unit  : "", sizeof(e->data.stat.unit)  - 1);
    return e;
}

DashboardCardEntry* ESPDashboardPlus::addStatusCard(const char* id, const char* title,
                                                     StatusIcon icon) {
    DashboardCardEntry* e = _findSlot();
    if (!e) { ESP_LOGW(TAG, "Card pool full"); return nullptr; }
    e->type = CardType::STATUS;
    strncpy(e->id,    id,    sizeof(e->id)    - 1);
    strncpy(e->title, title, sizeof(e->title) - 1);
    e->data.status.icon = icon;
    return e;
}

DashboardCardEntry* ESPDashboardPlus::addChartCard(const char* id, const char* title,
                                                    ChartType type, int maxPoints) {
    DashboardCardEntry* e = _findSlot();
    if (!e) { ESP_LOGW(TAG, "Card pool full"); return nullptr; }
    e->type = CardType::CHART;
    strncpy(e->id,    id,    sizeof(e->id)    - 1);
    strncpy(e->title, title, sizeof(e->title) - 1);
    e->data.chart.chartType  = type;
    e->data.chart.maxPoints  = maxPoints;
    e->data.chart.numSeries  = 0;
    e->data.chart.headIdx    = 0;
    e->data.chart.data       = new float[maxPoints]();
    e->data.chart.seriesMeta = nullptr;
    return e;
}

int ESPDashboardPlus::addChartSeries(const char* cardId, const char* name,
                                      const char* color) {
    DashboardCardEntry* e = _findById(cardId);
    if (!e || e->type != CardType::CHART) return -1;

    int ns = e->data.chart.numSeries;
    int mp = e->data.chart.maxPoints;

    // Save old metadata pointer so we can copy before freeing
    ChartSeriesMeta* oldMeta = e->data.chart.seriesMeta;

    // Reallocate data buffer and series metadata (only called at setup, not runtime)
    delete[] e->data.chart.data;
    e->data.chart.data       = new float[(ns + 1) * mp]();
    e->data.chart.seriesMeta = new ChartSeriesMeta[ns + 1]();

    // Copy existing series metadata into the new array
    for (int i = 0; i < ns; i++) {
        e->data.chart.seriesMeta[i] = oldMeta[i];
    }
    delete[] oldMeta;

    strncpy(e->data.chart.seriesMeta[ns].name,  name,  sizeof(ChartSeriesMeta::name)  - 1);
    strncpy(e->data.chart.seriesMeta[ns].color, color, sizeof(ChartSeriesMeta::color) - 1);
    e->data.chart.numSeries = ns + 1;
    e->data.chart.headIdx   = 0;
    return ns;
}

DashboardCardEntry* ESPDashboardPlus::addButtonCard(const char* id, const char* title,
                                                     const char* label, ButtonCallback cb) {
    DashboardCardEntry* e = _findSlot();
    if (!e) { ESP_LOGW(TAG, "Card pool full"); return nullptr; }
    e->type = CardType::BUTTON;
    strncpy(e->id,    id,    sizeof(e->id)    - 1);
    strncpy(e->title, title, sizeof(e->title) - 1);
    strncpy(e->data.action.label, label, sizeof(e->data.action.label) - 1);  // BUTTON reuses action.label — see cards_to_cjson BUTTON case
    e->onButton = cb;
    return e;
}

DashboardCardEntry* ESPDashboardPlus::addLinkCard(const char* id, const char* title,
                                                   const char* label, const char* url) {
    DashboardCardEntry* e = _findSlot();
    if (!e) { ESP_LOGW(TAG, "Card pool full"); return nullptr; }
    e->type = CardType::LINK;
    strncpy(e->id,    id,    sizeof(e->id)    - 1);
    strncpy(e->title, title, sizeof(e->title) - 1);
    strncpy(e->data.link.label,  label, sizeof(e->data.link.label)  - 1);
    strncpy(e->data.link.url,    url,   sizeof(e->data.link.url)    - 1);
    strncpy(e->data.link.target, "_blank", sizeof(e->data.link.target) - 1);
    return e;
}

DashboardCardEntry* ESPDashboardPlus::addTimezoneCard(const char* id, const char* title,
                                                       const char* label) {
    DashboardCardEntry* e = _findSlot();
    if (!e) { ESP_LOGW(TAG, "Card pool full"); return nullptr; }
    e->type = CardType::TIMEZONE;
    e->variant = CardVariant::INFO;
    strncpy(e->id,    id,    sizeof(e->id)    - 1);
    strncpy(e->title, title, sizeof(e->title) - 1);
    strncpy(e->data.timezone.label, label, sizeof(e->data.timezone.label) - 1);
    return e;
}

DashboardCardEntry* ESPDashboardPlus::addDateCard(const char* id, const char* title,
                                                   bool includeTime) {
    DashboardCardEntry* e = _findSlot();
    if (!e) { ESP_LOGW(TAG, "Card pool full"); return nullptr; }
    e->type = CardType::DATE;
    strncpy(e->id,    id,    sizeof(e->id)    - 1);
    strncpy(e->title, title, sizeof(e->title) - 1);
    e->data.date_field.includeTime = includeTime;
    return e;
}

DashboardCardEntry* ESPDashboardPlus::addTimeCard(const char* id, const char* title,
                                                   bool includeSeconds) {
    DashboardCardEntry* e = _findSlot();
    if (!e) { ESP_LOGW(TAG, "Card pool full"); return nullptr; }
    e->type = CardType::TIME;
    strncpy(e->id,    id,    sizeof(e->id)    - 1);
    strncpy(e->title, title, sizeof(e->title) - 1);
    e->data.time_field.includeSeconds = includeSeconds;
    return e;
}

DashboardCardEntry* ESPDashboardPlus::addLocationCard(const char* id, const char* title,
                                                       const char* label) {
    DashboardCardEntry* e = _findSlot();
    if (!e) { ESP_LOGW(TAG, "Card pool full"); return nullptr; }
    e->type = CardType::LOCATION;
    e->variant = CardVariant::INFO;
    strncpy(e->id,    id,    sizeof(e->id)    - 1);
    strncpy(e->title, title, sizeof(e->title) - 1);
    strncpy(e->data.location.label, label, sizeof(e->data.location.label) - 1);
    return e;
}

DashboardCardEntry* ESPDashboardPlus::addActionButton(const char* id, const char* title,
                                                       const char* label,
                                                       const char* confirmTitle,
                                                       const char* confirmMsg,
                                                       ButtonCallback cb) {
    DashboardCardEntry* e = _findSlot();
    if (!e) { ESP_LOGW(TAG, "Card pool full"); return nullptr; }
    e->type = CardType::ACTION;
    e->variant = CardVariant::WARNING;
    strncpy(e->id,    id,    sizeof(e->id)    - 1);
    strncpy(e->title, title, sizeof(e->title) - 1);
    strncpy(e->data.action.label,        label,        sizeof(e->data.action.label)        - 1);
    strncpy(e->data.action.confirmTitle, confirmTitle, sizeof(e->data.action.confirmTitle) - 1);
    strncpy(e->data.action.confirmMsg,   confirmMsg,   sizeof(e->data.action.confirmMsg)   - 1);
    e->onButton = cb;
    return e;
}

DashboardCardEntry* ESPDashboardPlus::addInputCard(const char* id, const char* title,
                                                    const char* placeholder) {
    DashboardCardEntry* e = _findSlot();
    if (!e) { ESP_LOGW(TAG, "Card pool full"); return nullptr; }
    e->type = CardType::DATA_INPUT;
    strncpy(e->id,    id,    sizeof(e->id)    - 1);
    strncpy(e->title, title, sizeof(e->title) - 1);
    strncpy(e->data.input.placeholder, placeholder ? placeholder : "",
            sizeof(e->data.input.placeholder) - 1);
    strncpy(e->data.input.inputType, "text", sizeof(e->data.input.inputType) - 1);
    return e;
}

DashboardCardEntry* ESPDashboardPlus::addColorPickerCard(const char* id, const char* title,
                                                          const char* defaultColor) {
    DashboardCardEntry* e = _findSlot();
    if (!e) { ESP_LOGW(TAG, "Card pool full"); return nullptr; }
    e->type = CardType::COLOR;
    strncpy(e->id,    id,    sizeof(e->id)    - 1);
    strncpy(e->title, title, sizeof(e->title) - 1);
    strncpy(e->data.color.value, defaultColor ? defaultColor : "#00D4AA",
            sizeof(e->data.color.value) - 1);
    static const char* def_presets[] = {
        "#00D4AA","#22C55E","#F59E0B","#F97316","#EF4444",
        "#EC4899","#8B5CF6","#3B82F6","#06B6D4","#FFFFFF"
    };
    e->data.color.presetCount = 10;
    for (int i = 0; i < 10; i++)
        strncpy(e->data.color.presets[i], def_presets[i],
                sizeof(e->data.color.presets[i]) - 1);
    return e;
}

DashboardCardEntry* ESPDashboardPlus::addDropdownCard(const char* id, const char* title,
                                                       const char* placeholder) {
    DashboardCardEntry* e = _findSlot();
    if (!e) { ESP_LOGW(TAG, "Card pool full"); return nullptr; }
    e->type = CardType::DROPDOWN;
    strncpy(e->id,    id,    sizeof(e->id)    - 1);
    strncpy(e->title, title, sizeof(e->title) - 1);
    strncpy(e->data.dropdown.placeholder, placeholder ? placeholder : "Select...",
            sizeof(e->data.dropdown.placeholder) - 1);
    e->data.dropdown.optMax   = CONFIG_DASHBOARD_MAX_DROPDOWN_OPTS;
    e->data.dropdown.optCount = 0;
    e->data.dropdown.options  = new DropdownOption[CONFIG_DASHBOARD_MAX_DROPDOWN_OPTS]();
    return e;
}

DashboardCardEntry* ESPDashboardPlus::addToggleCard(const char* id, const char* title,
                                                     const char* label, bool defaultValue) {
    DashboardCardEntry* e = _findSlot();
    if (!e) { ESP_LOGW(TAG, "Card pool full"); return nullptr; }
    e->type = CardType::TOGGLE;
    strncpy(e->id,    id,    sizeof(e->id)    - 1);
    strncpy(e->title, title, sizeof(e->title) - 1);
    strncpy(e->data.toggle.label, label ? label : "", sizeof(e->data.toggle.label) - 1);
    e->data.toggle.value = defaultValue;
    return e;
}

DashboardCardEntry* ESPDashboardPlus::addSliderCard(const char* id, const char* title,
                                                     int min, int max, int step,
                                                     const char* unit) {
    DashboardCardEntry* e = _findSlot();
    if (!e) { ESP_LOGW(TAG, "Card pool full"); return nullptr; }
    e->type = CardType::SLIDER;
    strncpy(e->id,    id,    sizeof(e->id)    - 1);
    strncpy(e->title, title, sizeof(e->title) - 1);
    e->data.slider.value = min;
    e->data.slider.min   = min;
    e->data.slider.max   = max;
    e->data.slider.step  = step;
    strncpy(e->data.slider.unit, unit ? unit : "", sizeof(e->data.slider.unit) - 1);
    return e;
}

DashboardCardEntry* ESPDashboardPlus::addGaugeCard(const char* id, const char* title,
                                                    float min, float max,
                                                    const char* unit) {
    DashboardCardEntry* e = _findSlot();
    if (!e) { ESP_LOGW(TAG, "Card pool full"); return nullptr; }
    e->type = CardType::GAUGE;
    strncpy(e->id,    id,    sizeof(e->id)    - 1);
    strncpy(e->title, title, sizeof(e->title) - 1);
    e->data.gauge.value       = min;
    e->data.gauge.min         = min;
    e->data.gauge.max         = max;
    e->data.gauge.warnThresh  = 70.0f;
    e->data.gauge.dangerThresh = 90.0f;
    strncpy(e->data.gauge.unit, unit ? unit : "%", sizeof(e->data.gauge.unit) - 1);
    return e;
}

// ═══════════════════════════════════════════════════════
// update*Card helpers
// ═══════════════════════════════════════════════════════

#define CARD_UPDATE_BEGIN(id_, type_) \
    DashboardCardEntry* e = _findById(id_); \
    if (!e || e->type != CardType::type_) return; \
    cJSON* data = cJSON_CreateObject();

#define CARD_UPDATE_END(id_) \
    char* out = cJSON_PrintUnformatted(data); \
    cJSON_Delete(data); \
    if (out) { _broadcastUpdate(id_, out); free(out); }

void ESPDashboardPlus::updateStatCard(const char* id, const char* value) {
    CARD_UPDATE_BEGIN(id, STAT)
    strncpy(e->data.stat.value, value, sizeof(e->data.stat.value) - 1);
    cJSON_AddStringToObject(data, "value", value);
    CARD_UPDATE_END(id)
}

void ESPDashboardPlus::updateStatusCard(const char* id, StatusIcon icon,
                                         CardVariant variant, const char* label,
                                         const char* message) {
    CARD_UPDATE_BEGIN(id, STATUS)
    e->data.status.icon = icon;
    e->variant          = variant;
    strncpy(e->data.status.label,   label,   sizeof(e->data.status.label)   - 1);
    strncpy(e->data.status.message, message, sizeof(e->data.status.message) - 1);
    cJSON_AddStringToObject(data, "icon",    icon_to_str(icon));
    cJSON_AddStringToObject(data, "variant", variant_to_str(variant));
    cJSON_AddStringToObject(data, "label",   label);
    cJSON_AddStringToObject(data, "message", message);
    CARD_UPDATE_END(id)
}

void ESPDashboardPlus::updateChartCard(const char* id, float value) {
    DashboardCardEntry* e = _findById(id);
    if (!e || e->type != CardType::CHART) return;
    int mp = e->data.chart.maxPoints;
    e->data.chart.data[e->data.chart.headIdx % mp] = value;
    e->data.chart.headIdx = (e->data.chart.headIdx + 1) % mp;

    cJSON* data = cJSON_CreateObject();
    cJSON* arr  = cJSON_AddArrayToObject(data, "data");
    int head = e->data.chart.headIdx;
    for (int i = 0; i < mp; i++)
        cJSON_AddItemToArray(arr, cJSON_CreateNumber(
            e->data.chart.data[(head + i) % mp]));
    char* out = cJSON_PrintUnformatted(data);
    cJSON_Delete(data);
    if (out) { _broadcastUpdate(id, out); free(out); }
}

void ESPDashboardPlus::updateChartCard(const char* id, int seriesIndex, float value) {
    DashboardCardEntry* e = _findById(id);
    if (!e || e->type != CardType::CHART) return;
    if (seriesIndex < 0 || seriesIndex >= e->data.chart.numSeries) return;

    int mp  = e->data.chart.maxPoints;
    int off = seriesIndex * mp;
    int hi  = e->data.chart.headIdx;
    e->data.chart.data[off + hi % mp] = value;
    // headIdx advances only when series 0 is updated — callers must update series 0 each tick
    if (seriesIndex == 0)
        e->data.chart.headIdx = (hi + 1) % mp;

    cJSON* upd     = cJSON_CreateObject();
    cJSON* seriesA = cJSON_AddArrayToObject(upd, "series");
    int head = e->data.chart.headIdx;
    for (int s = 0; s < e->data.chart.numSeries; s++) {
        cJSON* so  = cJSON_CreateObject();
        cJSON_AddStringToObject(so, "name",  e->data.chart.seriesMeta[s].name);
        cJSON_AddStringToObject(so, "color", e->data.chart.seriesMeta[s].color);
        cJSON* arr = cJSON_AddArrayToObject(so, "data");
        for (int i = 0; i < mp; i++)
            cJSON_AddItemToArray(arr, cJSON_CreateNumber(
                e->data.chart.data[s * mp + (head + i) % mp]));
        cJSON_AddItemToArray(seriesA, so);
    }
    char* out = cJSON_PrintUnformatted(upd);
    cJSON_Delete(upd);
    if (out) { _broadcastUpdate(id, out); free(out); }
}

void ESPDashboardPlus::updateGaugeCard(const char* id, float value) {
    CARD_UPDATE_BEGIN(id, GAUGE)
    e->data.gauge.value = dashboard_constrain(value, e->data.gauge.min, e->data.gauge.max);
    cJSON_AddNumberToObject(data, "value", e->data.gauge.value);
    CARD_UPDATE_END(id)
}

void ESPDashboardPlus::updateToggleCard(const char* id, bool value) {
    CARD_UPDATE_BEGIN(id, TOGGLE)
    e->data.toggle.value = value;
    cJSON_AddBoolToObject(data, "value", value);
    CARD_UPDATE_END(id)
}

void ESPDashboardPlus::updateSliderCard(const char* id, int value) {
    CARD_UPDATE_BEGIN(id, SLIDER)
    e->data.slider.value = dashboard_constrain(value, e->data.slider.min, e->data.slider.max);
    cJSON_AddNumberToObject(data, "value", e->data.slider.value);
    CARD_UPDATE_END(id)
}

void ESPDashboardPlus::updateColorCard(const char* id, const char* color) {
    CARD_UPDATE_BEGIN(id, COLOR)
    strncpy(e->data.color.value, color, sizeof(e->data.color.value) - 1);
    cJSON_AddStringToObject(data, "value", color);
    CARD_UPDATE_END(id)
}

void ESPDashboardPlus::updateDropdownCard(const char* id, const char* value) {
    CARD_UPDATE_BEGIN(id, DROPDOWN)
    strncpy(e->data.dropdown.value, value, sizeof(e->data.dropdown.value) - 1);
    cJSON_AddStringToObject(data, "value", value);
    CARD_UPDATE_END(id)
}

void ESPDashboardPlus::updateDateCard(const char* id, const char* value) {
    CARD_UPDATE_BEGIN(id, DATE)
    strncpy(e->data.date_field.value, value, sizeof(e->data.date_field.value) - 1);
    cJSON_AddStringToObject(data, "value", value);
    CARD_UPDATE_END(id)
}

void ESPDashboardPlus::updateTimeCard(const char* id, const char* value) {
    CARD_UPDATE_BEGIN(id, TIME)
    strncpy(e->data.time_field.value, value, sizeof(e->data.time_field.value) - 1);
    cJSON_AddStringToObject(data, "value", value);
    CARD_UPDATE_END(id)
}

void ESPDashboardPlus::updateLocationCard(const char* id, float lat, float lon) {
    CARD_UPDATE_BEGIN(id, LOCATION)
    e->data.location.latitude  = lat;
    e->data.location.longitude = lon;
    cJSON_AddNumberToObject(data, "latitude",  lat);
    cJSON_AddNumberToObject(data, "longitude", lon);
    CARD_UPDATE_END(id)
}

void ESPDashboardPlus::updateLinkCard(const char* id, const char* url) {
    CARD_UPDATE_BEGIN(id, LINK)
    strncpy(e->data.link.url, url, sizeof(e->data.link.url) - 1);
    cJSON_AddStringToObject(data, "url", url);
    CARD_UPDATE_END(id)
}

// ═══════════════════════════════════════════════════════
// Console log methods
// ═══════════════════════════════════════════════════════

void ESPDashboardPlus::logDebug(const char* message)   { _broadcastLog(LogLevel::DEBUG,   message); }
void ESPDashboardPlus::logInfo(const char* message)    { _broadcastLog(LogLevel::INFO,    message); }
void ESPDashboardPlus::logWarning(const char* message) { _broadcastLog(LogLevel::WARNING, message); }
void ESPDashboardPlus::logError(const char* message)   { _broadcastLog(LogLevel::ERROR,   message); }
void ESPDashboardPlus::log(LogLevel level, const char* message) { _broadcastLog(level, message); }

// ═══════════════════════════════════════════════════════
// Group methods
// ═══════════════════════════════════════════════════════

void ESPDashboardPlus::addGroup(const char* id, const char* title) {
    if (_groupCount >= 8) { ESP_LOGW(TAG, "Max 8 groups"); return; }
    CardGroup& g = _groups[_groupCount++];
    memset(&g, 0, sizeof(CardGroup));
    strncpy(g.id,    id,    sizeof(g.id)    - 1);
    strncpy(g.title, title, sizeof(g.title) - 1);
}

void ESPDashboardPlus::addCardToGroup(const char* groupId, const char* cardId) {
    for (int i = 0; i < _groupCount; i++) {
        if (strcmp(_groups[i].id, groupId) == 0) {
            if (_groups[i].cardCount < 16) {
                strncpy(_groups[i].cardIds[_groups[i].cardCount++], cardId,
                        sizeof(_groups[i].cardIds[0]) - 1);
            }
            return;
        }
    }
}

void ESPDashboardPlus::removeCardFromGroup(const char* groupId, const char* cardId) {
    for (int i = 0; i < _groupCount; i++) {
        if (strcmp(_groups[i].id, groupId) != 0) continue;
        CardGroup& g = _groups[i];
        for (int j = 0; j < g.cardCount; j++) {
            if (strcmp(g.cardIds[j], cardId) == 0) {
                for (int k = j; k < g.cardCount - 1; k++)
                    memcpy(g.cardIds[k], g.cardIds[k + 1], sizeof(g.cardIds[0]));
                g.cardCount--;
                return;
            }
        }
    }
}

void ESPDashboardPlus::removeGroup(const char* id) {
    for (int i = 0; i < _groupCount; i++) {
        if (strcmp(_groups[i].id, id) == 0) {
            for (int j = i; j < _groupCount - 1; j++)
                _groups[j] = _groups[j + 1];
            _groupCount--;
            return;
        }
    }
}
