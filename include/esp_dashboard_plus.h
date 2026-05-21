#pragma once

#include <stdint.h>
#include <stddef.h>
#include <functional>
#include "esp_err.h"
#include "esp_http_server.h"  // httpd_req_t for _onClientConnect

#ifndef CONFIG_DASHBOARD_MAX_STR_LEN
#define CONFIG_DASHBOARD_MAX_STR_LEN 64
#endif
#ifndef CONFIG_DASHBOARD_MAX_CLIENTS
#define CONFIG_DASHBOARD_MAX_CLIENTS 4
#endif
#ifndef CONFIG_DASHBOARD_MAX_DROPDOWN_OPTS
#define CONFIG_DASHBOARD_MAX_DROPDOWN_OPTS 8
#endif

// ─────────────────────────────────────────────
// Callback types  (const char* replaces String)
// ─────────────────────────────────────────────
using ButtonCallback   = std::function<void()>;
using InputCallback    = std::function<void(const char*)>;
using ToggleCallback   = std::function<void(bool)>;
using SliderCallback   = std::function<void(int)>;
using ColorCallback    = std::function<void(const char*)>;
using DropdownCallback = std::function<void(const char*)>;
using TimezoneCallback = std::function<void(const char* tz, int offsetMin, const char* offsetStr)>;
using DateCallback     = std::function<void(const char*)>;
using TimeCallback     = std::function<void(const char*)>;
using LocationCallback = std::function<void(float lat, float lon)>;

// ─────────────────────────────────────────────
// Enumerations
// ─────────────────────────────────────────────
enum class CardType {
    STAT, CHART, BUTTON, ACTION, DATA_INPUT, COLOR, DROPDOWN,
    OTA, GAUGE, TOGGLE, SLIDER, LINK, TIMEZONE, DATE, TIME,
    LOCATION, STATUS, CONSOLE
};

enum class CardVariant   { PRIMARY, SUCCESS, WARNING, DANGER, INFO, SECONDARY };
enum class ChartType     { LINE, AREA, BAR, SCATTER, STEP };
enum class StatusIcon    { CHECK, ERROR, WARNING, INFO, WIFI, POWER, SYNC, CLOUD, LOCK, UNLOCK };
enum class LogLevel      { DEBUG, INFO, WARNING, ERROR };

// ─────────────────────────────────────────────
// Supporting structs
// ─────────────────────────────────────────────
struct DropdownOption {
    char value[CONFIG_DASHBOARD_MAX_STR_LEN];
    char label[CONFIG_DASHBOARD_MAX_STR_LEN];
};

struct ChartSeriesMeta {
    char name[CONFIG_DASHBOARD_MAX_STR_LEN];
    char color[16];  // "primary", "success", "warning", "danger", "info"
};

struct CardGroup {
    char id[CONFIG_DASHBOARD_MAX_STR_LEN];
    char title[CONFIG_DASHBOARD_MAX_STR_LEN];
    char cardIds[16][CONFIG_DASHBOARD_MAX_STR_LEN];
    int  cardCount;
};

// ─────────────────────────────────────────────
// Card pool entry — tagged union, fixed size
// ─────────────────────────────────────────────
struct DashboardCardEntry {
    bool        active;
    CardType    type;
    char        id[CONFIG_DASHBOARD_MAX_STR_LEN];
    char        title[CONFIG_DASHBOARD_MAX_STR_LEN];
    CardVariant variant;
    int         weight;
    int         sizeX, sizeY;

    union {
        struct {
            char value[64];
            char unit[16];
            char trend[8];
            char trendVal[16];
        } stat;

        struct {
            float value, min, max;
            char  unit[16];
            float warnThresh, dangerThresh;
        } gauge;

        struct {
            bool value;
            char label[64];
        } toggle;

        struct {
            int  value, min, max, step;
            char unit[16];
        } slider;

        struct {
            char  value[64];
            char  placeholder[64];
            char  inputType[8];
            float fmin, fmax, fstep;
            char  unit[16];
        } input;

        struct {
            char    value[16];
            char    presets[10][8];
            uint8_t presetCount;
        } color;

        struct {
            char            value[CONFIG_DASHBOARD_MAX_STR_LEN];
            char            placeholder[CONFIG_DASHBOARD_MAX_STR_LEN];
            DropdownOption* options;
            uint8_t         optCount;
            uint8_t         optMax;
        } dropdown;

        struct {
            char label[64];
            char url[128];
            char target[8];
        } link;

        struct {
            char label[64];
            char confirmTitle[64];
            char confirmMsg[128];
        } action;

        struct {
            ChartType        chartType;
            int              maxPoints;
            int              numSeries;
            int              headIdx;
            float*           data;
            ChartSeriesMeta* seriesMeta;
        } chart;

        struct {
            StatusIcon icon;
            char       label[64];
            char       message[128];
        } status;

        struct {
            char value[32];
            char label[64];
            bool includeTime;
            char minDate[16];
            char maxDate[16];
        } date_field;

        struct {
            char value[16];
            bool includeSeconds;
        } time_field;

        struct {
            float latitude, longitude;
            char  label[64];
        } location;

        struct {
            char value[32];
            char label[64];
        } timezone;
    } data;

    // One std::function per card; allocated once at add*Card() call, never at runtime
    ButtonCallback   onButton;
    InputCallback    onInput;
    ToggleCallback   onToggle;
    SliderCallback   onSlider;
    ColorCallback    onColor;
    DropdownCallback onDropdown;
    TimezoneCallback onTimezone;
    DateCallback     onDate;
    TimeCallback     onTime;
    LocationCallback onLocation;
};

