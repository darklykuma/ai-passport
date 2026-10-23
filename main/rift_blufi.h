// rift_blufi.h — RIFT BOARD Wi-Fi provisioning service. BLUFI over NimBLE:
// the EspBlufi/ESP Config phone apps deliver STA credentials; they persist
// in Wi-Fi STORAGE_FLASH across reboots. The service starts once at boot and
// stays resident; the app polls the state from its own task.
#pragma once

#include <stdbool.h>

#include "esp_err.h"

typedef enum {
    RIFT_BLUFI_OFF = 0,
    RIFT_BLUFI_STARTING,
    RIFT_BLUFI_ADVERTISING,   // ready for the phone app
    RIFT_BLUFI_BLE_CONNECTED,
    RIFT_BLUFI_WIFI_CONNECTING,
    RIFT_BLUFI_WIFI_CONNECTED,
    RIFT_BLUFI_FAILED,
} rift_blufi_state_t;

// Boots Wi-Fi STA + NimBLE/BLUFI. Safe to call once from the app boot.
// Callback fires from the ESP event task when the station gets an IP
// (also once per re-connect); the app must only enqueue from it.
typedef void (*rift_blufi_on_ip_cb)(void *user);
esp_err_t rift_blufi_start(rift_blufi_on_ip_cb on_ip, void *user);

rift_blufi_state_t rift_blufi_state(void);
bool rift_blufi_wifi_ok(void);
// SSID/IP as NUL-terminated strings into fixed buffers; returns false when
// unavailable. Never logs or returns the password.
bool rift_blufi_wifi_info(char *ssid, int ssid_cap, char *ip, int ip_cap);
// Clears stored credentials and reconnects as provisioning-ready.
void rift_blufi_clear_credentials(void);
// Re-issues a station connect attempt with the stored credentials
// (data-page "retry" for a dropped link).
void rift_blufi_reconnect(void);
