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
before a receive buffer is allocated.

## 📖 Documentation

Full documentation is available at: **[https://aaronbeckmann.github.io/ESP-DashboardPlus](https://aaronbeckmann.github.io/ESP-DashboardPlus)**
