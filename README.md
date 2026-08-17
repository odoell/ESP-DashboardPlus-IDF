# ESP-DashboardPlus IDF

ESP-IDF component for [ESP-DashboardPlus](https://github.com/aaronbeckmann/ESP-DashboardPlus) — a real-time, on-device web dashboard library for ESP32. This repository contains the ESP-IDF port.

For the Arduino IDE version, see [ESP-DashboardPlus-Arduino](https://github.com/aaronbeckmann/ESP-DashboardPlus-Arduino).

## Usage

Clone this repository next to your project and add it as a component via an external wrapper, or place it directly in your project's `components/` folder.

For a complete working example, see [ESP-DashboardPlus-IDF-Example](https://github.com/aaronbeckmann/ESP-DashboardPlus-IDF-Example).

## Configuration

Use `idf.py menuconfig` and open **ESP Dashboard Plus** to configure HTTP or
HTTPS operation, the listening port, simultaneous WebSocket clients, maximum
inbound WebSocket text-frame length, card string length, and dropdown capacity.
Inbound frames larger than `CONFIG_DASHBOARD_MAX_WS_FRAME_LEN` are rejected
before a receive buffer is allocated. The component enables ESP-IDF WebSocket
server support automatically.

OTA and console support are compiled by default for compatibility. Disable
`CONFIG_DASHBOARD_ENABLE_OTA` to omit the OTA implementation and `app_update`
dependency. Disable `CONFIG_DASHBOARD_ENABLE_CONSOLE` to omit inbound console
command handling and outbound console-log serialization. Passing `true` to
`begin()` cannot enable a feature that was excluded at compile time.

Asynchronous WebSocket sends that fail cause the affected HTTP server session
to be closed. This releases stale client slots so a disconnected or
unresponsive browser cannot permanently block later reconnects.

Frame-header and frame-payload receive errors, as well as frames exceeding
`CONFIG_DASHBOARD_MAX_WS_FRAME_LEN`, close the affected session so it cannot
remain in an invalid protocol state.

The server retains ESP-IDF's default seven open client sockets. WebSocket
clients are limited to five so ordinary page and asset requests retain at least
two client slots. Broadcast enumeration covers the complete HTTP server client
pool instead of assuming that only WebSocket clients occupy sockets.

After the WebSocket upgrade, the browser's explicit `init` request triggers the
complete dashboard initialization response for that client. Initialization is
not sent during the upgrade handshake, avoiding a race in which the first frame
could be dropped before the connection became writable.

The generic page remains behind a neutral loading state until that complete
initialization arrives, so default titles, cards, and disabled feature tabs do
not flash before product-specific content is ready. The HTML response uses
`Cache-Control: no-cache` so browsers revalidate embedded UI revisions after a
firmware update. CMake tracks the HTML source and conversion script so an
incremental build custom command regenerates the embedded page when either
changes.

The loading label is delayed by 200 milliseconds, avoiding a flash during fast
initialization while remaining visible when startup actually takes longer. The
initialized dashboard uses a short opacity transition into its complete state.

The browser marks an open WebSocket stale after ten seconds without an inbound
message. This tolerates short scheduling and network delays while retaining
automatic recovery for an unresponsive connection.

## 📖 Documentation

Full documentation is available at: **[https://aaronbeckmann.github.io/ESP-DashboardPlus](https://aaronbeckmann.github.io/ESP-DashboardPlus)**
