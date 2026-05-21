#include "dashboard_ota.h"
#include <string.h>
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "dashboard_ota";

static esp_ota_handle_t       s_ota_handle    = 0;
static const esp_partition_t* s_ota_partition = nullptr;
static bool                   s_in_progress   = false;

bool ota_in_progress() {
    return s_in_progress;
}

void ota_begin(size_t fileSize) {
    if (s_in_progress) {
        ESP_LOGW(TAG, "ota_begin called while OTA already in progress — aborting previous");
        ota_abort();
    }

    s_ota_partition = esp_ota_get_next_update_partition(nullptr);
    if (!s_ota_partition) {
        ESP_LOGE(TAG, "No OTA partition available");
        return;
    }

    esp_err_t err = esp_ota_begin(s_ota_partition, fileSize, &s_ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        s_ota_handle    = 0;
        s_ota_partition = nullptr;
        return;
    }

    s_in_progress = true;
    ESP_LOGI(TAG, "OTA started, expected size: %u bytes", (unsigned)fileSize);
}

void ota_write(const uint8_t* data, size_t len) {
    if (!s_in_progress || !s_ota_handle) {
        ESP_LOGW(TAG, "ota_write called but OTA not in progress");
        return;
    }

    esp_err_t err = esp_ota_write(s_ota_handle, data, len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
        ota_abort();
    }
}

void ota_end(size_t expectedSize, size_t receivedSize) {
    if (!s_in_progress || !s_ota_handle) {
        ESP_LOGW(TAG, "ota_end called but OTA not in progress");
        return;
    }

    if (receivedSize != expectedSize) {
        ESP_LOGE(TAG, "OTA size mismatch: expected %u, received %u",
                 (unsigned)expectedSize, (unsigned)receivedSize);
        ota_abort();
        return;
    }

    esp_err_t err = esp_ota_end(s_ota_handle);
    s_ota_handle  = 0;
    s_in_progress = false;

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        return;
    }

    err = esp_ota_set_boot_partition(s_ota_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "OTA complete — restarting in 1 s");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

void ota_abort() {
    if (s_ota_handle) {
        esp_ota_abort(s_ota_handle);
        s_ota_handle = 0;
    }
    s_ota_partition = nullptr;
    s_in_progress   = false;
    ESP_LOGI(TAG, "OTA aborted");
}
