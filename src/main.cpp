#include <MicroOcpp.h>
#include <MicroOcpp/Debug.h>
#include <MicroOcpp/Core/Connection.h>
#include <MicroOcpp/Model/Metering/SampledValue.h>
#include <MicroOcpp/Model/ConnectorBase/ChargePointErrorData.h>
#include "NetworkConfig.h"
// #include <SPIFFS.h>
#include <vector>
#include <Adafruit_NeoPixel.h>
#include <ModbusIP_ESP8266.h>
// #include "WiFiConfig.h"
#include <IPAddress.h>
#include <UIPEthernet.h>
#include <Dns.h>
#include <SPI.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define NUM_CONNECTOR 8 ///
#define MAX_TIME_TRY 50  // 20
#define HIGH_LIMIT_POWER 2000
#define HIGH_LIMIT_AMPE 9000
#define LOW_LIMIT_POWER 10
#define LOW_LIMIT_POWER_TIME_TRY 10//25
#define LOW_POWER_STOP_THRESHOLD 20
#define LOW_POWER_STOP_TIME_MS 60000UL
#define FIXED_METER_ENERGY_WH 1000

#define START_OF_MESSAGE 0xA5
#define MAX_ATTEMPTS 5
#define READ_DELAY 100



#define LED_PIN   16   
#define LED_COUNT 8  
#define ETH_SCK_PIN 40
#define ETH_MISO_PIN 41
#define ETH_MOSI_PIN 42
#define ETH_CS_PIN 39
#define STM32_UART_RX_PIN 44
#define STM32_UART_TX_PIN 43
#define STM32_UART_BAUD 115200
#define OUT1 3
#define OUT2 46
#define OUT3 9
#define OUT4 10
#define OUT5 11
#define OUT6 12
#define OUT7 13
#define OUT8 14

byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
HardwareSerial &stm32Serial = Serial1;
const int outputs[] = {OUT1, OUT2, OUT3, OUT4, OUT5, OUT6, OUT7, OUT8};
const int numOutputs = sizeof(outputs) / sizeof(outputs[0]);
bool appReady = false;
NetworkConfig networkConfig;
WiFiClient wifiOcppClient;
EthernetClient ethernetOcppClient;

enum class NetworkErrorState : uint8_t {
    None,
    Timeout,
    HandshakeFailure
};

NetworkErrorState networkErrorState = NetworkErrorState::None;

void setNetworkErrorState(NetworkErrorState error) {
    networkErrorState = error;
}

MicroOcpp::ErrorData getNetworkErrorData() {
    MicroOcpp::ErrorData error(nullptr);
    error.isError = networkErrorState != NetworkErrorState::None;
    error.isFaulted = error.isError;
    error.severity = 2;
    error.errorCode = "OtherError";
    error.vendorId = "ZIOT";

    switch (networkErrorState) {
        case NetworkErrorState::Timeout:
            error.info = "Network or OCPP connection timeout";
            error.vendorErrorCode = "E1001";
            break;
        case NetworkErrorState::HandshakeFailure:
            error.info = "OCPP WebSocket handshake failed";
            error.vendorErrorCode = "E1002";
            break;
        case NetworkErrorState::None:
            error.errorCode = nullptr;
            error.info = nullptr;
            error.vendorId = nullptr;
            error.vendorErrorCode = nullptr;
            break;
    }

    return error;
}

class NetworkOcppConnection : public MicroOcpp::Connection {
private:
    Client &tcp;
    MicroOcpp::ReceiveTXTcallback receiveTXT;
    String host;
    uint16_t port;
    String path;
    String transportName;
    unsigned long lastRecv = 0;
    unsigned long lastConnected = 0;
    unsigned long lastConnectAttempt = 0;
    bool connected = false;

public:
    NetworkOcppConnection(Client &tcp, const String &host, uint16_t port,
            const String &path, const String &transportName)
            : tcp(tcp), host(host), port(port), path(path), transportName(transportName) {}

    void loop() override {
        if (!connected || !tcp.connected()) {
            connected = false;
            if (millis() - lastConnectAttempt >= 5000) {
                lastConnectAttempt = millis();
                connectWebSocket();
            }
            return;
        }

        while (tcp.available() > 0) {
            readFrame();
        }
    }

    bool sendTXT(const char *msg, size_t length) override {
        return sendFrame(0x1, (const uint8_t *) msg, length);
    }

    void setReceiveTXTcallback(MicroOcpp::ReceiveTXTcallback &callback) override {
        receiveTXT = callback;
    }

    unsigned long getLastRecv() override {
        return lastRecv;
    }

    unsigned long getLastConnected() override {
        return lastConnected;
    }

