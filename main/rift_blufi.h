// rift_blufi.h — RIFT BOARD Wi-Fi provisioning service. BLUFI over NimBLE:
// the EspBlufi/ESP Config phone apps deliver STA credentials; they persist
// in Wi-Fi STORAGE_FLASH across reboots. On-device provisioning (scan list
// + keyboard) goes through the same service via apply_credentials/scan, so
// both paths share one state machine. The service starts once at boot and
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

// One scanned access point, sorted by descending RSSI (see scan_result).
typedef struct {
    char ssid[33];
    int rssi;
    bool auth;                // any auth mode beyond open
} rift_blufi_ap_t;

// Boots Wi-Fi STA + NimBLE/BLUFI. Safe to call once from the app boot.
// Callback fires from the ESP event task when the station gets an IP
// (also once per re-connect); the app must only enqueue from it.
typedef void (*rift_blufi_on_ip_cb)(void *user);
esp_err_t rift_blufi_start(rift_blufi_on_ip_cb on_ip, void *user);

// Async service events for the on-device provisioning UI, raised from the
// ESP event task: the app must only enqueue from the callback.
typedef enum {
    RIFT_BLUFI_EV_SCAN_DONE = 0,   // scan_result() now holds fresh records
    RIFT_BLUFI_EV_CONN_FAIL,       // a connect attempt ended without IP
} rift_blufi_event_t;
typedef void (*rift_blufi_on_event_cb)(rift_blufi_event_t event, void *user);
void rift_blufi_set_event_cb(rift_blufi_on_event_cb cb, void *user);

rift_blufi_state_t rift_blufi_state(void);
bool rift_blufi_wifi_ok(void);
// SSID/IP as NUL-terminated strings into fixed buffers; returns false when
// unavailable. Never logs or returns the password.
bool rift_blufi_wifi_info(char *ssid, int ssid_cap, char *ip, int ip_cap);
// On-device provisioning: store and connect with typed credentials. The
// config persists (STORAGE_FLASH), so a reboot reconnects on its own.
// Rejects SSID >= 32 or password >= 64 bytes with ESP_ERR_INVALID_SIZE.
esp_err_t rift_blufi_apply_credentials(const char *ssid, const char *pass);
// Async station scan; results land in scan_result() when EV_SCAN_DONE
// fires. Fails with the underlying esp_wifi error (busy while connecting).
esp_err_t rift_blufi_scan_start(void);
// Copies up to max scanned APs (strongest first) into out; returns count.
int rift_blufi_scan_result(rift_blufi_ap_t *out, int max);
// Clears stored credentials and reconnects as provisioning-ready.
void rift_blufi_clear_credentials(void);
// Re-issues a station connect attempt with the stored credentials
// (data-page "retry" for a dropped link).
void rift_blufi_reconnect(void);
