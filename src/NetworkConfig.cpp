#include "NetworkConfig.h"

#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <esp_system.h>

namespace {

Preferences preferences;
WebServer configServer(80);
DNSServer configDnsServer;
NetworkConfig currentConfig;
String scannedWifiOptions;
String sessionToken;

constexpr const char *CONFIG_USERNAME = "admin";
constexpr const char *CONFIG_PASSWORD = "Ziot@123";

String htmlEscape(const String &value) {
    String escaped;
    escaped.reserve(value.length() + 16);
    for (size_t i = 0; i < value.length(); i++) {
        switch (value[i]) {
            case '&': escaped += F("&amp;"); break;
            case '<': escaped += F("&lt;"); break;
            case '>': escaped += F("&gt;"); break;
            case '"': escaped += F("&quot;"); break;
            case '\'': escaped += F("&#39;"); break;
            default: escaped += value[i]; break;
        }
    }
    return escaped;
}

bool isAuthenticated() {
    if (sessionToken.isEmpty() || !configServer.hasHeader("Cookie")) {
        return false;
    }
    String cookie = configServer.header("Cookie");
    return cookie.indexOf("ziot_session=" + sessionToken) >= 0;
}

void redirectToLogin() {
    configServer.sendHeader("Location", "/login");
    configServer.send(303, "text/plain", "");
}

void sendLoginPage(bool invalidCredentials = false) {
    String html;
    html.reserve(2200);
    html += F("<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>");
    html += F("<title>ZIOT Login</title><style>");
    html += F("body{font-family:Arial,sans-serif;background:#eef1f4;margin:0;padding:20px;color:#20252b}");
    html += F("main{max-width:380px;margin:12vh auto;background:#fff;padding:24px;border-radius:8px;box-shadow:0 2px 12px #0002}");
    html += F("h2{margin-top:0}label{display:block;font-weight:600;margin-top:14px}");
    html += F("input,button{width:100%;box-sizing:border-box;padding:12px;margin-top:6px;border:1px solid #bbc2ca;border-radius:5px;font-size:16px}");
    html += F("button{background:#1268d3;color:#fff;border:0;font-weight:700;margin-top:22px;cursor:pointer}");
    html += F(".error{color:#b42318;background:#fee4e2;padding:10px;border-radius:5px}</style></head><body><main>");
    html += F("<h2>ZIOT Charger Login</h2>");
    if (invalidCredentials) {
        html += F("<p class='error'>Invalid username or password.</p>");
    }
    html += F("<form method='post' action='/login'><label>Username</label>");
    html += F("<input name='username' autocomplete='username' required>");
    html += F("<label>Password</label><input type='password' name='password' autocomplete='current-password' required>");
    html += F("<button type='submit'>Login</button></form></main></body></html>");
    configServer.send(200, "text/html", html);
}

void handleLogin() {
    if (isAuthenticated()) {
        configServer.sendHeader("Location", "/");
        configServer.send(303, "text/plain", "");
        return;
    }
    sendLoginPage(false);
}

void handleLoginPost() {
    if (configServer.arg("username") != CONFIG_USERNAME ||
            configServer.arg("password") != CONFIG_PASSWORD) {
        sendLoginPage(true);
        return;
    }

    configServer.sendHeader("Set-Cookie",
            "ziot_session=" + sessionToken + "; Path=/; HttpOnly; SameSite=Strict");
    configServer.sendHeader("Location", "/");
    configServer.send(303, "text/plain", "");
}

void handleLogout() {
    configServer.sendHeader("Set-Cookie",
            "ziot_session=deleted; Path=/; Max-Age=0; HttpOnly; SameSite=Strict");
    configServer.sendHeader("Location", "/login");
    configServer.send(303, "text/plain", "");
}

void saveNetworkConfig(const NetworkConfig &config) {
    preferences.begin("network", false);
    preferences.putBool("ethernet", config.useEthernet);
    preferences.putBool("ethDhcp", config.ethernetDhcp);
    preferences.putString("ssid", config.wifiSsid);
    preferences.putString("wifiPass", config.wifiPassword);
    preferences.putString("ethIp", config.ethernetIp);
    preferences.putString("ethGw", config.ethernetGateway);
    preferences.putString("ethMask", config.ethernetSubnet);
    preferences.putString("ethDns", config.ethernetDns);
    preferences.putString("ocppHost", config.ocppHost);
    preferences.putUShort("ocppPort", config.ocppPort);
    preferences.putString("ocppPath", config.ocppPath);
    preferences.putString("boxId", config.chargeBoxId);
    preferences.end();
}

void sendConfigPage() {
    if (!isAuthenticated()) {
        redirectToLogin();
        return;
    }

    String html;
    html.reserve(7000);
    html += F("<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>");
    html += F("<title>ZIOT Charger Config</title><style>");
    html += F("body{font-family:Arial,sans-serif;background:#eef1f4;margin:0;padding:20px;color:#20252b}");
    html += F("main{max-width:620px;margin:auto;background:#fff;padding:20px;border-radius:8px;box-shadow:0 2px 12px #0002}");
    html += F("h2{margin-top:0}label{display:block;font-weight:600;margin-top:14px}");
    html += F("input,select,button{width:100%;box-sizing:border-box;padding:11px;margin-top:6px;border:1px solid #bbc2ca;border-radius:5px;font-size:15px}");
    html += F("button{background:#1268d3;color:#fff;border:0;font-weight:700;margin-top:22px;cursor:pointer}");
    html += F(".row{display:grid;grid-template-columns:1fr 1fr;gap:12px}.hint{color:#66717d;font-size:13px}");
    html += F("@media(max-width:540px){.row{grid-template-columns:1fr}}</style></head><body><main>");
    html += F("<div style='display:flex;align-items:center;justify-content:space-between;gap:12px'>");
    html += F("<h2>ZIOT Charger Configuration</h2><a href='/logout'>Logout</a></div>");
    html += F("<p class='hint'>Access point: ZIOT Charger Config, address: 192.168.4.1</p>");
    html += F("<form method='post' action='/save'>");
    html += F("<label>Network connection</label><select id='mode' name='mode' onchange='toggleMode()'>");
    html += currentConfig.useEthernet ? F("<option value='ethernet' selected>Ethernet</option><option value='wifi'>Wi-Fi</option>")
                                      : F("<option value='ethernet'>Ethernet</option><option value='wifi' selected>Wi-Fi</option>");
    html += F("</select>");

    html += F("<section id='wifi'><label>Wi-Fi SSID</label>");
    if (!scannedWifiOptions.isEmpty()) {
        html += F("<select name='ssid'>");
        html += scannedWifiOptions;
        html += F("</select>");
    } else {
        html += F("<input name='ssid' value='");
        html += htmlEscape(currentConfig.wifiSsid);
        html += F("'>");
    }
    html += F("<button type='button' onclick=\"location.href='/scan'\">Scan Wi-Fi</button>");
    html += F("<label>Wi-Fi password</label><input type='password' name='wifiPassword' value='");
    html += htmlEscape(currentConfig.wifiPassword);
    html += F("'></section>");

    html += F("<section id='ethernet'><label><input style='width:auto' type='checkbox' id='dhcp' name='dhcp' onchange='toggleDhcp()'");
    if (currentConfig.ethernetDhcp) html += F(" checked");
    html += F("> Use DHCP</label><div id='staticIp'>");
    html += F("<div class='row'><div><label>IP address</label><input name='ethernetIp' value='");
    html += htmlEscape(currentConfig.ethernetIp);
    html += F("'></div><div><label>Gateway</label><input name='ethernetGateway' value='");
    html += htmlEscape(currentConfig.ethernetGateway);
    html += F("'></div></div><div class='row'><div><label>Subnet mask</label><input name='ethernetSubnet' value='");
    html += htmlEscape(currentConfig.ethernetSubnet);
    html += F("'></div><div><label>DNS server</label><input name='ethernetDns' value='");
    html += htmlEscape(currentConfig.ethernetDns);
    html += F("'></div></div></div></section>");

    html += F("<hr><div class='row'><div><label>OCPP host</label><input name='ocppHost' required value='");
    html += htmlEscape(currentConfig.ocppHost);
    html += F("'></div><div><label>OCPP port</label><input type='number' min='1' max='65535' name='ocppPort' required value='");
    html += String(currentConfig.ocppPort);
    html += F("'></div></div><label>OCPP base path</label><input name='ocppPath' required value='");
    html += htmlEscape(currentConfig.ocppPath);
    html += F("'><label>Charge Box ID</label><input name='chargeBoxId' required value='");
    html += htmlEscape(currentConfig.chargeBoxId);
    html += F("'><p class='hint'>Final path = base path + / + Charge Box ID</p>");
    html += F("<button type='submit'>Save and restart</button></form>");
    html += F("<script>function toggleMode(){let e=document.getElementById('mode').value==='ethernet';document.getElementById('ethernet').style.display=e?'block':'none';document.getElementById('wifi').style.display=e?'none':'block'}");
    html += F("function toggleDhcp(){document.getElementById('staticIp').style.display=document.getElementById('dhcp').checked?'none':'block'}toggleMode();toggleDhcp();</script>");
    html += F("</main></body></html>");
    configServer.send(200, "text/html", html);
}

void handleSave() {
    if (!isAuthenticated()) {
        redirectToLogin();
        return;
    }

    NetworkConfig next = currentConfig;
    next.useEthernet = configServer.arg("mode") != "wifi";
    next.ethernetDhcp = configServer.hasArg("dhcp");
    next.wifiSsid = configServer.arg("ssid");
    next.wifiPassword = configServer.arg("wifiPassword");
    next.ethernetIp = configServer.arg("ethernetIp");
    next.ethernetGateway = configServer.arg("ethernetGateway");
    next.ethernetSubnet = configServer.arg("ethernetSubnet");
    next.ethernetDns = configServer.arg("ethernetDns");
    next.ocppHost = configServer.arg("ocppHost");
    next.ocppPort = (uint16_t) configServer.arg("ocppPort").toInt();
    next.ocppPath = configServer.arg("ocppPath");
    next.chargeBoxId = configServer.arg("chargeBoxId");

    if ((!next.useEthernet && next.wifiSsid.isEmpty()) ||
            next.ocppHost.isEmpty() || next.ocppPort == 0 ||
            next.ocppPath.isEmpty() || next.chargeBoxId.isEmpty()) {
        configServer.send(400, "text/plain", "Invalid configuration");
        return;
    }

    saveNetworkConfig(next);
    configServer.send(200, "text/html",
            "<html><body><h2>Configuration saved</h2><p>Device is restarting...</p></body></html>");
    delay(500);
    ESP.restart();
}

void handleWifiScan() {
    if (!isAuthenticated()) {
        redirectToLogin();
        return;
    }

    int count = WiFi.scanNetworks();
    scannedWifiOptions = "";

    if (count <= 0) {
        scannedWifiOptions = F("<option value=''>No networks found</option>");
    } else {
        for (int i = 0; i < count; i++) {
            String networkSsid = WiFi.SSID(i);
            scannedWifiOptions += F("<option value='");
            scannedWifiOptions += htmlEscape(networkSsid);
            scannedWifiOptions += "'";
            if (networkSsid == currentConfig.wifiSsid) {
                scannedWifiOptions += F(" selected");
            }
            scannedWifiOptions += F(">");
            scannedWifiOptions += htmlEscape(networkSsid);
            scannedWifiOptions += F(" (");
            scannedWifiOptions += String(WiFi.RSSI(i));
            scannedWifiOptions += F(" dBm)</option>");
        }
    }

    WiFi.scanDelete();
    configServer.sendHeader("Location", "/");
    configServer.send(303, "text/plain", "");
}

} // namespace