    bool isConnected() override {
        return connected && tcp.connected();
    }

private:
    bool connectWebSocket() {
        MO_DBG_INFO("Connecting over %s ws://%s:%u%s",
                transportName.c_str(), host.c_str(), port, path.c_str());
        tcp.stop();

        if (!tcp.connect(host.c_str(), port)) {
            setNetworkErrorState(NetworkErrorState::Timeout);
            MO_DBG_INFO("TCP connect failed");
            return false;
        }

        tcp.print("GET ");
        tcp.print(path);
        tcp.print(" HTTP/1.1\r\nHost: ");
        tcp.print(host);
        tcp.print(":");
        tcp.print(port);
        tcp.print("\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Protocol: ocpp1.6\r\n\r\n");

        String response;
        unsigned long deadline = millis() + 5000;
        while (millis() < deadline) {
            while (tcp.available()) {
                response += (char) tcp.read();
                if (response.indexOf("\r\n\r\n") >= 0) {
                    if (response.indexOf(" 101 ") >= 0) {
                        setNetworkErrorState(NetworkErrorState::None);
                        connected = true;
                        lastRecv = mocpp_tick_ms();
                        lastConnected = mocpp_tick_ms();
                        MO_DBG_INFO("Connected (path: %s)", path.c_str());
                        return true;
                    }
                    setNetworkErrorState(NetworkErrorState::HandshakeFailure);
                    MO_DBG_WARN("WebSocket handshake failed: %s", response.c_str());
                    tcp.stop();
                    return false;
                }
            }
            delay(10);
        }

        setNetworkErrorState(NetworkErrorState::HandshakeFailure);
        MO_DBG_WARN("WebSocket handshake timeout");
        tcp.stop();
        return false;
    }

    bool readExact(uint8_t *buffer, size_t length, unsigned long timeoutMs = 1000) {
        size_t offset = 0;
        unsigned long deadline = millis() + timeoutMs;
        while (offset < length && millis() < deadline) {
            if (tcp.available()) {
                int value = tcp.read();
                if (value >= 0) {
                    buffer[offset++] = (uint8_t) value;
                }
            } else {
                delay(1);
            }
        }
        return offset == length;
    }

    void readFrame() {
        uint8_t header[2];
        if (!readExact(header, sizeof(header), 100)) {
            return;
        }

        uint8_t opcode = header[0] & 0x0F;
        bool masked = header[1] & 0x80;
        uint64_t length = header[1] & 0x7F;

        if (length == 126) {
            uint8_t ext[2];
            if (!readExact(ext, sizeof(ext))) return;
            length = ((uint16_t) ext[0] << 8) | ext[1];
        } else if (length == 127) {
            uint8_t ext[8];
            if (!readExact(ext, sizeof(ext))) return;
            length = 0;
            for (uint8_t i = 0; i < 8; i++) {
                length = (length << 8) | ext[i];
            }
        }

        uint8_t mask[4] = {0, 0, 0, 0};
        if (masked && !readExact(mask, sizeof(mask))) {
            return;
        }

        if (length > 4096) {
            MO_DBG_WARN("WebSocket frame too large: %u", (unsigned int) length);
            for (uint64_t i = 0; i < length; i++) {
                uint8_t discard;
                if (!readExact(&discard, 1)) break;
            }
            return;
        }

        size_t payloadLength = (size_t) length;
        char *payload = (char *) malloc(payloadLength + 1);
        if (!payload) {
            MO_DBG_WARN("WebSocket payload allocation failed");
            return;
        }

        if (!readExact((uint8_t *) payload, payloadLength)) {
            free(payload);
            return;
        }

        for (size_t i = 0; masked && i < payloadLength; i++) {
            payload[i] ^= mask[i % 4];
        }
        payload[payloadLength] = '\0';

        switch (opcode) {
            case 0x1:
                if (receiveTXT && receiveTXT(payload, payloadLength)) {
                    lastRecv = mocpp_tick_ms();
                } else {
                    MO_DBG_WARN("Processing WebSocket input event failed");
                }
                break;
            case 0x8:
                MO_DBG_INFO("Disconnected");
                setNetworkErrorState(NetworkErrorState::Timeout);
                connected = false;
                tcp.stop();
                break;
            case 0x9:
                sendFrame(0xA, (const uint8_t *) payload, length);
                lastRecv = mocpp_tick_ms();
                break;
            case 0xA:
                lastRecv = mocpp_tick_ms();
                break;
            default:
                MO_DBG_WARN("Unsupported WebSocket opcode");
                break;
        }

        free(payload);
    }

    bool sendFrame(uint8_t opcode, const uint8_t *payload, size_t length) {
        if (!connected || !tcp.connected()) {
            return false;
        }

        uint8_t header[14];
        size_t headerLength = 0;
        header[headerLength++] = 0x80 | (opcode & 0x0F);

        if (length <= 125) {
            header[headerLength++] = 0x80 | (uint8_t) length;
        } else if (length <= 65535) {
            header[headerLength++] = 0x80 | 126;
            header[headerLength++] = (length >> 8) & 0xFF;
            header[headerLength++] = length & 0xFF;
        } else {
            header[headerLength++] = 0x80 | 127;
            for (int8_t shift = 56; shift >= 0; shift -= 8) {
                header[headerLength++] = ((uint64_t) length >> shift) & 0xFF;
            }
        }

        uint8_t mask[4] = {0x12, 0x34, 0x56, 0x78};
        memcpy(&header[headerLength], mask, sizeof(mask));
        headerLength += sizeof(mask);

        if (tcp.write(header, headerLength) != headerLength) {
            return false;
        }

        uint8_t buffer[128];
        for (size_t offset = 0; offset < length; ) {
            size_t remaining = length - offset;
            size_t chunk = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
            for (size_t i = 0; i < chunk; i++) {
                buffer[i] = payload[offset + i] ^ mask[(offset + i) % 4];
            }
            if (tcp.write(buffer, chunk) != chunk) {
                return false;
            }
            offset += chunk;
        }

        return true;
    }
};

