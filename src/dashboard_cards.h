#pragma once
#include "esp_dashboard_plus.h"
#include "cJSON.h"

// Initialize card pool — called by ESPDashboardPlus::begin()
void cards_pool_init(DashboardCardEntry* pool, size_t maxCards);

// Locate an unused slot; returns nullptr if pool full
DashboardCardEntry* cards_alloc_slot(DashboardCardEntry* pool, size_t maxCards);

// Find a card by id; returns nullptr if not found
DashboardCardEntry* cards_find_by_id(DashboardCardEntry* pool, size_t maxCards, const char* id);

// Serialize one card entry to a cJSON object (caller owns the returned object)
cJSON* cards_to_cjson(const DashboardCardEntry* entry);

// Free heap memory held by a card entry (chart data, dropdown options)
void cards_free_entry(DashboardCardEntry* entry);

// Helper: CardVariant → string
const char* variant_to_str(CardVariant v);

// Helper: StatusIcon → string
const char* icon_to_str(StatusIcon i);

// Helper: ChartType → string
const char* chart_type_to_str(ChartType t);

// Helper: CardType → string
const char* card_type_to_str(CardType t);

// Helper: LogLevel → string
const char* log_level_to_str(LogLevel l);

// Constrain val to [lo, hi]
template<typename T>
static inline T dashboard_constrain(T val, T lo, T hi) {
    return val < lo ? lo : (val > hi ? hi : val);
}