// ─────────────────────────────────────────────
// Main dashboard class
// ─────────────────────────────────────────────
class ESPDashboardPlus {
public:
    explicit ESPDashboardPlus(const char* title = "ESP32 Dashboard");
    ~ESPDashboardPlus();

    esp_err_t begin(const uint8_t* htmlData, size_t htmlSize,
                    size_t maxCards = 16, bool enableOTA = true,
                    bool enableConsole = true,
                    uint16_t port = CONFIG_DASHBOARD_PORT);

    void loop();

    void setTitle(const char* title);
    void setSubtitle(const char* subtitle);
    void setVersionInfo(const char* version, const char* lastUpdate = "");
    void onCommand(std::function<void(const char*)> handler);

    bool isOTAEnabled()     const { return _enableOTA; }
    bool isConsoleEnabled() const { return _enableConsole; }

    // ── Add card methods ──────────────────────────────────────────────
    DashboardCardEntry* addStatCard(const char* id, const char* title,
                                    const char* value = "", const char* unit = "");
    DashboardCardEntry* addStatusCard(const char* id, const char* title,
                                      StatusIcon icon = StatusIcon::INFO);
    DashboardCardEntry* addChartCard(const char* id, const char* title,
                                     ChartType type = ChartType::LINE, int maxPoints = 20);
    DashboardCardEntry* addButtonCard(const char* id, const char* title,
                                      const char* label, ButtonCallback cb);
    DashboardCardEntry* addLinkCard(const char* id, const char* title,
                                    const char* label, const char* url);
    DashboardCardEntry* addTimezoneCard(const char* id, const char* title,
                                        const char* label = "Get Browser Timezone");
    DashboardCardEntry* addDateCard(const char* id, const char* title,
                                    bool includeTime = false);
    DashboardCardEntry* addTimeCard(const char* id, const char* title,
                                    bool includeSeconds = false);
    DashboardCardEntry* addLocationCard(const char* id, const char* title,
                                        const char* label = "Get Current Location");
    DashboardCardEntry* addActionButton(const char* id, const char* title,
                                        const char* label, const char* confirmTitle,
                                        const char* confirmMsg, ButtonCallback cb);
    DashboardCardEntry* addInputCard(const char* id, const char* title,
                                     const char* placeholder = "");
    DashboardCardEntry* addColorPickerCard(const char* id, const char* title,
                                           const char* defaultColor = "#00D4AA");
    DashboardCardEntry* addDropdownCard(const char* id, const char* title,
                                        const char* placeholder = "Select...");
    DashboardCardEntry* addToggleCard(const char* id, const char* title,
                                      const char* label = "", bool defaultValue = false);
    DashboardCardEntry* addSliderCard(const char* id, const char* title,
                                      int min = 0, int max = 100, int step = 1,
                                      const char* unit = "");
    DashboardCardEntry* addGaugeCard(const char* id, const char* title,
                                     float min = 0, float max = 100,
                                     const char* unit = "%");

    int addChartSeries(const char* cardId, const char* name,
                       const char* color = "primary");

    // ── Update card methods ───────────────────────────────────────────
    void updateStatCard(const char* id, const char* value);
    void updateStatusCard(const char* id, StatusIcon icon, CardVariant variant,
                          const char* label, const char* message);
    void updateChartCard(const char* id, float value);
    void updateChartCard(const char* id, int seriesIndex, float value);
    void updateGaugeCard(const char* id, float value);
    void updateToggleCard(const char* id, bool value);
    void updateSliderCard(const char* id, int value);
    void updateColorCard(const char* id, const char* color);
    void updateDropdownCard(const char* id, const char* value);
    void updateDateCard(const char* id, const char* value);
    void updateTimeCard(const char* id, const char* value);
    void updateLocationCard(const char* id, float lat, float lon);
    void updateLinkCard(const char* id, const char* url);

    // ── Group methods ─────────────────────────────────────────────────
    void addGroup(const char* id, const char* title);
    void addCardToGroup(const char* groupId, const char* cardId);
    void removeCardFromGroup(const char* groupId, const char* cardId);
    void removeGroup(const char* id);

    // ── Console log methods ───────────────────────────────────────────
    void logDebug(const char* message);
    void logInfo(const char* message);
    void logWarning(const char* message);
    void logError(const char* message);
    void log(LogLevel level, const char* message);

    // ── Internal (used by dashboard_server.cpp) ───────────────────────
    void _onClientConnect(httpd_req_t* req);
    void _queueInitForFd(void* hd, int fd);
    void _handleWebSocketMessage(const char* data, size_t len);
    const uint8_t* _htmlData;
    size_t         _htmlSize;

private:
    DashboardCardEntry* _cards;
    size_t              _maxCards;

    CardGroup _groups[8];
    int       _groupCount;

    void* _server;   // httpd_handle_t — void* to avoid IDF header in public API

    char  _title[CONFIG_DASHBOARD_MAX_STR_LEN];
    char  _subtitle[CONFIG_DASHBOARD_MAX_STR_LEN];
    char  _version[32];
    char  _lastUpdate[32];
    bool  _enableOTA;
    bool  _enableConsole;

    int64_t _lastHeartbeatMs;

    std::function<void(const char*)> _onCommandCb;

    void* _otaHandle;
    const void* _otaPartition;
    size_t _otaSize;
    size_t _otaReceived;

    int  _knownFds[CONFIG_DASHBOARD_MAX_CLIENTS + 2];
    int  _knownFdCount;

    DashboardCardEntry* _findSlot();
    DashboardCardEntry* _findById(const char* id);
    void _broadcastJson(const char* json);
    void _broadcastUpdate(const char* cardId, const char* dataJson);
    void _broadcastLog(LogLevel level, const char* message);
    void _sendInitToClient(httpd_req_t* req);
    void _sendInitToNewClients();
    char* _buildInitJson();
};