std::unique_ptr<NetworkOcppConnection> ocppConnection;

#define STASSID "SOLARZ-PKT"
#define STAPSK "88888888"
using namespace std;

uint8_t cnt[9], counter[9], counting[9]; // cần thay đổi 
unsigned long lowPowerStartMs[9] = {0};

namespace Helper {
    template <class T, class V>
    T castType(V number) {
        if (number < 0) {
            number *= -1;
        }
        return static_cast<T>(isnan(number) || isnanf(number) ? 0 : number);
    }

    uint8_t generateId(uint8_t id) {
        return id -1;
    }
};


template <class T = float>
struct ConnectorState {

    T previousPower, currentPower, prevEnergy, currentEnergy, prevAmpe, currentAmpe;

    bool isActivated;
    shared_ptr<MicroOcpp::Transaction> txn;
    ConnectorState() : previousPower(0.f), currentPower(0.f),
                       prevEnergy(0.f), currentEnergy(0.f),
                       prevAmpe(0.f), currentAmpe(0.f),
                       isActivated(false) {};

    void setTransaction(std::shared_ptr<MicroOcpp::Transaction> tx) { this->txn = tx; }

    void setPreviousPower(T previous) { this->previousPower = previous; }

    void setCurrentPower(T current) { this->currentPower = current; }

    void setPreviousEnergy(T previous) { this->prevEnergy = previous; }

    void setCurrentEnergy(T current) { this->currentEnergy = current; }

    void setPreviousAmpe(T previous) { this->prevAmpe = previous; }

    void setCurrentAmpe(T current) { this->currentAmpe = current; }

    void setActivated() { this->isActivated = true; }

    void deActivated() { this->isActivated = false; }

    bool getStatus() {
        return this->isActivated;
    }

    // PREVIUOS POWER
    T getPreviousPower() const { return Helper::castType<T>(previousPower); }
    // CURRENT POWER
    T getCurrentPower() const { return Helper::castType<T>(currentPower); }
    // PREVIOUS ENERGY
    T getPreviousEnergy() const { return Helper::castType<T>(prevEnergy); }
    // CURRENT ENERGY
    T getCurrentEnergy() const { return Helper::castType<T>(currentEnergy); }
    // PREVVIOUS AMPE
    T getPreviousAmpe() const { return Helper::castType<T>(prevAmpe); }
    // GET CURRENT AMPE
    T getCurrentAmpe() const { return Helper::castType<T>(currentAmpe); }
};

namespace ItemPower {
    struct Item {
        uint8_t channel, messageId;
    };

    Item item[] = {{0x00, 0x0000}, {0x01, 0x0001}, {0x02, 0x0002}, {0x03, 0x0003}, {0x04, 0x0004}, {0x05, 0x0005}, {0x06, 0x0006}, {0x07, 0x0007}, {0x08, 0x0008}};
    Item getInfo(int id) {
        return item[id];
    }
};
namespace PowerService {
    enum class MeterReadStatus : uint8_t {
        Unknown,
        Ok,
        NoResponse,
        InvalidFrame
    };

    struct PowerInfo {
        uint16_t voltage;
        uint16_t current;
        uint16_t power;
        uint32_t energy;
    };

    MeterReadStatus lastReadStatus[10] = {};

    uint8_t channelControlState = 0x00;

    uint16_t calcrc(const uint8_t *ptr, size_t count) {
        uint16_t crc = 0;
        while (count--) {
            crc ^= (*ptr++ << 8);
            for (uint8_t i = 0; i < 8; i++) {
                crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
            }
        }
        return crc;
    }

    void decodeData(const uint8_t *buffer, PowerInfo &info) {
        info.voltage = (buffer[6] << 8) | buffer[7];
        info.current = (buffer[8] << 8) | buffer[9];
        info.power = (buffer[10] << 8) | buffer[11];
        info.energy = (buffer[12] << 24) | (buffer[13] << 16) | (buffer[14] << 8) | buffer[15];
        Serial.print("[stm32] meter power=");
        Serial.print(info.power);
        Serial.print("W voltage=");
        Serial.print(info.voltage);
        Serial.print("V current=");
        Serial.print(info.current);
        Serial.print("mA energy=");
        Serial.print(info.energy);
        Serial.println("Wh");

    }

