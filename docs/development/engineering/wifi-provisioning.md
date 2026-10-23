<p align="right">
  <a href="wifi-provisioning.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Wi-Fi Connection via On-Device and Bluetooth Provisioning

When a user needs the device to connect to a Wi-Fi network, an application can
provision from the device itself or over Bluetooth.

- **On-device provisioning** (RIFT BOARD): the data page opens a provisioning
  menu that scans for nearby access points, lets the user pick one with the
  three buttons, and enters the password on an on-screen keyboard
  (`main/rift_kb.c` model + `rift_blufi_apply_credentials`). Open networks
  connect without the password step. Both paths write the same persistent
  station config, so a reboot reconnects on its own.
- **Bluetooth provisioning** ([`demo/blufi-provisioning` branch](https://github.com/FoloToy/ai-passport/tree/demo/blufi-provisioning)):
  the phone supplies the SSID and password over BLE using BLUFI; the device
  connects as a Wi-Fi station and reports connection status. Bluetooth carries
  the provisioning exchange, not the application's Internet traffic. The RIFT
  BOARD keeps this service resident as the alternative path.

Provisioning is an optional application capability. Do not enable networking
for applications that do not need it.

## On-device flow (RIFT BOARD)

Data page → Wi-Fi row → provisioning menu: scan list, manual SSID entry
(for hidden networks), stored-credential retry, or the Bluetooth card. The
keyboard is a three-key linear picker: UP/DOWN move the selection (hold
repeats), OK activates; a special row carries space, delete, mode
(lowercase/uppercase/symbols), and done. Passwords echo as asterisks. A failed
attempt returns to the keyboard with a retry message; a successful one lands
back on the data page showing the state from `wifi_line()`. Clearing saved
credentials stays on the data page Wi-Fi row while connected. The keyboard
model is pure C covered by `tests/test_rift_kb.c`; never log the password.

## Mini program name

Use the companion mini program identified by the
[exact Chinese search name](wifi-provisioning.zh_CN.md#mini-program-name).
Keep that name unchanged in setup instructions rather than inventing a
translated search name. It is the mini program's name, not the device's BLE
advertising name. The reference firmware advertises as `BLUFI_FoloPassport`.

The intended flow is to open provisioning on the device, enable Bluetooth and
the required permissions on the phone, open the mini program, select the target
device, and provide a 2.4 GHz Wi-Fi network's credentials. Confirm that the
device obtains an IP address, then test the application's actual network
request; a successful BLE connection alone does not prove Internet access.

## Code to inspect

- [`main/demo_blufi.c`](https://github.com/FoloToy/ai-passport/blob/demo/blufi-provisioning/main/demo_blufi.c): BLUFI callbacks, Wi-Fi scanning/connection, credential handling, status reporting, and lifecycle.
- [`main/demo_blufi_security.c`](https://github.com/FoloToy/ai-passport/blob/demo/blufi-provisioning/main/demo_blufi_security.c) and its header: BLUFI security negotiation callbacks; do not discard them when extracting the example.
- [`main/demo_radio.c`](https://github.com/FoloToy/ai-passport/blob/demo/blufi-provisioning/main/demo_radio.c): shared initialization of NVS, `esp_netif`, and the default event loop.
- [`main/CMakeLists.txt`](https://github.com/FoloToy/ai-passport/blob/demo/blufi-provisioning/main/CMakeLists.txt) and [`sdkconfig.defaults`](https://github.com/FoloToy/ai-passport/blob/demo/blufi-provisioning/sdkconfig.defaults): source registration, dependencies, NimBLE/BLUFI, and cryptographic configuration. Copying a single C file is not sufficient.

## Integration and acceptance

Start from the application's current baseline, record the reference commit,
and adapt only the relevant networking logic. Do not merge the entire demo or
overwrite current BSP, partitions, or configuration with the branch's older
versions. Implement the application's own UI and keep provisioning state,
tasks, and Wi-Fi/BLE services in the application layer.

Define provisioning entry/exit, timeouts, retry limits, credential persistence,
and an explicit way to clear saved credentials. Never log or commit Wi-Fi
passwords. Preserve LVGL locking, non-blocking callbacks, and cleanup of tasks
and event handlers; budget internal RAM for concurrent Wi-Fi, BLE, and UI use.
The example is not a complete production authorization or security design.

Run the [validation gate](build-and-test.md), then follow the
[on-device testing handoff](../ai-guide.md#offer-on-device-testing). Test with
the named mini program: discovery, successful provisioning, wrong-password and
unavailable-network failures, reconnect/restart behavior, credential clearing,
repeated entry/exit, and an actual application network request. Build success
does not prove mini program compatibility or on-device networking; keep
unperformed checks under `Unverified`.
