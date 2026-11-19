

// Prototypes
void saveWiFiConfig(const String &ssid, const String &password, const String &charboxID, const String &ocppServer, bool useEthernet, const String &ethernetIP, const String &ethernetGateway, const String &ethernetSubnet, const String &ethernetDNS);
bool loadWiFiConfig(String &ssid, String &password, String &charboxID, String &ocppServer, bool &useEthernet, String &ethernetIP, String &ethernetGateway, String &ethernetSubnet, String &ethernetDNS);
void handleLogin();
void handleLoginPost();
void handleConfig();
void handleScan();
void handleSave();
void startWebServer();
void connectWiFi();
void connectEthernet();
void setupWiFi();
void checkRebootAndSwitchMode();
void reconnectEthernet();



#include <SPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ETH.h>
#include <UIPEthernet.h>
#include <Dns.h>

Preferences preferences;
WebServer server(80);
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
// EthernetClient ethClient;
// PubSubClient client(ethClient);

// Khai báo các biến toàn cục

String ssid, password, charboxID, ocppServer;
bool configSaved = false;
String username = "admin";  // Tên đăng nhập mặc định
String userPassword = "1234"; // Mật khẩu mặc định
bool loggedIn = false;

// Biến cho Ethernet
bool useEthernet = false;
String ethernetIP, ethernetGateway, ethernetSubnet, ethernetDNS;

void setupWiFi() {
  if (loadWiFiConfig(ssid, password, charboxID, ocppServer, useEthernet, ethernetIP, ethernetGateway, ethernetSubnet, ethernetDNS)) {
    if (useEthernet) {
      connectEthernet();
    } else {
      connectWiFi();
    }

    if (WiFi.status() != WL_CONNECTED && Ethernet.localIP() == IPAddress(0, 0, 0, 0)) {
      Serial.println("Entering Config Mode");
      WiFi.softAP("ZIOT Charger", "88888888");
      startWebServer();
      while (!configSaved) {
        server.handleClient();
        delay(10);
      }
      if (useEthernet) {
        connectEthernet();
      } else {
        connectWiFi();
      }
    }
  } else {
    Serial.println("Entering Config Mode");
    WiFi.softAP("ZIOT Charger", "88888888");
    startWebServer();
    while (!configSaved) {
      server.handleClient();
      delay(10);
    }
    if (useEthernet) {
      connectEthernet();
    } else {
      connectWiFi();
    }
  }
}

void connectWiFi() {
  if (!useEthernet) {
    WiFi.begin(ssid.c_str(), password.c_str());
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < 60000) {
      delay(1000);
      Serial.println("Connecting to WiFi...");
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("Connected to WiFi!");
      Serial.print("IP Address: ");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println("Failed to connect to WiFi.");
    }
  }
}