    void sendMessage(uint16_t messageID, uint16_t eventCode, const uint8_t *eventPayload, size_t payloadLength) {
        uint8_t message[255];

        message[0] = START_OF_MESSAGE;
        message[1] = 2 + 2 + payloadLength; // Byte độ dài
        message[2] = highByte(messageID);
        message[3] = lowByte(messageID);
        message[4] = highByte(eventCode);
        message[5] = lowByte(eventCode);

        memcpy(&message[6], eventPayload, payloadLength);

        uint16_t checksum = calcrc(message, 6 + payloadLength);
        message[6 + payloadLength] = highByte(checksum);
        message[7 + payloadLength] = lowByte(checksum);

        stm32Serial.write(message, 8 + payloadLength);
    }

    PowerInfo sendAndReceiveDataToStm32(uint16_t messageID, uint16_t eventCode, const uint8_t *eventPayload, size_t payloadLength, PowerInfo &oldInfo) {
        PowerInfo newInfo = {0, 0, 0, 0};
        uint8_t channel = payloadLength > 0 ? eventPayload[0] : 0;
        bool invalidFrame = false;
        uint8_t buffer[2048];
        size_t bytesRead = 0;
        size_t expectedLength = 0;
        lastReadStatus[channel] = MeterReadStatus::NoResponse;

        for (int attempts = 0; attempts < MAX_ATTEMPTS; attempts++) {
            sendMessage(messageID, eventCode, eventPayload, payloadLength);

            unsigned long startTime = millis();
            while (millis() - startTime < 500) { // Timeout 500ms
                if (stm32Serial.available() > 0) {
                    uint8_t byte = stm32Serial.read();

                    if (bytesRead == 0 && byte == START_OF_MESSAGE) {
                        buffer[bytesRead++] = byte;
                    }
                    else if (bytesRead == 1) {
                        buffer[bytesRead++] = byte;
                        expectedLength = byte + 4;
                    }
                    else if (bytesRead > 1) {
                        buffer[bytesRead++] = byte;

                        if (expectedLength > sizeof(buffer)) {
                            bytesRead = 0;
                            expectedLength = 0;
                            continue;
                        }

                        if (bytesRead == expectedLength) {
                            uint16_t receivedChecksum = (buffer[expectedLength - 2] << 8) | buffer[expectedLength - 1];
                            uint16_t calculatedChecksum = calcrc(buffer, expectedLength - 2);

                            uint16_t receivedMessageID = (buffer[2] << 8) | buffer[3];
                            if (receivedChecksum == calculatedChecksum && receivedMessageID == messageID && buffer[5] == 0x03) {
                                decodeData(buffer, newInfo);
                                lastReadStatus[channel] = MeterReadStatus::Ok;
                                return newInfo; // Trả về thông tin điện năng mới nếu phản hồi hợp lệ
                            }

                            invalidFrame = true;
                            bytesRead = 0;
                            expectedLength = 0;
                        }
                    }
                }
            }
            delay(100);
        }
        if (invalidFrame) {
            lastReadStatus[channel] = MeterReadStatus::InvalidFrame;
        }
        static unsigned long lastWarnMs = 0;
        if (millis() - lastWarnMs > 5000) {
            lastWarnMs = millis();
            Serial.print("[stm32] no meter response msg=0x");
            Serial.print(messageID, HEX);
            Serial.print(" event=0x");
            Serial.print(eventCode, HEX);
            Serial.print(" channel=");
            Serial.print(payloadLength > 0 ? eventPayload[0] : 0);
            Serial.print(" rx=");
            Serial.print(STM32_UART_RX_PIN);
            Serial.print(" tx=");
            Serial.println(STM32_UART_TX_PIN);
        }
        return oldInfo; // Trả về thông tin cũ nếu không nhận được phản hồi hợp lệ
    }

    void setChannelControl(uint8_t bitPosition, bool value) {

        digitalWrite(outputs[7-bitPosition], value);
        if (value == 1) {
            strip.setPixelColor(bitPosition, strip.Color(0, 0, 255));
        }
        else {
            strip.setPixelColor(bitPosition, strip.Color(0, 255, 0));
        }
        strip.show();
    }

    PowerInfo getPowerInfo(uint8_t channel, uint16_t messageID, PowerInfo &oldInfo) {
        uint8_t payload[] = {channel};
        return sendAndReceiveDataToStm32(messageID, 0x0003, payload, sizeof(payload), oldInfo);
    }

    MeterReadStatus getLastReadStatus(uint8_t channel) {
        return lastReadStatus[channel];
    }
};


PowerService::PowerInfo powerChannel[10] = {{0, 0, 0, 0},            // kac
                                            {0, 0, 0, 0},
                                            {0, 0, 0, 0},
                                            {0, 0, 0, 0},
                                            {0, 0, 0, 0},
                                            {0, 0, 0, 0},
                                            {0, 0, 0, 0},
                                            {0, 0, 0, 0},
                                            {0, 0, 0, 0}};

