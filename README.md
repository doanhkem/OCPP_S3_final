# OCPP S3 Charger

Firmware for an ESP32-S3 EV charger using MicroOcpp 1.2.0, OCPP 1.6, an
ENC28J60 Ethernet module or Wi-Fi, and an STM32 power meter controller.

## Hardware

Current pin mapping:

| Function | Pin |
|---|---:|
| STM32 UART RX | GPIO44 |
| STM32 UART TX | GPIO43 |
| ENC28J60 SCK | GPIO40 |
| ENC28J60 MISO | GPIO41 |
| ENC28J60 MOSI | GPIO42 |
| ENC28J60 CS | GPIO39 |
| WS2812 LED | GPIO16 |
| Relay outputs | GPIO3, 46, 9, 10, 11, 12, 13, 14 |

STM32 UART is configured for `115200 8-N-1`.

## Connector count

The physical connector count is declared in `src/main.cpp`:

```cpp
#define NUM_CONNECTOR 4
```

The current firmware has four physical connectors, with IDs `1` to `4`.

MicroOcpp also reserves connector ID `0` for the charge point, so
`MO_NUMCONNECTORS` in `platformio.ini` must be the physical connector count
plus one:

```ini
-DMO_NUMCONNECTORS=5
```

For a different number of physical connectors, update both values:

```text
NUM_CONNECTOR = N
MO_NUMCONNECTORS = N + 1
```

Also check the following when changing `N`:

- `ItemPower::item[]` channel and message mappings in `src/main.cpp`.
- Relay pin order in the `outputs[]` array.
- STM32 channel mapping.
- The available number of relay and meter channels.

## First startup and configuration

The charger always starts the configuration access point:

```text
SSID:      ZIOT Charger Config
Password:  88888888
Address:   http://192.168.4.1
```

Web login:

```text
Username: admin
Password: Ziot@123
```

The web page can configure:

- Wi-Fi SSID and password.
- Wi-Fi scan.
- Ethernet DHCP or static IP.
- OCPP host, port and base path.
- Charge Box ID.

The AP remains available while the configured network and OCPP are running.
When Ethernet DHCP is unavailable, DHCP runs in a separate task so the AP
web page remains responsive.

After saving, the ESP32 restarts and automatically uses the saved network
configuration on later startups.

## OCPP configuration

The final WebSocket path is built as:

```text
OCPP base path + "/" + Charge Box ID
```

Example:

```text
ws://ocpp-1.rabbitevc.vn:80/steve/websocket/CentralSystemService/1
```

The current connection implementation uses plain `ws://`. TLS `wss://` is
not enabled in this firmware.

## Build and upload

Install PlatformIO, then run from the project directory:

```powershell
platformio run -j 1
platformio run --target upload --upload-port COM11 -j 1
platformio device monitor --port COM11 --baud 115200
```

The project uses a stable upload configuration:

```ini
upload_speed = 115200
upload_flags = --no-stub
```

If automatic reset does not enter download mode, hold `BOOT`, press and
release `RESET/EN`, then release `BOOT` and run the upload command again.

## Runtime behavior

- Relay is enabled only when the connector status is `Charging`.
- Relay is disabled for other connector states.
- Meter values are read from STM32: voltage, current, power and energy.
- `meterStart` and `meterStop` use the real STM32 energy register.
- A transaction stops when power remains below `20 W` for `60 seconds`.
- A transaction also stops when current reaches `9000 mA`.
- All relays are initialized OFF at startup.

## Error codes

The firmware reports error information through OCPP `StatusNotification`:

| Code | OCPP errorCode | Condition |
|---|---|---|
| E1001 | OtherError | TCP or OCPP connection timeout |
| E1002 | OtherError | OCPP WebSocket handshake failure |
| E3001 | PowerMeterFailure | No STM32 meter response |
| E3002 | PowerMeterFailure | Invalid STM32 meter frame |
| W3002 | OtherError | Power below 20 W for 60 seconds |
| - | OverCurrentFailure | Current at or above 9000 mA |

Vendor ID is `ZIOT`.

Relay stuck and relay acknowledgement errors are not enabled because the
current hardware interface does not provide relay feedback.

## Serial log examples

```text
[config] AP IP=192.168.4.1
[wifi] connected IP=192.168.1.50
[eth] hardware=ENC28J60
[stm32] meter power=1234W voltage=226V current=5400mA energy=2744Wh
```