void connectEthernet() {
  if (useEthernet) {
    Serial.println("Connecting with Ethernet (ENC28J60)...");

    // Cấu hình ENC28J60
    SPI.begin(40, 41, 42, 39);  // SCK, MISO, MOSI, CS
    Ethernet.init(39); // Đặt CS là chân 39

    if (ethernetIP.isEmpty()) {
      // Sử dụng DHCP
      // if (Ethernet.begin(mac) == 0) {
      //   Serial.println("Failed to configure Ethernet using DHCP.");
      //   checkRebootAndSwitchMode();
      // } else {
      //   Serial.println("Ethernet connected using DHCP!");
      //   Serial.print("IP Address: ");
      //   Serial.println(Ethernet.localIP());
    
      //   Serial.print("Subnet Mask: ");
      //   Serial.println(Ethernet.subnetMask());
    
      //   Serial.print("Gateway: ");
      //   Serial.println(Ethernet.gatewayIP());
    
      //   // Lấy DNS server từ DHCP
      //   IPAddress dnsServer = Ethernet.dnsServerIP();
        Serial.print("kết nối DHCP!");
      //   Serial.println(dnsServer);

      // }


      while (!Serial);

      Serial.println("Địa chỉ MAC của ESP32:");
      Serial.println(WiFi.macAddress());
  
      // Cấu hình ENC28J60
      Serial.println("Cấu hình ENC28J60...");
      SPI.begin(40, 41, 42, 39); // SCK, MISO, MOSI, CS
  
      Ethernet.init(39); // Chân CS của ENC28J60
      if (Ethernet.begin(mac) == 0) {
          Serial.println("Không thể nhận địa chỉ IP từ DHCP.");
          while (true) {
              delay(1000);
          }
      }
  
      Serial.println("Kết nối ENC28J60 thành công!");
      Serial.print("Địa chỉ IP (ENC28J60): ");
      Serial.println(Ethernet.localIP());

    } else {
      // Sử dụng IP tĩnh
      IPAddress ip, gateway, subnet, dns;
      ip.fromString(ethernetIP);
      gateway.fromString(ethernetGateway);
      subnet.fromString(ethernetSubnet);
      dns.fromString(ethernetDNS);

      Ethernet.begin(mac, ip, dns, gateway, subnet);
      Serial.println("Ethernet connected using static IP!");
      Serial.print("IP Address (ENC28J60): ");
      Serial.println(Ethernet.localIP());
    }
  }
}
void startWebServer() {
  server.on("/", HTTP_GET, handleLogin);
  server.on("/login", HTTP_POST, handleLoginPost);
  server.on("/config", HTTP_GET, handleConfig);
  server.on("/scan", HTTP_POST, handleScan);
  server.on("/save", HTTP_POST, handleSave);
  server.begin();
}

void handleLogin() {
  String html = "<!DOCTYPE html><html><head><title>Login</title>";
  html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">";
  html += "<style>body { font-family: Arial, sans-serif; background-color: #f2f2f2; display: flex; justify-content: center; align-items: center; height: 100vh; margin: 0; }";
  html += ".container { width: 100%; max-width: 400px; background: #ffffff; border-radius: 10px; box-shadow: 0 4px 10px rgba(0, 0, 0, 0.1); padding: 20px; box-sizing: border-box; }";
  html += "h2 { text-align: center; color: #333; font-size: 28px; margin-bottom: 20px; }";
  html += "input { width: 100%; padding: 15px; margin: 10px 0; border: 1px solid #ccc; border-radius: 5px; font-size: 16px; box-sizing: border-box; transition: border 0.3s ease; }";
  html += "input:focus { border-color: #007BFF; }";
  html += "button { width: 100%; padding: 15px; background-color: #007BFF; color: white; border: none; border-radius: 5px; font-size: 18px; cursor: pointer; transition: background-color 0.3s ease; }";
  html += "button:hover { background-color: #0056b3; }";
  html += ".error { color: #ff4d4d; text-align: center; margin-top: 10px; font-size: 14px; }";
  html += ".footer { text-align: center; margin-top: 20px; font-size: 14px; color: #777; }";
  html += "</style></head><body>";

  html += "<div class='container'>";
  html += "<h2>Login</h2>";
  html += "<form action=\"/login\" method=\"POST\">";
  html += "<input type=\"text\" name=\"username\" placeholder=\"Username\" required>";
  html += "<input type=\"password\" name=\"password\" placeholder=\"Password\" required>";
  html += "<button type=\"submit\">Login</button>";
  html += "</form>";

  html += "<div class=\"footer\">&copy; 2025 My Company</div>";
  html += "</div>";

  html += "</body></html>";

  server.send(200, "text/html", html);
}

void handleLoginPost() {
  String user = server.arg("username");
  String pass = server.arg("password");

  if (user == username && pass == userPassword) {
    loggedIn = true;
    server.sendHeader("Location", "/config");  // Chuyển hướng đến trang cấu hình sau khi đăng nhập thành công
    server.send(303, "text/html", "");
  } else {
    server.send(200, "text/html", "<html><body><h2>Login Failed!</h2><p class=\"error\">Invalid username or password. Please try again.</p><a href=\"/\">Back to Login</a></body></html>");
  }
}


