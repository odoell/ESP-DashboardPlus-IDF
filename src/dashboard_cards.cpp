#include "dashboard_cards.h"
#include <string.h>
#include <stdlib.h>

// ─── Pool helpers ────────────────────────────────────────────────────────────

void cards_pool_init(DashboardCardEntry* pool, size_t maxCards) {
    memset(pool, 0, sizeof(DashboardCardEntry) * maxCards);
}

DashboardCardEntry* cards_alloc_slot(DashboardCardEntry* pool, size_t maxCards) {
    for (size_t i = 0; i < maxCards; i++) {
        if (!pool[i].active) {
            pool[i].active = true;
            pool[i].sizeX  = 1;
            pool[i].sizeY  = 1;
            pool[i].weight = 0;
            pool[i].variant = CardVariant::PRIMARY;
            return &pool[i];
        }
    }
    return nullptr;
}

DashboardCardEntry* cards_find_by_id(DashboardCardEntry* pool, size_t maxCards,
                                      const char* id) {
    for (size_t i = 0; i < maxCards; i++) {
        if (pool[i].active && strcmp(pool[i].id, id) == 0) {
            return &pool[i];
        }
    }
    return nullptr;
}

void cards_free_entry(DashboardCardEntry* e) {
    if (!e || !e->active) return;
    if (e->type == CardType::CHART) {
        delete[] e->data.chart.data;
        delete[] e->data.chart.seriesMeta;
        e->data.chart.data       = nullptr;
        e->data.chart.seriesMeta = nullptr;
    }
    if (e->type == CardType::DROPDOWN) {
        delete[] e->data.dropdown.options;
        e->data.dropdown.options = nullptr;
    }
    e->active = false;
}

// ─── String converters ───────────────────────────────────────────────────────

const char* variant_to_str(CardVariant v) {
    switch (v) {
        case CardVariant::SUCCESS:   return "success";
        case CardVariant::WARNING:   return "warning";
        case CardVariant::DANGER:    return "danger";
        case CardVariant::INFO:      return "info";
        case CardVariant::SECONDARY: return "secondary";
        default:                     return "primary";
    }
}

const char* icon_to_str(StatusIcon i) {
    switch (i) {
        case StatusIcon::CHECK:   return "check";
        case StatusIcon::ERROR:   return "error";
        case StatusIcon::WARNING: return "warning";
        case StatusIcon::INFO:    return "info";
        case StatusIcon::WIFI:    return "wifi";
        case StatusIcon::POWER:   return "power";
        case StatusIcon::SYNC:    return "sync";
        case StatusIcon::CLOUD:   return "cloud";
        case StatusIcon::LOCK:    return "lock";
        case StatusIcon::UNLOCK:  return "unlock";
        default:                  return "info";
    }
}

const char* chart_type_to_str(ChartType t) {
    switch (t) {
        case ChartType::AREA:    return "area";
        case ChartType::BAR:     return "bar";
        case ChartType::SCATTER: return "scatter";
        case ChartType::STEP:    return "step";
        default:                 return "line";
    }
}

const char* card_type_to_str(CardType t) {
    switch (t) {
        case CardType::STAT:       return "stat";
        case CardType::CHART:      return "chart";
        case CardType::BUTTON:     return "button";
        case CardType::ACTION:     return "action";
        case CardType::DATA_INPUT: return "input";
        case CardType::COLOR:      return "color";
        case CardType::DROPDOWN:   return "dropdown";
        case CardType::OTA:        return "ota";
        case CardType::GAUGE:      return "gauge";
        case CardType::TOGGLE:     return "toggle";
        case CardType::SLIDER:     return "slider";
        case CardType::LINK:       return "link";
        case CardType::TIMEZONE:   return "timezone";
        case CardType::DATE:       return "date";
        case CardType::TIME:       return "time";
        case CardType::LOCATION:   return "location";
        case CardType::STATUS:     return "status";
        case CardType::CONSOLE:    return "console";
        default:                   return "stat";
    }
}

const char* log_level_to_str(LogLevel l) {
    switch (l) {
        case LogLevel::DEBUG:   return "debug";
        case LogLevel::INFO:    return "info";
        case LogLevel::WARNING: return "warning";
        case LogLevel::ERROR:   return "error";
        default:                return "info";
    }
}

// ─── cJSON serialization helpers ─────────────────────────────────────────────

static void _add_size(cJSON* cfg, const DashboardCardEntry* e) {
    if (e->sizeX > 1) cJSON_AddNumberToObject(cfg, "sizeX", e->sizeX);
    if (e->sizeY > 1) cJSON_AddNumberToObject(cfg, "sizeY", e->sizeY);
}

