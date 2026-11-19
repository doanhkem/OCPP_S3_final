#pragma once

#include <Arduino.h>

struct NetworkConfig {
    bool useEthernet = true;
    bool ethernetDhcp = true;
    String wifiSsid;
    String wifiPassword;
    String ethernetIp;
    String ethernetGateway;
    String ethernetSubnet;
    String ethernetDns;
    String ocppHost = "ocpp-1.rabbitevc.vn";
    uint16_t ocppPort = 80;
    String ocppPath = "/steve/websocket/CentralSystemService";
    String chargeBoxId = "1";
};

bool loadNetworkConfig(NetworkConfig &config);
void startNetworkConfigPortal(const NetworkConfig &config);
void handleNetworkConfigPortal();
String buildOcppPath(const NetworkConfig &config);