void handleConfig() {
  if (!loggedIn) {
    server.sendHeader("Location", "/");
    server.send(302, "text/html", "");
    return;
  }

  int n = WiFi.scanComplete();
  String wifiOptions = "";

  if (n == WIFI_SCAN_RUNNING) {
    wifiOptions = "<option value=\"\">Scanning...</option>";
  } else if (n <= 0) {
    wifiOptions = "<option value=\"\">No networks found</option>";
  } else {
    for (int i = 0; i < n; ++i) {
      wifiOptions += "<option value=\"" + WiFi.SSID(i) + "\">" + WiFi.SSID(i) + " (" + String(WiFi.RSSI(i)) + " dBm)</option>";
    }
    WiFi.scanDelete(); // Xóa danh sách sau khi quét xong
  }

  String html = "<!DOCTYPE html><html><head><title>Wi-Fi Config</title>";
  html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; margin: 0; padding: 0; display: flex; justify-content: center; align-items: center; height: 100vh; background-color: #e9ecef; }";
  html += ".container { max-width: 400px; width: 100%; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 4px 8px rgba(0, 0, 0, 0.1); }";
  html += "h2 { text-align: center; color: #333; margin-bottom: 20px; }";
  html += "form { margin-bottom: 15px; }";
  html += "input, select, button { width: 100%; padding: 10px; margin: 10px 0; border: 1px solid #ccc; border-radius: 5px; font-size: 16px; box-sizing: border-box; }";
  html += "button { background-color: #007BFF; color: white; font-size: 16px; cursor: pointer; border: none; }";
  html += "button:hover { background-color: #0056b3; }";
  html += "p { text-align: center; font-size: 14px; color: #555; margin-top: 10px; }";
  html += ".dhcp-toggle { margin-bottom: 10px; }";
  html += "</style>";
  html += "</head><body>";

  html += "<div class='container'>";
  html += "<h2>Wi-Fi Configuration</h2>";

  html += "<form action=\"/scan\" method=\"POST\">";
  html += "<button type=\"submit\">Scan Wi-Fi</button>";
  html += "</form>";

  html += "<form action=\"/save\" method=\"POST\">";
  html += "<label for=\"connectionType\">Connection Type:</label>";
  html += "<select id=\"connectionType\" name=\"connectionType\" onchange=\"toggleConnectionType()\">";
  html += "<option value=\"wifi\">Wi-Fi</option>";
  html += "<option value=\"ethernet\">Ethernet</option>";
  html += "</select>";

  html += "<div id=\"wifiConfig\">";
  html += "<label for=\"ssid\">SSID:</label>";
  html += "<select id=\"ssid\" name=\"ssid\">" + wifiOptions + "</select>";

  html += "<label for=\"password\">Password:</label>";
  html += "<input type=\"password\" id=\"password\" name=\"password\" placeholder=\"Enter Wi-Fi password\">";
  html += "</div>";

  html += "<div id=\"ethernetConfig\" style=\"display: none;\">";
  html += "<div class=\"dhcp-toggle\">";
  html += "<label><input type=\"checkbox\" id=\"dhcp\" name=\"dhcp\" onchange=\"toggleDHCP()\"> Use DHCP</label>";
  html += "</div>";

  html += "<div id=\"staticIPConfig\" style=\"display: none;\">";
  html += "<label for=\"ethernetIP\">IP Address:</label>";
  html += "<input type=\"text\" id=\"ethernetIP\" name=\"ethernetIP\" placeholder=\"Enter IP Address\">";

  html += "<label for=\"ethernetGateway\">Gateway:</label>";
  html += "<input type=\"text\" id=\"ethernetGateway\" name=\"ethernetGateway\" placeholder=\"Enter Gateway\">";

  html += "<label for=\"ethernetSubnet\">Subnet Mask:</label>";
  html += "<input type=\"text\" id=\"ethernetSubnet\" name=\"ethernetSubnet\" placeholder=\"Enter Subnet Mask\">";

  html += "<label for=\"ethernetDNS\">DNS Server:</label>";
  html += "<input type=\"text\" id=\"ethernetDNS\" name=\"ethernetDNS\" placeholder=\"Enter DNS Server\">";
  html += "</div>"; // Đóng staticIPConfig
  html += "</div>"; // Đóng ethernetConfig

  html += "<label for=\"charboxID\">CharboxID:</label>";
  html += "<input type=\"text\" id=\"charboxID\" name=\"charboxID\" placeholder=\"Enter Charbox ID\">";

  html += "<label for=\"ocppServer\">OCPP Server:</label>";
  html += "<input type=\"text\" id=\"ocppServer\" name=\"ocppServer\" placeholder=\"Enter OCPP server URL\">";

  html += "<button type=\"submit\">Save</button>";
  html += "</form>";

  if (wifiOptions == "<option value=\"\">No networks found</option>") {
    html += "<p>No Wi-Fi networks found. Please try again.</p>";
  }

  html += "</div>";
  html += "<script>";
  html += "function toggleConnectionType() {";
  html += "  var connectionType = document.getElementById('connectionType').value;";
  html += "  if (connectionType === 'wifi') {";
  html += "    document.getElementById('wifiConfig').style.display = 'block';";
  html += "    document.getElementById('ethernetConfig').style.display = 'none';";
  html += "  } else {";
  html += "    document.getElementById('wifiConfig').style.display = 'none';";
  html += "    document.getElementById('ethernetConfig').style.display = 'block';";
  html += "  }";
  html += "}";
  html += "function toggleDHCP() {";
  html += "  var dhcp = document.getElementById('dhcp').checked;";
  html += "  if (dhcp) {";
  html += "    document.getElementById('staticIPConfig').style.display = 'none';";
  html += "  } else {";
  html += "    document.getElementById('staticIPConfig').style.display = 'block';";
  html += "  }";
  html += "}";
  html += "</script>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}
// Xử lý quét Wi-Fi
void handleScan() {
  WiFi.scanDelete();           // Xóa danh sách mạng cũ
  WiFi.disconnect();
  int n = WiFi.scanNetworks(); // Bắt đầu quét Wi-Fi đồng bộ

  if (n > 0) {
    Serial.println("Wi-Fi networks found:");
    for (int i = 0; i < n; ++i) {
      Serial.printf("%d: %s (%d dBm)\n", i + 1, WiFi.SSID(i).c_str(), WiFi.RSSI(i));
    }
  }

  handleConfig(); // Quay lại trang root với danh sách mạng Wi-Fi mới
}



void handleSave() {
  if (server.hasArg("connectionType")) {
    String connectionType = server.arg("connectionType");

    if (connectionType == "wifi" && server.hasArg("ssid") && server.hasArg("password")) {
      ssid = server.arg("ssid");
      password = server.arg("password");
      useEthernet = false;
    } else if (connectionType == "ethernet") {
      useEthernet = true;
      bool useDHCP = server.hasArg("dhcp"); // Kiểm tra xem DHCP có được chọn không

      if (useDHCP) {
        // Sử dụng DHCP
        ethernetIP = "";
        ethernetGateway = "";
        ethernetSubnet = "";
        ethernetDNS = "";
      } else {
        // Sử dụng IP tĩnh
        ethernetIP = server.arg("ethernetIP");
        ethernetGateway = server.arg("ethernetGateway");
        ethernetSubnet = server.arg("ethernetSubnet");
        ethernetDNS = server.arg("ethernetDNS");
      }
    } else {
      server.send(200, "text/html", "<html><body><h2>Invalid Input!</h2></body></html>");
      return;
    }

    charboxID = server.arg("charboxID");
    ocppServer = server.arg("ocppServer");

    saveWiFiConfig(ssid, password, charboxID, ocppServer, useEthernet, ethernetIP, ethernetGateway, ethernetSubnet, ethernetDNS);
    configSaved = true;

    server.send(200, "text/html", "<html><body><h2>Configuration Saved!</h2></body></html>");
    delay(1000);
    server.stop();
  } else {
    server.send(200, "text/html", "<html><body><h2>Invalid Input!</h2></body></html>");
  }
}



void saveWiFiConfig(const String &ssid, const String &password, const String &charboxID, const String &ocppServer, bool useEthernet, const String &ethernetIP, const String &ethernetGateway, const String &ethernetSubnet, const String &ethernetDNS) {
  preferences.begin("wifi-config", false);
  preferences.putString("ssid", ssid);
  preferences.putString("password", password);
  preferences.putString("charboxID", charboxID);
  preferences.putString("ocppServer", ocppServer);
  preferences.putBool("useEthernet", useEthernet);
  preferences.putString("ethernetIP", ethernetIP);
  preferences.putString("ethernetGateway", ethernetGateway);
  preferences.putString("ethernetSubnet", ethernetSubnet);
  preferences.putString("ethernetDNS", ethernetDNS);
  preferences.end();
}

bool loadWiFiConfig(String &ssid, String &password, String &charboxID, String &ocppServer, bool &useEthernet, String &ethernetIP, String &ethernetGateway, String &ethernetSubnet, String &ethernetDNS) {
  preferences.begin("wifi-config", true);
  ssid = preferences.getString("ssid", "");
  password = preferences.getString("password", "");
  charboxID = preferences.getString("charboxID", "");
  ocppServer = preferences.getString("ocppServer", "");
  useEthernet = preferences.getBool("useEthernet", false);
  ethernetIP = preferences.getString("ethernetIP", "");
  ethernetGateway = preferences.getString("ethernetGateway", "");
  ethernetSubnet = preferences.getString("ethernetSubnet", "");
  ethernetDNS = preferences.getString("ethernetDNS", "");
  preferences.end();

  return !ssid.isEmpty() && !password.isEmpty() && !charboxID.isEmpty() && !ocppServer.isEmpty();
}

void checkRebootAndSwitchMode() {
  Preferences preferences;
  unsigned long currentMillis = millis();
  unsigned long rebootInterval = 30000; // 30 giây
  int rebootCount = 0;
  unsigned long lastRebootTime = 0;

  preferences.begin("rebootData", false);
  rebootCount = preferences.getInt("count", 0);
  lastRebootTime = preferences.getULong("lastRebootTime", 0);

  if ((currentMillis - lastRebootTime) <= rebootInterval) {
    rebootCount++;
  } else {
    rebootCount = 1; // Reset lại nếu khoảng thời gian lớn hơn rebootInterval
  }

  preferences.putInt("count", rebootCount);
  preferences.putULong("lastRebootTime", currentMillis);

  if (rebootCount >= 3) {
    Serial.println("Detected 3 reboots in 30 seconds. Switching to AP mode.");
    preferences.putInt("count", 0); // Reset đếm lại sau khi chuyển sang AP
    WiFi.softAP("ZIOT Charger", "88888888");
    Serial.println("AP mode active. SSID: ZIOT Charger, Password: 88888888");
    startWebServer();
    while (!configSaved) {
      server.handleClient();
      delay(10);
    }
    if (useEthernet) {
      connectEthernet();
    } else {
      connectWiFi();
    }
  } else {
    Serial.println("Normal operation. Reboot count: " + String(rebootCount));
    setupWiFi();
  }

  preferences.end();
}


void reconnectEthernet() {
  if (useEthernet) {
    // Kiểm tra trạng thái kết nối Ethernet
    if (Ethernet.linkStatus() == LinkOFF) {
      Serial.println("Ethernet connection lost. Attempting to reconnect...");

      // Thử kết nối lại
      if (ethernetIP.isEmpty()) {
        // Sử dụng DHCP
        if (Ethernet.begin(mac) == 0) {
          Serial.println("Failed to reconnect using DHCP.");
        } else {
          Serial.println("Ethernet reconnected using DHCP!");
          Serial.print("IP Address (ENC28J60): ");
          Serial.println(Ethernet.localIP());
        }
      } else {
        // Sử dụng IP tĩnh
        IPAddress ip, gateway, subnet, dns;
        ip.fromString(ethernetIP);
        gateway.fromString(ethernetGateway);
        subnet.fromString(ethernetSubnet);
        dns.fromString(ethernetDNS);

        Ethernet.begin(mac, ip, dns, gateway, subnet);
        Serial.println("Ethernet reconnected using static IP!");
        Serial.print("IP Address (ENC28J60): ");
        Serial.println(Ethernet.localIP());
      }
    }
  }
}