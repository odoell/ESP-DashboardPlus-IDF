#pragma once
#include <stdint.h>
#include <stddef.h>

// Start an OTA update. fileSize is the total firmware binary size.
void ota_begin(size_t fileSize);

// Write a decoded chunk of firmware data.
void ota_write(const uint8_t* data, size_t len);

// Finalize and reboot. Aborts if received != expected size.
void ota_end(size_t expectedSize, size_t receivedSize);

// Abort any in-progress OTA.
void ota_abort();

// Returns true if an OTA update is in progress.
bool ota_in_progress();