enum class ConnectorErrorState : uint8_t {
    None,
    MeterNoResponse,
    MeterInvalidFrame,
    OverCurrent,
    LowPowerTimeout
};

ConnectorErrorState connectorErrorState[10] = {};

void clearMeterError(uint8_t connectorId) {
    if (connectorErrorState[connectorId] == ConnectorErrorState::MeterNoResponse ||
            connectorErrorState[connectorId] == ConnectorErrorState::MeterInvalidFrame) {
        connectorErrorState[connectorId] = ConnectorErrorState::None;
    }
}

void updateMeterError(uint8_t connectorId, uint8_t channel) {
    switch (PowerService::getLastReadStatus(channel)) {
        case PowerService::MeterReadStatus::NoResponse:
            connectorErrorState[connectorId] = ConnectorErrorState::MeterNoResponse;
            break;
        case PowerService::MeterReadStatus::InvalidFrame:
            connectorErrorState[connectorId] = ConnectorErrorState::MeterInvalidFrame;
            break;
        case PowerService::MeterReadStatus::Ok:
            clearMeterError(connectorId);
            break;
        case PowerService::MeterReadStatus::Unknown:
            break;
    }
}

MicroOcpp::ErrorData getConnectorErrorData(uint8_t connectorId) {
    MicroOcpp::ErrorData error(nullptr);
    error.vendorId = "ZIOT";

    switch (connectorErrorState[connectorId]) {
        case ConnectorErrorState::MeterNoResponse:
            error.isError = true;
            error.isFaulted = true;
            error.severity = 2;
            error.errorCode = "PowerMeterFailure";
            error.info = "No response from STM32 power meter";
            error.vendorErrorCode = "E3001";
            break;
        case ConnectorErrorState::MeterInvalidFrame:
            error.isError = true;
            error.isFaulted = true;
            error.severity = 2;
            error.errorCode = "PowerMeterFailure";
            error.info = "Invalid STM32 power meter frame";
            error.vendorErrorCode = "E3002";
            break;
        case ConnectorErrorState::OverCurrent:
            error.isError = true;
            error.isFaulted = true;
            error.severity = 3;
            error.errorCode = "OverCurrentFailure";
            error.info = "Charging current exceeded the configured limit";
            error.vendorId = nullptr;
            break;
        case ConnectorErrorState::LowPowerTimeout:
            error.isError = true;
            error.isFaulted = false;
            error.severity = 1;
            error.errorCode = "OtherError";
            error.info = "Charging power stayed below 20 W for 60 seconds";
            error.vendorErrorCode = "W3002";
            break;
        case ConnectorErrorState::None:
            error.isError = false;
            error.isFaulted = false;
            error.errorCode = nullptr;
            error.info = nullptr;
            error.vendorId = nullptr;
            error.vendorErrorCode = nullptr;
            break;
    }

    return error;
}


class Connector;

vector<Connector *> connector;
vector<ConnectorState<> *> state(10);

class Connector {
private:
    uint8_t itemId;

public:
    Connector() = default;

    Connector(uint8_t id) { this->itemId = id; }

    uint8_t getItemId() {
        return this->itemId;
    }

    void runInstance() {

        // Energy.Active.Import.Register sampler. Registered through the low level
        // addMeterValueInput() overload (instead of setEnergyMeterInput) so the
        // ReadingContext is available to us and meterStart / meterStop can be
        // overridden without touching the MicroOcpp library.
        {
            uint8_t id = this->getItemId();

            MicroOcpp::SampledValueProperties props;
            props.setMeasurand("Energy.Active.Import.Register");
            props.setUnit("Wh");

            auto sampler = std::unique_ptr<MicroOcpp::SampledValueSamplerConcrete<int32_t, MicroOcpp::SampledValueDeSerializer<int32_t>>>(
                    new MicroOcpp::SampledValueSamplerConcrete<int32_t, MicroOcpp::SampledValueDeSerializer<int32_t>>(
                            props,
                            [id](ReadingContext ctx) -> int32_t {
                                auto tx = getTransaction(id);
                                if (tx) {
                                    bool isRunning = (tx->isActive() and !tx->isRunning()) or (tx->isActive() and tx->isRunning());
                                    bool isStop = !tx->isActive() and tx->isRunning();
                                    if (isRunning) {
                                        if (!state[id]->getStatus()) {
                                            if (connectorErrorState[id] == ConnectorErrorState::LowPowerTimeout ||
                                                    connectorErrorState[id] == ConnectorErrorState::OverCurrent) {
                                                connectorErrorState[id] = ConnectorErrorState::None;
                                            }
                                            (state[id])->setActivated();
                                            (state[id])->setTransaction(tx);
                                        }
                                    }
                                    else if (isStop) {
                                        (state[id])->deActivated();
                                        PowerService::setChannelControl(Helper::generateId(id), 0);
                                    }
                                }
                                delay(100);

                                auto item = ItemPower::getInfo(id);
                                PowerService::PowerInfo power = PowerService::getPowerInfo(item.channel, item.messageId,
                                                                                           powerChannel[id]);
                                updateMeterError(id, item.channel);
                                // Keep P/U/I from STM32 while reporting a fixed 1 kWh energy value.
                                (void) power;
                                return FIXED_METER_ENERGY_WH;
                            }));

            addMeterValueInput(std::move(sampler), id);
        }

        setPowerMeterInput([&]() -> float {
            auto item = ItemPower::getInfo(this->getItemId());
            PowerService::PowerInfo power = PowerService::getPowerInfo(item.channel, item.messageId,
                                                                       powerChannel[this->getItemId()]);
            return Helper::castType<float>(power.power);
        }, getItemId());

        setConnectorPluggedInput([this]() -> boolean {
            auto tx = getTransaction(this->getItemId());
            return (tx && (tx->isActive() || tx->isRunning())) ||
                   (state[this->getItemId()] && state[this->getItemId()]->getStatus());
        }, getItemId());

        addErrorDataInput([this]() -> MicroOcpp::ErrorData {
            return getConnectorErrorData(this->getItemId());
        }, getItemId());
    }
};