// ─── cards_to_cjson ──────────────────────────────────────────────────────────

cJSON* cards_to_cjson(const DashboardCardEntry* e) {
    cJSON* card = cJSON_CreateObject();
    cJSON_AddStringToObject(card, "id",     e->id);
    cJSON_AddStringToObject(card, "type",   card_type_to_str(e->type));
    cJSON_AddNumberToObject(card, "weight", e->weight);

    cJSON* cfg = cJSON_AddObjectToObject(card, "config");
    cJSON_AddStringToObject(cfg, "title", e->title);

    switch (e->type) {
    case CardType::STAT:
        cJSON_AddStringToObject(cfg, "value", e->data.stat.value);
        cJSON_AddStringToObject(cfg, "unit",  e->data.stat.unit);
        cJSON_AddStringToObject(cfg, "color", variant_to_str(e->variant));
        if (e->data.stat.trend[0]) {
            cJSON_AddStringToObject(cfg, "trend",      e->data.stat.trend);
            cJSON_AddStringToObject(cfg, "trendValue", e->data.stat.trendVal);
        }
        break;

    case CardType::GAUGE:
        cJSON_AddNumberToObject(cfg, "value", e->data.gauge.value);
        cJSON_AddNumberToObject(cfg, "min",   e->data.gauge.min);
        cJSON_AddNumberToObject(cfg, "max",   e->data.gauge.max);
        cJSON_AddStringToObject(cfg, "unit",  e->data.gauge.unit);
        {
            cJSON* thr = cJSON_AddObjectToObject(cfg, "thresholds");
            cJSON_AddNumberToObject(thr, "warning", e->data.gauge.warnThresh);
            cJSON_AddNumberToObject(thr, "danger",  e->data.gauge.dangerThresh);
        }
        break;

    case CardType::TOGGLE:
        cJSON_AddStringToObject(cfg, "label", e->data.toggle.label);
        cJSON_AddBoolToObject(cfg, "value",   e->data.toggle.value);
        break;

    case CardType::SLIDER:
        cJSON_AddNumberToObject(cfg, "value", e->data.slider.value);
        cJSON_AddNumberToObject(cfg, "min",   e->data.slider.min);
        cJSON_AddNumberToObject(cfg, "max",   e->data.slider.max);
        cJSON_AddNumberToObject(cfg, "step",  e->data.slider.step);
        cJSON_AddStringToObject(cfg, "unit",  e->data.slider.unit);
        break;

    case CardType::DATA_INPUT:
        cJSON_AddStringToObject(cfg, "value",       e->data.input.value);
        cJSON_AddStringToObject(cfg, "placeholder", e->data.input.placeholder);
        cJSON_AddStringToObject(cfg, "inputType",   e->data.input.inputType);
        cJSON_AddStringToObject(cfg, "unit",        e->data.input.unit);
        if (strcmp(e->data.input.inputType, "number") == 0) {
            cJSON_AddNumberToObject(cfg, "min",  e->data.input.fmin);
            cJSON_AddNumberToObject(cfg, "max",  e->data.input.fmax);
            cJSON_AddNumberToObject(cfg, "step", e->data.input.fstep);
        }
        break;

    case CardType::COLOR: {
        cJSON_AddStringToObject(cfg, "value", e->data.color.value);
        cJSON* presets = cJSON_AddArrayToObject(cfg, "presets");
        for (int i = 0; i < e->data.color.presetCount; i++) {
            cJSON_AddItemToArray(presets, cJSON_CreateString(e->data.color.presets[i]));
        }
        break;
    }

    case CardType::DROPDOWN: {
        cJSON_AddStringToObject(cfg, "value",       e->data.dropdown.value);
        cJSON_AddStringToObject(cfg, "placeholder", e->data.dropdown.placeholder);
        cJSON* opts = cJSON_AddArrayToObject(cfg, "options");
        for (int i = 0; i < e->data.dropdown.optCount; i++) {
            cJSON* opt = cJSON_CreateObject();
            cJSON_AddStringToObject(opt, "value", e->data.dropdown.options[i].value);
            cJSON_AddStringToObject(opt, "label", e->data.dropdown.options[i].label);
            cJSON_AddItemToArray(opts, opt);
        }
        break;
    }

    case CardType::LINK:
        cJSON_AddStringToObject(cfg, "label",   e->data.link.label);
        cJSON_AddStringToObject(cfg, "url",     e->data.link.url);
        cJSON_AddStringToObject(cfg, "target",  e->data.link.target);
        cJSON_AddStringToObject(cfg, "variant", variant_to_str(e->variant));
        break;

    case CardType::ACTION:
        cJSON_AddStringToObject(cfg, "label",          e->data.action.label);
        cJSON_AddStringToObject(cfg, "confirmTitle",   e->data.action.confirmTitle);
        cJSON_AddStringToObject(cfg, "confirmMessage", e->data.action.confirmMsg);
        cJSON_AddStringToObject(cfg, "variant",        variant_to_str(e->variant));
        break;

    case CardType::BUTTON:
        cJSON_AddStringToObject(cfg, "label",   e->data.action.label);  // BUTTON reuses action.label
        cJSON_AddStringToObject(cfg, "variant", variant_to_str(e->variant));
        break;

    case CardType::CHART: {
        cJSON_AddStringToObject(cfg, "chartType", chart_type_to_str(e->data.chart.chartType));
        cJSON_AddStringToObject(cfg, "color",     variant_to_str(e->variant));
        int ns = e->data.chart.numSeries;
        int mp = e->data.chart.maxPoints;
        if (ns > 0) {
            cJSON* seriesArr = cJSON_AddArrayToObject(cfg, "series");
            for (int s = 0; s < ns; s++) {
                cJSON* so = cJSON_CreateObject();
                cJSON_AddStringToObject(so, "name",  e->data.chart.seriesMeta[s].name);
                cJSON_AddStringToObject(so, "color", e->data.chart.seriesMeta[s].color);
                cJSON* dataArr = cJSON_AddArrayToObject(so, "data");
                int head = e->data.chart.headIdx;
                for (int i = 0; i < mp; i++) {
                    int idx = ((head + i) % mp) + s * mp;
                    cJSON_AddItemToArray(dataArr, cJSON_CreateNumber(e->data.chart.data[idx]));
                }
                cJSON_AddItemToArray(seriesArr, so);
            }
        } else {
            cJSON* dataArr = cJSON_AddArrayToObject(cfg, "data");
            int head = e->data.chart.headIdx;
            for (int i = 0; i < mp; i++) {
                int idx = (head + i) % mp;
                cJSON_AddItemToArray(dataArr, cJSON_CreateNumber(e->data.chart.data[idx]));
            }
        }
        break;
    }

    case CardType::STATUS:
        cJSON_AddStringToObject(cfg, "icon",    icon_to_str(e->data.status.icon));
        cJSON_AddStringToObject(cfg, "variant", variant_to_str(e->variant));
        cJSON_AddStringToObject(cfg, "label",   e->data.status.label);
        cJSON_AddStringToObject(cfg, "message", e->data.status.message);
        break;

    case CardType::DATE:
        cJSON_AddStringToObject(cfg, "value",       e->data.date_field.value);
        cJSON_AddStringToObject(cfg, "label",       e->data.date_field.label);
        cJSON_AddBoolToObject(cfg,   "includeTime", e->data.date_field.includeTime);
        if (e->data.date_field.minDate[0])
            cJSON_AddStringToObject(cfg, "min", e->data.date_field.minDate);
        if (e->data.date_field.maxDate[0])
            cJSON_AddStringToObject(cfg, "max", e->data.date_field.maxDate);
        break;

    case CardType::TIME:
        cJSON_AddStringToObject(cfg, "value",          e->data.time_field.value);
        cJSON_AddBoolToObject(cfg,   "includeSeconds", e->data.time_field.includeSeconds);
        break;

    case CardType::LOCATION:
        cJSON_AddStringToObject(cfg, "label",     e->data.location.label);
        cJSON_AddNumberToObject(cfg, "latitude",  e->data.location.latitude);
        cJSON_AddNumberToObject(cfg, "longitude", e->data.location.longitude);
        cJSON_AddStringToObject(cfg, "variant",   variant_to_str(e->variant));
        break;

    case CardType::TIMEZONE:
        cJSON_AddStringToObject(cfg, "value",   e->data.timezone.value);
        cJSON_AddStringToObject(cfg, "label",   e->data.timezone.label);
        cJSON_AddStringToObject(cfg, "variant", variant_to_str(e->variant));
        break;

    case CardType::CONSOLE:
        cJSON_AddBoolToObject(cfg, "autoScroll", true);
        cJSON_AddItemToObject(cfg, "logs", cJSON_CreateArray());
        break;

    case CardType::OTA:
        cJSON_AddNumberToObject(cfg, "maxSize", 4);
        break;

    default:
        break;
    }

    _add_size(cfg, e);
    return card;
}