bool loadNetworkConfig(NetworkConfig &config) {
    preferences.begin("network", false);
    bool configured = preferences.isKey("ethernet");
    config.useEthernet = preferences.getBool("ethernet", config.useEthernet);
    config.ethernetDhcp = preferences.getBool("ethDhcp", config.ethernetDhcp);
    config.wifiSsid = preferences.getString("ssid", config.wifiSsid);
    config.wifiPassword = preferences.getString("wifiPass", config.wifiPassword);
    config.ethernetIp = preferences.getString("ethIp", config.ethernetIp);
    config.ethernetGateway = preferences.getString("ethGw", config.ethernetGateway);
    config.ethernetSubnet = preferences.getString("ethMask", config.ethernetSubnet);
    config.ethernetDns = preferences.getString("ethDns", config.ethernetDns);
    config.ocppHost = preferences.getString("ocppHost", config.ocppHost);
    config.ocppPort = preferences.getUShort("ocppPort", config.ocppPort);
    config.ocppPath = preferences.getString("ocppPath", config.ocppPath);
    config.chargeBoxId = preferences.getString("boxId", config.chargeBoxId);
    preferences.end();
    return configured;
}

void startNetworkConfigPortal(const NetworkConfig &config) {
    currentConfig = config;
    sessionToken = String((uint32_t) esp_random(), HEX) + String((uint32_t) esp_random(), HEX);
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP("ZIOT Charger Config", "88888888");
    configDnsServer.start(53, "*", WiFi.softAPIP());
    const char *headers[] = {"Cookie"};
    configServer.collectHeaders(headers, 1);
    configServer.on("/", HTTP_GET, sendConfigPage);
    configServer.on("/login", HTTP_GET, handleLogin);
    configServer.on("/login", HTTP_POST, handleLoginPost);
    configServer.on("/logout", HTTP_GET, handleLogout);
    configServer.on("/scan", HTTP_GET, handleWifiScan);
    configServer.on("/save", HTTP_POST, handleSave);
    configServer.onNotFound([]() {
        configServer.sendHeader("Location", "/");
        configServer.send(302, "text/plain", "");
    });
    configServer.begin();
    Serial.print("[config] AP IP=");
    Serial.println(WiFi.softAPIP());
}

void handleNetworkConfigPortal() {
    configDnsServer.processNextRequest();
    configServer.handleClient();
}

String buildOcppPath(const NetworkConfig &config) {
    String path = config.ocppPath;
    if (!path.startsWith("/")) path = "/" + path;
    while (path.endsWith("/")) path.remove(path.length() - 1);
    path += "/";
    path += config.chargeBoxId;
    return path;
}