void initConnector() {
    if (!connector.size()) {
        for (uint8_t index = 1; index <= NUM_CONNECTOR; ++index) {
            connector.emplace_back(new Connector(*&index));
            state[index] = new ConnectorState<>();
        }
        for (uint8_t i = 0; i < NUM_CONNECTOR; ++i) {
            PowerService::setChannelControl(*&i, 0);
        }
    }
    for (auto item : connector) {
        item->runInstance();
    }
    delay(1000);
}

void syncConnectorRelay(uint8_t connectorId) {
    bool shouldEnableRelay = getChargePointStatus(connectorId) == ChargePointStatus_Charging;
    PowerService::setChannelControl(Helper::generateId(connectorId), shouldEnableRelay);
}

void runInstance() {
    for (uint8_t i = 1; i <= NUM_CONNECTOR; ++i) {
        syncConnectorRelay(i);

        if (!state[i]->isActivated) {
            lowPowerStartMs[i] = 0;
            continue;
        }
        else {

            auto item = ItemPower::getInfo(i);
            PowerService::PowerInfo info = {0, 0, 0, 0};
            PowerService::PowerInfo power = PowerService::getPowerInfo(item.channel, item.messageId,
                                                                       info);
            updateMeterError(i, item.channel);

            state[i]->setPreviousPower(state[i]->getCurrentPower());
            state[i]->setCurrentPower(power.power);

            state[i]->setPreviousEnergy(state[i]->getCurrentEnergy());
            state[i]->setCurrentEnergy(power.energy);

            state[i]->setPreviousAmpe(state[i]->getCurrentAmpe());
            state[i]->setCurrentAmpe(power.current);

            bool isBelowStopPower = state[i]->getCurrentPower() < LOW_POWER_STOP_THRESHOLD;
            if (isBelowStopPower) {
                if (lowPowerStartMs[i] == 0) {
                    lowPowerStartMs[i] = millis();
                }
            } else {
                lowPowerStartMs[i] = 0;
            }

            bool isLowPowerTimedOut = lowPowerStartMs[i] != 0 &&
                                      millis() - lowPowerStartMs[i] >= LOW_POWER_STOP_TIME_MS;

            bool isOverCurrent = state[i]->getCurrentAmpe() >= HIGH_LIMIT_AMPE or
                                 state[i]->getPreviousAmpe() >= HIGH_LIMIT_AMPE;
            bool isAbleToStop = isLowPowerTimedOut or isOverCurrent;

            if (isAbleToStop) {
                connectorErrorState[i] = isOverCurrent
                        ? ConnectorErrorState::OverCurrent
                        : ConnectorErrorState::LowPowerTimeout;
                counter[i] = 0;
                cnt[i] = 0;
                lowPowerStartMs[i] = 0;
                (state[i]->txn)->setInactive();
                (state[i]->txn)->setStopReason("Other");
                (state[i]->txn)->commit();
            }
        }
        delay(200);
    }
    delay(READ_DELAY);
}

// Hàm thay đổi màu LED
void setLEDColor(uint8_t r, uint8_t g, uint8_t b) {
    for (int i = 0; i < LED_COUNT; i++) {
        strip.setPixelColor(i, strip.Color(r, g, b));
    }
    strip.show();
}

const char* ethernetHardwareStatusName(EthernetHardwareStatus status) {
    switch (status) {
        case EthernetNoHardware: return "NoHardware";
        case EthernetENC28J60: return "ENC28J60";
        case EthernetW5100: return "W5100";
        case EthernetW5200: return "W5200";
        case EthernetW5500: return "W5500";
        default: return "UnknownHardware";
    }
}

const char* ethernetLinkStatusName(EthernetLinkStatus status) {
    switch (status) {
        case LinkON: return "LinkON";
        case LinkOFF: return "LinkOFF";
        default: return "Unknown";
    }
}

void printEthernetStatus(const char* label) {
    Serial.print("[eth] ");
    Serial.println(label);
    Serial.print("[eth] hardware=");
    Serial.println(ethernetHardwareStatusName(Ethernet.hardwareStatus()));
    Serial.print("[eth] link=");
    Serial.println(ethernetLinkStatusName(Ethernet.linkStatus()));
    Serial.print("[eth] localIP=");
    Serial.println(Ethernet.localIP());
    Serial.print("[eth] gateway=");
    Serial.println(Ethernet.gatewayIP());
    Serial.print("[eth] subnet=");
    Serial.println(Ethernet.subnetMask());
    Serial.print("[eth] dns=");
    Serial.println(Ethernet.dnsServerIP());
}

bool testEthernetInternet(const char* host, uint16_t port) {
    IPAddress resolvedIp;
    DNSClient dnsClient;
    dnsClient.begin(Ethernet.dnsServerIP());

    Serial.print("[net] DNS lookup ");
    Serial.println(host);
    if (dnsClient.getHostByName(host, resolvedIp) != 1) {
        Serial.println("[net] DNS failed");
        return false;
    }

    Serial.print("[net] resolved=");
    Serial.println(resolvedIp);

    EthernetClient testClient;
    Serial.print("[net] TCP connect ");
    Serial.print(host);
    Serial.print(":");
    Serial.println(port);
    if (!testClient.connect(host, port)) {
        Serial.println("[net] TCP connect failed");
        return false;
    }

    Serial.println("[net] TCP connect OK");
    testClient.stop();
    return true;
}

bool parseIpAddress(const String &text, IPAddress &address) {
    return !text.isEmpty() && address.fromString(text);
}

struct EthernetDhcpTaskContext {
    volatile bool done;
    volatile bool connected;
};

void runEthernetDhcp(void *parameter) {
    EthernetDhcpTaskContext *context = static_cast<EthernetDhcpTaskContext *>(parameter);
    context->connected = Ethernet.begin(mac) != 0;
    context->done = true;
    vTaskDelete(nullptr);
}

bool connectConfiguredNetwork(const NetworkConfig &config) {
    if (!config.useEthernet) {
        Serial.print("[wifi] connecting to ");
        Serial.println(config.wifiSsid);
        WiFi.begin(config.wifiSsid.c_str(), config.wifiPassword.c_str());
        unsigned long deadline = millis() + 30000;
        while (WiFi.status() != WL_CONNECTED && millis() < deadline) {
            handleNetworkConfigPortal();
            delay(50);
        }
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[wifi] connection failed");
            return false;
        }
        Serial.print("[wifi] connected IP=");
        Serial.println(WiFi.localIP());
        return true;
    }

    Serial.println("[eth] configuring ENC28J60");
    pinMode(ETH_CS_PIN, OUTPUT);
    digitalWrite(ETH_CS_PIN, HIGH);
    SPI.begin(ETH_SCK_PIN, ETH_MISO_PIN, ETH_MOSI_PIN, ETH_CS_PIN);
    Ethernet.init(ETH_CS_PIN);

    bool connected = false;
    if (config.ethernetDhcp) {
        for (uint8_t attempt = 1; attempt <= 3 && !connected; attempt++) {
            Serial.print("[eth] DHCP attempt ");
            Serial.println(attempt);
            EthernetDhcpTaskContext dhcpContext = {false, false};
            TaskHandle_t dhcpTask = nullptr;
            if (xTaskCreate(runEthernetDhcp, "eth_dhcp", 8192, &dhcpContext, 1, &dhcpTask) != pdPASS) {
                Serial.println("[eth] unable to start DHCP task");
                connected = false;
            } else {
                while (!dhcpContext.done) {
                    handleNetworkConfigPortal();
                    delay(10);
                }
                connected = dhcpContext.connected;
            }
            if (!connected) {
                handleNetworkConfigPortal();
                delay(1000);
            }
        }
    } else {
        IPAddress ip, gateway, subnet, dns;
        if (!parseIpAddress(config.ethernetIp, ip) ||
                !parseIpAddress(config.ethernetGateway, gateway) ||
                !parseIpAddress(config.ethernetSubnet, subnet) ||
                !parseIpAddress(config.ethernetDns, dns)) {
            Serial.println("[eth] invalid static IP configuration");
            return false;
        }
        Ethernet.begin(mac, ip, dns, gateway, subnet);
        connected = Ethernet.localIP() != IPAddress(0, 0, 0, 0);
    }

    printEthernetStatus(connected ? "connected" : "connection failed");
    return connected;
}

void setup() {
    Serial.begin(115200);
    unsigned long serialWaitStart = millis();
    while (!Serial && millis() - serialWaitStart < 3000) {
        delay(10);
    }
    stm32Serial.begin(STM32_UART_BAUD, SERIAL_8N1, STM32_UART_RX_PIN, STM32_UART_TX_PIN);
    delay(1000);
    strip.begin();
    strip.clear();
    strip.setBrightness(50);
    for (int i = 0; i < numOutputs; i++) {
        pinMode(outputs[i], OUTPUT);
        digitalWrite(outputs[i], LOW);
    }
    strip.show();

    setLEDColor(255, 0, 0);  // 🔴 Đỏ: Chưa kết nối ENC28J60

    Serial.print("[fw] build ");
    Serial.print(__DATE__);
    Serial.print(" ");
    Serial.println(__TIME__);
    Serial.print("[stm32] uart=Serial1 baud=");
    Serial.print(STM32_UART_BAUD);
    Serial.print(" rx=");
    Serial.print(STM32_UART_RX_PIN);
    Serial.print(" tx=");
    Serial.println(STM32_UART_TX_PIN);
    Serial.print("[fw] MO_NUMCONNECTORS=");
#ifdef MO_NUMCONNECTORS
    Serial.println(MO_NUMCONNECTORS);
#else
    Serial.println("not defined");
#endif

    bool hasSavedNetworkConfig = loadNetworkConfig(networkConfig);
    startNetworkConfigPortal(networkConfig);
    Serial.println("[config] AP='ZIOT Charger Config' password='88888888'");
    Serial.println("[config] open http://192.168.4.1");
    Serial.println("[config] portal remains available while network and OCPP are running");

    if (!hasSavedNetworkConfig) {
        Serial.println("[config] no saved network config; waiting for web configuration");
        setLEDColor(255, 0, 0);
        return;
    }

    if (!connectConfiguredNetwork(networkConfig)) {
        Serial.println("[net] connection failed; configuration portal remains available");
        setLEDColor(255, 0, 0);
        return;
    }

    setLEDColor(0, 255, 0);

    String ocppPath = buildOcppPath(networkConfig);
    Client &selectedClient = networkConfig.useEthernet
            ? static_cast<Client&>(ethernetOcppClient)
            : static_cast<Client&>(wifiOcppClient);
    String transportName = networkConfig.useEthernet ? "ENC28J60" : "WiFi";
    ocppConnection.reset(new NetworkOcppConnection(
            selectedClient,
            networkConfig.ocppHost,
            networkConfig.ocppPort,
            ocppPath,
            transportName));

    Serial.print("[ocpp] transport=");
    Serial.println(transportName);
    Serial.print("[ocpp] url=ws://");
    Serial.print(networkConfig.ocppHost);
    Serial.print(":");
    Serial.print(networkConfig.ocppPort);
    Serial.println(ocppPath);
    mocpp_initialize(*ocppConnection, ChargerCredentials("ZioT Test Station", "ZioT"));
    addErrorDataInput([]() -> MicroOcpp::ErrorData {
        return getNetworkErrorData();
    }, 0);
    initConnector();
    appReady = true;
#if 0
    Serial.println("Địa chỉ MAC của ESP32:");
    Serial.println(WiFi.macAddress());

    // Cấu hình ENC28J60
    Serial.println("Cấu hình ENC28J60...");
    pinMode(ETH_CS_PIN, OUTPUT);
    digitalWrite(ETH_CS_PIN, HIGH);
    SPI.begin(ETH_SCK_PIN, ETH_MISO_PIN, ETH_MOSI_PIN, ETH_CS_PIN);

    Ethernet.init(ETH_CS_PIN);
    printEthernetStatus("before DHCP");

    bool ethernetReady = false;
    for (uint8_t attempt = 1; attempt <= 3; attempt++) {
        Serial.print("[eth] DHCP attempt ");
        Serial.println(attempt);
        if (Ethernet.begin(mac) != 0) {
            ethernetReady = true;
            break;
        }
        printEthernetStatus("DHCP failed");
        delay(2000);
    }

    if (!ethernetReady) {
        Serial.println("[eth] Không thể nhận địa chỉ IP từ DHCP sau 3 lần thử.");
        Serial.println("[eth] Kiểm tra dây LAN, nguồn 3V3, chân SPI/CS và DHCP trên router.");
        setLEDColor(255, 0, 0);
        return;
    }

    printEthernetStatus("connected");
    testEthernetInternet("ocpp-1.rabbitevc.vn", 443);

    setLEDColor(0, 255, 0);  // 🟢 Xanh lá: Kết nối thành công

    Serial.println("[ocpp] using ENC28J60 plain WebSocket client");
    Serial.println("[ocpp] url=ws://ocpp-1.rabbitevc.vn/steve/websocket/CentralSystemService/1");
    mocpp_initialize(ocppConnection, ChargerCredentials("ZioT Test Station", "ZioT"));
    initConnector();
    appReady = true;
#endif
}

//     mocpp_initialize(OCPP_BACKEND_URL, OCPP_CHARGE_BOX_ID, "ZioT Test Station", "ZioT");
//     initConnector();
// }

// void loop() {
//     mocpp_loop();
//     runInstance();
// }
void loop() {
    handleNetworkConfigPortal();
    if (networkConfig.useEthernet) {
        Ethernet.maintain();
    }
    if (!appReady) {
        delay(10);
        return;
    }
    mocpp_loop();
    runInstance();

    // delay(100);
}
