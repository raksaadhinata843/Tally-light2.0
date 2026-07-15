#include <MODE_TALLYHUB_32.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFiManager.h>
#include <ArduinoOTA.h>
#include <Adafruit_NeoPixel.h>

#define FIRMWARE_VERSION "1.0.1"
#define DEVICE_MODEL "ESP32-WS2812B"

// ---- LED hardware config (edit to match your wiring) ----
#define LED_PIN 5
#define LED_COUNT 1
#define LED_BRIGHTNESS 80
#define BOOT_BUTTON_PIN 0

static Adafruit_NeoPixel leds_th(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
#define TH_BLACK leds_th.Color(0, 0, 0)
#define TH_WHITE leds_th.Color(255, 255, 255)
#define TH_RED leds_th.Color(255, 0, 0)
#define TH_GREEN leds_th.Color(0, 255, 0)
#define TH_BLUE leds_th.Color(0, 0, 255)
#define TH_YELLOW leds_th.Color(255, 200, 0)

WebServer server(80);
WiFiUDP udp_th;
Preferences preferences;
WiFiManager wifiManager;

String deviceName = "ESP32 Tally Light";
String deviceID = "tally-";
String macAddress = "";
String ipAddress = "";
String hubIP = "192.168.0.1";
int hubPort = 3000;
String assignedSource = "";
String assignedSourceName = "";
String currentSource = "";
String customDisplayName = "";
String currentStatus = "INIT";
bool isConnected = false;
bool isRegisteredWithHub = false;
bool isAssigned = false;

bool isProgram = false;
bool isPreview = false;
bool isRecording = false;
bool isStreaming = false;

unsigned long lastHeartbeat = 0;
unsigned long lastLedUpdate = 0;
unsigned long bootTime = 0;
unsigned long lastHubResponse = 0;
unsigned long hubConnectionAttempts = 0;
unsigned long lastReconnectionAttempt = 0;
unsigned long lastWiFiCheck = 0;
unsigned long lastUDPRestart = 0;
const unsigned long HEARTBEAT_INTERVAL = 30000;
const unsigned long WIFI_CHECK_INTERVAL = 5000;
const unsigned long UDP_RESTART_INTERVAL = 600000;
const unsigned long HUB_TIMEOUT = 60000;
const unsigned long MAX_HUB_RECONNECT_ATTEMPTS = 5;
const unsigned long MIN_RECONNECTION_INTERVAL = 15000;
const unsigned long CONNECTION_CHECK_INTERVAL = 2000;

bool showingRegistrationStatus = false;
unsigned long registrationStatusStart = 0;
String registrationStatusMessage = "";
uint32_t registrationStatusColor = TH_GREEN;

static String adminMessageText = "";
static bool adminMessageActive = false;
static unsigned long adminMessageExpire = 0;
static uint32_t adminMessageBg = TH_BLUE;
static String adminMessageId = "";

bool showingAssignmentConfirmation = false;
unsigned long assignmentConfirmationStart = 0;
String confirmationSourceName = "";
bool confirmationIsAssigned = false;

unsigned long buttonPressStart = 0;
bool buttonWasPressed = false;
const unsigned long WIFI_RESET_HOLD_TIME = 5000;

void setupWiFi();
void setupWebServer();
void loadConfiguration();
void saveConfiguration();
void registerDevice();
void sendHeartbeat();
void handleUDPMessages();
void updateLED();
void updateStatus(const String &status);
void setLedColor(uint32_t color);
void handleRoot();
void handleConfig();
void handleSave();
void handleSources();
void handleStatus();
void handleAssign();
void handleUnassign();
void handleSaveDisplayName();
void handleReset();
void handleRestart();
void handleNotFound();
String formatUptime();
String cleanSourceName(String sourceName);
void checkButtonForWiFiReset();
void reconnectWiFi();
void restartUDP();
void checkWiFiConnection();
void ensureUDPConnection();

void setLedColor(uint32_t color)
{
  for (int i = 0; i < LED_COUNT; i++)
    leds_th.setPixelColor(i, color);
  leds_th.show();
}

void setup_mode_th()
{
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== ESP32 Tally Light (WS2812B) v" + String(FIRMWARE_VERSION) + " ===");
  bootTime = millis();

  leds_th.begin();
  leds_th.setBrightness(LED_BRIGHTNESS);
  setLedColor(TH_BLACK);
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

  macAddress = WiFi.macAddress();
  deviceID = "tally-" + macAddress;
  deviceID.replace(":", "");
  deviceID.toLowerCase();
  Serial.println("Device ID: " + deviceID);

  loadConfiguration();
  setupWiFi();

  if (WiFi.status() == WL_CONNECTED)
  {
    ipAddress = WiFi.localIP().toString();
    Serial.println("IP Address: " + ipAddress);
    setupWebServer();
    ArduinoOTA.setHostname(deviceID.c_str());
    ArduinoOTA.begin();
    if (udp_th.begin(3000))
    {
      Serial.println("UDP started on port 3000");
    }
    else
    {
      Serial.println("Failed to start UDP");
    }
    lastHubResponse = millis();
    registerDevice();
    updateStatus("READY");
  }
  else
  {
    updateStatus("NO_WIFI");
  }
  delay(500);
}

void checkHubConnection()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    if (isConnected || isRegisteredWithHub)
    {
      isConnected = false;
      isRegisteredWithHub = false;
      updateStatus("NO_WIFI");
    }
    return;
  }

  if (!isRegisteredWithHub)
  {
    unsigned long timeSinceLastAttempt = millis() - lastReconnectionAttempt;
    if (timeSinceLastAttempt < MIN_RECONNECTION_INTERVAL)
      return;

    if (hubConnectionAttempts < MAX_HUB_RECONNECT_ATTEMPTS)
    {
      hubConnectionAttempts++;
      lastReconnectionAttempt = millis();
      showingRegistrationStatus = true;
      registrationStatusStart = millis();
      registrationStatusMessage = "Connecting...";
      registrationStatusColor = TH_YELLOW;
      registerDevice();
    }
    else
    {
      static unsigned long lastAttemptReset = 0;
      if (millis() - lastAttemptReset > 300000)
      {
        hubConnectionAttempts = 0;
        lastAttemptReset = millis();
        return;
      }
      lastReconnectionAttempt = millis();
      showingRegistrationStatus = true;
      registrationStatusStart = millis();
      registrationStatusMessage = "Hub Lost";
      registrationStatusColor = TH_RED;
      isConnected = false;
      isRegisteredWithHub = false;
      registerDevice();
    }
    return;
  }

  unsigned long timeSinceLastResponse = millis() - lastHubResponse;
  if (timeSinceLastResponse > HUB_TIMEOUT)
  {
    unsigned long timeSinceLastAttempt = millis() - lastReconnectionAttempt;
    if (timeSinceLastAttempt < MIN_RECONNECTION_INTERVAL)
      return;

    isRegisteredWithHub = false;

    if (hubConnectionAttempts < MAX_HUB_RECONNECT_ATTEMPTS)
    {
      hubConnectionAttempts++;
      lastReconnectionAttempt = millis();
      showingRegistrationStatus = true;
      registrationStatusStart = millis();
      registrationStatusMessage = "Reconnecting...";
      registrationStatusColor = TH_YELLOW;
      registerDevice();
    }
    else
    {
      static unsigned long lastAttemptReset2 = 0;
      if (millis() - lastAttemptReset2 > 300000)
      {
        hubConnectionAttempts = 0;
        lastAttemptReset2 = millis();
        return;
      }
      lastReconnectionAttempt = millis();
      showingRegistrationStatus = true;
      registrationStatusStart = millis();
      registrationStatusMessage = "Hub Lost";
      registrationStatusColor = TH_RED;
      isConnected = false;
      isRegisteredWithHub = false;
      registerDevice();
    }
  }
}

void monitorConnectionStatus()
{
  static unsigned long lastConnectionCheck = 0;
  if (millis() - lastConnectionCheck > CONNECTION_CHECK_INTERVAL)
  {
    if (WiFi.status() != WL_CONNECTED)
    {
      if (isConnected || isRegisteredWithHub)
      {
        isConnected = false;
        isRegisteredWithHub = false;
        updateStatus("NO_WIFI");
        updateLED();
      }
    }
    if (WiFi.status() == WL_CONNECTED && (isConnected || isRegisteredWithHub))
    {
      unsigned long timeSinceLastResponse = millis() - lastHubResponse;
      if (timeSinceLastResponse > HUB_TIMEOUT)
      {
        isConnected = false;
        isRegisteredWithHub = false;
        updateStatus("HUB_LOST");
        updateLED();
      }
    }
    lastConnectionCheck = millis();
  }
}

void loop_mode_th()
{
  ArduinoOTA.handle();
  checkButtonForWiFiReset();
  monitorConnectionStatus();

  if (millis() - lastWiFiCheck > WIFI_CHECK_INTERVAL)
  {
    checkWiFiConnection();
    lastWiFiCheck = millis();
  }

  if (WiFi.status() != WL_CONNECTED)
  {
    if (isConnected || isRegisteredWithHub)
    {
      isConnected = false;
      isRegisteredWithHub = false;
    }
    updateStatus("NO_WIFI");
    updateLED();
    delay(500);
    return;
  }

  if (millis() - lastUDPRestart > UDP_RESTART_INTERVAL)
  {
    restartUDP();
    lastUDPRestart = millis();
  }

  static unsigned long lastUDPHealthCheck = 0;
  if (millis() - lastUDPHealthCheck > 300000)
  {
    ensureUDPConnection();
    lastUDPHealthCheck = millis();
  }

  server.handleClient();
  handleUDPMessages();
  checkHubConnection();

  if (millis() - lastHeartbeat > HEARTBEAT_INTERVAL)
  {
    sendHeartbeat();
    lastHeartbeat = millis();
  }

  if (millis() - lastLedUpdate > 200)
  {
    updateLED();
    lastLedUpdate = millis();
  }

  delay(20);
}

void checkButtonForWiFiReset()
{
  int buttonState = digitalRead(BOOT_BUTTON_PIN);
  if (buttonState == LOW)
  {
    if (!buttonWasPressed)
    {
      buttonPressStart = millis();
      buttonWasPressed = true;
    }
    else if (millis() - buttonPressStart > WIFI_RESET_HOLD_TIME)
    {
      setLedColor(TH_RED);
      delay(1000);
      wifiManager.resetSettings();
      preferences.begin("tally", false);
      preferences.clear();
      preferences.end();
      delay(500);
      ESP.restart();
    }
  }
  else
  {
    if (buttonWasPressed)
    {
      unsigned long held = millis() - buttonPressStart;
      if (held < WIFI_RESET_HOLD_TIME && adminMessageActive)
      {
        String snippet = adminMessageText.substring(0, 24);
        adminMessageActive = false;
        adminMessageText = "";
        JsonDocument ack;
        ack["type"] = "admin_message_ack";
        ack["deviceId"] = deviceID;
        ack["method"] = "button";
        ack["timestamp"] = millis();
        ack["textSnippet"] = snippet;
        if (adminMessageId.length() > 0)
          ack["id"] = adminMessageId;
        String payload;
        serializeJson(ack, payload);
        int r = udp_th.beginPacket(hubIP.c_str(), hubPort);
        if (r == 1)
        {
          udp_th.print(payload);
          udp_th.endPacket();
        }
        updateLED();
      }
    }
    buttonWasPressed = false;
  }
}

void setupWiFi()
{
  WiFi.mode(WIFI_STA);
  WiFi.setAutoConnect(true);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(WIFI_PS_NONE);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);

  wifiManager.setAPCallback([](WiFiManager *myWiFiManager)
                            {
    // No QR-code screen on LED-only hardware; blink blue to indicate AP/config mode.
    for (int i = 0; i < 3; i++) {
      setLedColor(TH_BLUE);
      delay(200);
      setLedColor(TH_BLACK);
      delay(200);
    } });

  wifiManager.setSaveConfigCallback([]()
                                    {
    setLedColor(TH_GREEN);
    delay(1000); });

  String apName = "TallyLight-" + deviceID.substring(6, 12);
  wifiManager.setBreakAfterConfig(true);
  bool connected = wifiManager.autoConnect(apName.c_str());
  if (!connected)
  {
    Serial.println("[WiFiManager] Failed to connect or no credentials. Starting AP mode.");
    setLedColor(TH_RED);
    delay(2000);
  }
  else
  {
    Serial.println("WiFi connected! IP: " + WiFi.localIP().toString());
  }
}

void setupWebServer()
{
  server.on("/", handleRoot);
  server.on("/config", handleConfig);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/sources", handleSources);
  server.on("/assign", HTTP_POST, handleAssign);
  server.on("/unassign", HTTP_POST, handleUnassign);
  server.on("/save_display_name", HTTP_POST, handleSaveDisplayName);
  server.on("/reset", HTTP_POST, handleReset);
  server.on("/restart", HTTP_POST, handleRestart);
  server.on("/status", handleStatus);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("HTTP server started");
}

void loadConfiguration()
{
  preferences.begin("tally", false);
  deviceName = preferences.getString("deviceName", "ESP32 Tally Light");
  hubIP = preferences.getString("hubIP");
  hubPort = preferences.getInt("hubPort");
  assignedSource = preferences.getString("assignedSource", "");
  assignedSourceName = preferences.getString("assignedSourceName", "");
  customDisplayName = preferences.getString("customDisplayName", "");
  preferences.end();
  isAssigned = (assignedSource.length() > 0);
}

void saveConfiguration()
{
  preferences.begin("tally", false);
  preferences.putString("deviceName", deviceName);
  preferences.putString("hubIP", hubIP);
  preferences.putInt("hubPort", hubPort);
  preferences.putString("assignedSource", assignedSource);
  preferences.putString("assignedSourceName", assignedSourceName);
  preferences.putString("customDisplayName", customDisplayName);
  preferences.end();
}

void registerDevice()
{
  if (WiFi.status() != WL_CONNECTED)
    return;
  ensureUDPConnection();

  JsonDocument doc;
  doc["type"] = "register";
  doc["deviceId"] = deviceID;
  doc["deviceName"] = deviceName;
  doc["deviceType"] = "esp32-ws2812b";
  doc["model"] = DEVICE_MODEL;
  doc["firmware"] = FIRMWARE_VERSION;
  doc["ip"] = ipAddress;
  doc["mac"] = macAddress;
  doc["wifiRSSI"] = WiFi.RSSI();
  doc["freeHeap"] = ESP.getFreeHeap();
  if (isAssigned && assignedSource.length() > 0)
  {
    doc["assignedSource"] = assignedSource;
    doc["isAssigned"] = true;
  }
  else
  {
    doc["isAssigned"] = false;
  }

  String message;
  serializeJson(doc, message);
  int result = udp_th.beginPacket(hubIP.c_str(), hubPort);
  if (result == 1)
  {
    udp_th.print(message);
    if (udp_th.endPacket() != 1)
      restartUDP();
  }
  else
  {
    restartUDP();
  }
}

void sendHeartbeat()
{
  if (!isRegisteredWithHub)
    return;
  ensureUDPConnection();

  JsonDocument doc;
  doc["type"] = "heartbeat";
  doc["deviceId"] = deviceID;
  doc["uptime"] = millis() - bootTime;
  doc["status"] = currentStatus;
  doc["assignedSource"] = assignedSource;
  doc["wifiRSSI"] = WiFi.RSSI();
  doc["freeHeap"] = ESP.getFreeHeap();

  String message;
  serializeJson(doc, message);
  int result = udp_th.beginPacket(hubIP.c_str(), hubPort);
  if (result == 1)
  {
    udp_th.print(message);
    if (udp_th.endPacket() != 1)
      restartUDP();
  }
  else
  {
    restartUDP();
  }
}

void handleUDPMessages()
{
  int packetSize = udp_th.parsePacket();
  if (packetSize == 0)
    return;

  char incomingPacket[512];
  int len = udp_th.read(incomingPacket, 511);
  if (len > 0)
    incomingPacket[len] = 0;

  lastHubResponse = millis();
  hubConnectionAttempts = 0;

  JsonDocument doc;
  if (deserializeJson(doc, incomingPacket) != DeserializationError::Ok)
    return;

  String type = doc["type"];

  if (type == "tally")
  {
    if (doc["data"].is<JsonObject>())
    {
      JsonObject data = doc["data"];
      String sourceId = data["id"];
      String sourceName = data["name"];
      bool program = data["program"];
      bool preview = data["preview"];
      bool recording = data["recording"] | false;
      bool streaming = data["streaming"] | false;

      if (isAssigned && assignedSource.length() > 0 && sourceId == assignedSource)
      {
        isProgram = program;
        isPreview = preview;
        isRecording = recording;
        isStreaming = streaming;
        if (customDisplayName.length() == 0)
          currentSource = cleanSourceName(sourceName);
        if (program)
          updateStatus("PROGRAM");
        else if (preview)
          updateStatus("PREVIEW");
        else
          updateStatus("IDLE");
      }
    }
    else
    {
      String sourceId = doc["sourceId"];
      bool program = doc["program"];
      bool preview = doc["preview"];
      bool recording = doc["recording"] | false;
      bool streaming = doc["streaming"] | false;

      if (isAssigned && assignedSource.length() > 0 && sourceId == assignedSource)
      {
        isProgram = program;
        isPreview = preview;
        isRecording = recording;
        isStreaming = streaming;
        if (program)
          updateStatus("PROGRAM");
        else if (preview)
          updateStatus("PREVIEW");
        else
          updateStatus("IDLE");
      }
    }
  }
  else if (type == "assignment")
  {
    if (doc["data"].is<JsonObject>())
    {
      JsonObject data = doc["data"];
      String newSource = data["sourceId"];
      String sourceName = data["sourceName"];
      String mode = data["mode"];

      if (mode == "assigned")
      {
        assignedSource = newSource;
        assignedSourceName = sourceName;
        isAssigned = true;
        currentSource = (customDisplayName.length() == 0) ? cleanSourceName(sourceName) : customDisplayName;
        saveConfiguration();
        showingAssignmentConfirmation = true;
        assignmentConfirmationStart = millis();
        confirmationSourceName = cleanSourceName(sourceName);
        confirmationIsAssigned = true;
        isProgram = isPreview = isRecording = isStreaming = false;
      }
      else
      {
        assignedSource = "";
        assignedSourceName = "";
        currentSource = "";
        customDisplayName = "";
        isAssigned = false;
        saveConfiguration();
        showingAssignmentConfirmation = true;
        assignmentConfirmationStart = millis();
        confirmationSourceName = "";
        confirmationIsAssigned = false;
        isProgram = isPreview = isRecording = isStreaming = false;
      }
    }
    else
    {
      String newSource = doc["sourceId"];
      if (newSource != assignedSource)
      {
        if (newSource.length() > 0)
        {
          assignedSource = newSource;
          isAssigned = true;
          saveConfiguration();
          showingAssignmentConfirmation = true;
          assignmentConfirmationStart = millis();
          confirmationSourceName = cleanSourceName(newSource);
          confirmationIsAssigned = true;
        }
        else
        {
          assignedSource = "";
          isAssigned = false;
          saveConfiguration();
          showingAssignmentConfirmation = true;
          assignmentConfirmationStart = millis();
          confirmationSourceName = "";
          confirmationIsAssigned = false;
        }
        isProgram = isPreview = isRecording = isStreaming = false;
      }
    }
  }
  else if (type == "register_required")
  {
    showingRegistrationStatus = true;
    registrationStatusStart = millis();
    registrationStatusMessage = "Re-register";
    registrationStatusColor = TH_YELLOW;
    registerDevice();
  }
  else if (type == "registered")
  {
    isRegisteredWithHub = true;
    hubConnectionAttempts = 0;
    if (!isAssigned || assignedSource.length() == 0)
      updateStatus("READY");
    showingRegistrationStatus = true;
    registrationStatusStart = millis();
    registrationStatusMessage = "Connected";
    registrationStatusColor = TH_GREEN;
  }
  else if (type == "heartbeat_ack")
  {
    hubConnectionAttempts = 0;
  }
  else if (type == "admin_message")
  {
    const char *txt = doc["text"] | "";
    if (strlen(txt) > 0)
    {
      adminMessageText = String(txt);
      adminMessageId = doc["id"].isNull() ? String("") : String((const char *)doc["id"].as<const char *>());
      unsigned long dur = doc["duration"].isNull() ? 8000UL : (unsigned long)doc["duration"].as<unsigned long>();
      if (dur < 1000UL)
        dur = 1000UL;
      if (dur > 30000UL)
        dur = 30000UL;
      adminMessageExpire = millis() + dur;
      adminMessageBg = TH_BLUE;
      if (!doc["color"].isNull())
      {
        String c = doc["color"].as<String>();
        if (c.startsWith("#"))
          c.remove(0, 1);
        if (c.length() == 6)
        {
          char rHex[3] = {c[0], c[1], '\0'};
          char gHex[3] = {c[2], c[3], '\0'};
          char bHex[3] = {c[4], c[5], '\0'};
          int r = strtol(rHex, nullptr, 16);
          int g = strtol(gHex, nullptr, 16);
          int b = strtol(bHex, nullptr, 16);
          adminMessageBg = leds_th.Color(r, g, b);
        }
      }
      adminMessageActive = true;
      updateLED();
    }
  }
}

void updateLED()
{
  if (adminMessageActive && millis() > adminMessageExpire)
  {
    adminMessageActive = false;
    adminMessageText = "";
  }

  // Admin message: blink the message color instead of showing text
  if (adminMessageActive && !showingAssignmentConfirmation && !showingRegistrationStatus)
  {
    setLedColor((millis() / 250) % 2 == 0 ? adminMessageBg : TH_BLACK);
    return;
  }

  // Assignment confirmation: solid green (assigned) or red (unassigned) for 2s
  if (showingAssignmentConfirmation)
  {
    if (millis() - assignmentConfirmationStart < 2000)
    {
      setLedColor(confirmationIsAssigned ? TH_GREEN : TH_RED);
      return;
    }
    else
    {
      showingAssignmentConfirmation = false;
    }
  }

  // Registration/reconnection status: brief blink
  if (showingRegistrationStatus)
  {
    unsigned long displayDuration = (registrationStatusMessage == "Re-register") ? 500 : 1000;
    if (millis() - registrationStatusStart < displayDuration)
    {
      setLedColor((millis() / 150) % 2 == 0 ? registrationStatusColor : TH_BLACK);
      return;
    }
    else
    {
      showingRegistrationStatus = false;
    }
  }

  if (WiFi.status() != WL_CONNECTED)
  {
    setLedColor((millis() / 400) % 2 == 0 ? TH_RED : TH_BLACK);
    return;
  }

  if (!isRegisteredWithHub)
  {
    unsigned long timeSinceLastResponse = millis() - lastHubResponse;
    if ((timeSinceLastResponse > HUB_TIMEOUT && lastHubResponse > 0) ||
        (lastHubResponse == 0 && millis() > 30000))
    {
      setLedColor((millis() / 400) % 2 == 0 ? TH_RED : TH_BLACK);
    }
    else
    {
      setLedColor((millis() / 400) % 2 == 0 ? TH_BLUE : TH_BLACK);
    }
    return;
  }

  if (!isAssigned)
  {
    setLedColor(TH_WHITE);
    return;
  }

  if (currentStatus == "PROGRAM")
  {
    setLedColor(TH_RED);
  }
  else if (currentStatus == "PREVIEW")
  {
    setLedColor(TH_GREEN);
  }
  else if (currentStatus == "IDLE")
  {
    setLedColor(TH_BLUE);
  }
  else if (currentStatus == "NO_WIFI")
  {
    setLedColor((millis() / 400) % 2 == 0 ? TH_RED : TH_BLACK);
  }
  else if (currentStatus == "HUB_LOST")
  {
    setLedColor((millis() / 400) % 2 == 0 ? TH_RED : TH_BLACK);
  }
  else
  {
    setLedColor(TH_BLACK);
  }
}

void updateStatus(const String &status)
{
  currentStatus = status;
}

void handleRoot()
{
  String html = "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>ESP32 Tally Configuration</title>";
  html += "<style>body{font-family:-apple-system,BlinkMacSystemFont,system-ui,sans-serif;";
  html += "background:#F2F2F7;margin:0;padding:2rem 1rem;} .container{max-width:480px;margin:0 auto;}";
  html += ".card{background:#fff;border-radius:16px;padding:1.5rem;margin-bottom:1.5rem;box-shadow:0 2px 10px rgba(0,0,0,0.08);}";
  html += "h1{font-size:22px;margin-bottom:1rem;} .form-group{margin-bottom:1rem;}";
  html += "label{font-size:13px;font-weight:600;margin-bottom:0.5rem;display:block;}";
  html += "input{border:1px solid #C7C7CC;border-radius:6px;padding:0.6rem 0.75rem;font-size:14px;width:100%;box-sizing:border-box;}";
  html += ".btn{border:none;padding:0.75rem 1.25rem;border-radius:10px;font-size:15px;font-weight:600;cursor:pointer;width:100%;margin-bottom:0.75rem;color:#fff;}";
  html += ".btn-primary{background:#007AFF;} .btn-secondary{background:#8E8E93;} .btn-danger{background:#FF3B30;}";
  html += "</style></head><body><div class='container'>";
  html += "<div class='card'><h1>ESP32 Tally Light</h1>";
  html += "<p>Device: " + deviceName + "</p><p>IP: " + WiFi.localIP().toString() + "</p>";
  html += "<p>Hub: " + hubIP + ":" + String(hubPort) + "</p></div>";
  html += "<div class='card'><h1>WiFi/Hub Configuration</h1>";
  html += "<form action='/save' method='post'>";
  html += "<div class='form-group'><label>Device Name</label>";
  html += "<input type='text' name='device_name' value='" + deviceName + "' required></div>";
  html += "<div class='form-group'><label>Hub Server IP</label>";
  html += "<input type='text' name='hub_ip' value='" + hubIP + "' required></div>";
  html += "<div class='form-group'><label>Hub Server Port</label>";
  html += "<input type='number' name='hub_port' value='" + String(hubPort) + "' min='1' max='65535' required></div>";
  html += "<div class='form-group'><label>Device ID</label>";
  html += "<input type='text' name='device_id' value='" + deviceID + "' required></div>";
  html += "<button type='submit' class='btn btn-primary'>Save Configuration</button></form></div>";
  html += "<div class='card'><h1>Device Actions</h1>";
  html += "<button onclick=\"window.location='/sources'\" class='btn btn-secondary'>Manage Sources</button>";
  html += "<button onclick=\"window.location='/status'\" class='btn btn-secondary'>Device Status</button>";
  html += "<button onclick='restart()' class='btn btn-secondary'>Restart Device</button>";
  html += "<button onclick='resetConfig()' class='btn btn-danger'>Factory Reset</button></div></div>";
  html += "<script>function restart(){if(confirm('Restart the device now?')){fetch('/restart',{method:'POST'}).then(()=>{alert('Device is restarting...');});}}";
  html += "function resetConfig(){if(confirm('WARNING: This will erase ALL settings!')){fetch('/reset',{method:'POST'}).then(()=>{alert('Factory reset complete.');});}}</script>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void handleConfig() { handleRoot(); }

void handleSave()
{
  deviceName = server.arg("device_name");
  hubIP = server.arg("hub_ip");
  hubPort = server.arg("hub_port").toInt();
  deviceID = server.arg("device_id");
  saveConfiguration();

  String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Configuration Saved</title></head><body style='font-family:sans-serif;text-align:center;padding:2rem;'>";
  html += "<h1>Configuration Saved!</h1><p>Restarting and connecting to " + hubIP + ":" + String(hubPort) + "...</p>";
  html += "</body></html>";
  server.send(200, "text/html", html);
  delay(1500);
  ESP.restart();
}

void handleRestart()
{
  server.send(200, "text/html", "<h2>Device Restarting</h2><p>Please wait...</p>");
  delay(1000);
  ESP.restart();
}

String formatUptime()
{
  unsigned long uptime = millis() - bootTime;
  unsigned long seconds = uptime / 1000;
  unsigned long minutes = seconds / 60;
  unsigned long hours = minutes / 60;
  unsigned long days = hours / 24;
  String result = "";
  if (days > 0)
    result += String(days) + "d ";
  if (hours % 24 > 0)
    result += String(hours % 24) + "h ";
  if (minutes % 60 > 0)
    result += String(minutes % 60) + "m ";
  result += String(seconds % 60) + "s";
  return result;
}

String cleanSourceName(String sourceName)
{
  String cleaned = sourceName;
  if (cleaned.startsWith("obs-"))
    cleaned = cleaned.substring(4);
  if (cleaned.startsWith("vmix-"))
    cleaned = cleaned.substring(5);
  if (cleaned.startsWith("source-"))
    cleaned = cleaned.substring(7);
  if (cleaned.startsWith("scene-"))
    cleaned = cleaned.substring(6);
  return cleaned;
}

void checkWiFiConnection()
{
  if (WiFi.status() != WL_CONNECTED)
    reconnectWiFi();
}

void reconnectWiFi()
{
  static unsigned long lastReconnectAttempt = 0;
  static int reconnectAttempts = 0;
  const unsigned long RECONNECT_INTERVAL = 30000;
  const int MAX_RECONNECT_ATTEMPTS = 10;

  if (millis() - lastReconnectAttempt < RECONNECT_INTERVAL)
    return;
  lastReconnectAttempt = millis();
  reconnectAttempts++;

  WiFi.disconnect();
  delay(1000);
  WiFi.mode(WIFI_STA);
  WiFi.begin();

  unsigned long connectStart = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - connectStart < 15000)
  {
    delay(500);
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    ipAddress = WiFi.localIP().toString();
    reconnectAttempts = 0;
    restartUDP();
    isConnected = false;
    isRegisteredWithHub = false;
    lastHubResponse = 0;
    hubConnectionAttempts = 0;
  }
  else if (reconnectAttempts >= MAX_RECONNECT_ATTEMPTS)
  {
    ESP.restart();
  }
}

void restartUDP()
{
  udp_th.stop();
  delay(100);
  udp_th.begin(7411);
}

void ensureUDPConnection()
{
  if (WiFi.status() == WL_CONNECTED)
  {
    static unsigned long lastUDPTest = 0;
    const unsigned long UDP_TEST_INTERVAL = 300000;
    if (millis() - lastUDPTest > UDP_TEST_INTERVAL)
    {
      JsonDocument doc;
      doc["type"] = "ping";
      doc["deviceId"] = deviceID;
      doc["timestamp"] = millis();
      String message;
      serializeJson(doc, message);
      int result = udp_th.beginPacket(hubIP.c_str(), hubPort);
      if (result == 1)
      {
        udp_th.print(message);
        udp_th.endPacket();
      }
      lastUDPTest = millis();
    }
  }
}

void handleSources()
{
  String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Source Management</title><style>body{font-family:sans-serif;background:#F2F2F7;padding:2rem;}";
  html += ".card{background:#fff;border-radius:16px;padding:1.5rem;margin-bottom:1.5rem;}";
  html += "input{width:100%;padding:0.6rem;box-sizing:border-box;margin-bottom:0.5rem;}";
  html += ".btn{padding:0.75rem 1.5rem;border:none;border-radius:10px;color:#fff;cursor:pointer;margin:0.25rem;}";
  html += ".btn-primary{background:#007AFF;} .btn-secondary{background:#8E8E93;} .btn-danger{background:#FF3B30;}";
  html += "</style></head><body>";
  html += "<div class='card'><h2>Current Assignment</h2>";
  html += "<p>Source ID: " + (assignedSource.length() > 0 ? assignedSource : "None") + "</p>";
  html += "<p>Custom Display Name: " + (customDisplayName.length() > 0 ? customDisplayName : "None") + "</p>";
  html += "<p>Current Source: " + (currentSource.length() > 0 ? currentSource : "None") + "</p></div>";
  html += "<div class='card'><h2>Custom Display Name</h2>";
  html += "<form action='/save_display_name' method='post'>";
  html += "<input type='text' name='display_name' value='" + customDisplayName + "' maxlength='20' placeholder='Leave empty to use source name'>";
  html += "<button type='submit' class='btn btn-primary'>Save Display Name</button></form></div>";
  html += "<div class='card'><h2>Manual Assignment</h2>";
  html += "<form action='/assign' method='post'>";
  html += "<input type='text' name='source' value='" + assignedSource + "' placeholder='Enter source ID'>";
  html += "<button type='submit' class='btn btn-primary'>Assign Source</button></form>";
  html += "<form action='/unassign' method='post' style='margin-top:1rem;'>";
  html += "<button type='submit' class='btn btn-danger'>Unassign Device</button></form></div>";
  html += "<div class='card'><a href='/' class='btn btn-secondary'>Back to Main</a></div>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void handleAssign()
{
  String sourceId = server.arg("source");
  if (sourceId.length() > 0)
  {
    assignedSource = sourceId;
    isAssigned = true;
    saveConfiguration();
    server.send(200, "text/html", "<h2>Source Assigned</h2><p>" + sourceId + "</p><script>setTimeout(()=>{window.location='/sources';},2000);</script>");
  }
  else
  {
    server.send(400, "text/plain", "Missing source parameter");
  }
}

void handleUnassign()
{
  assignedSource = "";
  isAssigned = false;
  customDisplayName = "";
  saveConfiguration();
  server.send(200, "text/html", "<h2>Device Unassigned</h2><script>setTimeout(()=>{window.location='/sources';},2000);</script>");
}

void handleSaveDisplayName()
{
  String displayName = server.arg("display_name");
  customDisplayName = displayName;
  if (customDisplayName.length() > 0)
    currentSource = customDisplayName;
  else if (assignedSource.length() > 0)
    currentSource = cleanSourceName(assignedSource);
  else
    currentSource = "";
  saveConfiguration();
  server.send(200, "text/html", "<h2>Display Name Saved</h2><script>setTimeout(()=>{window.location='/sources';},2000);</script>");
}

void handleReset()
{
  preferences.begin("tally", false);
  preferences.clear();
  preferences.end();
  server.send(200, "text/html", "<h2>Factory Reset Complete</h2><p>Device will restart in configuration mode...</p>");
  delay(1500);
  ESP.restart();
}

void handleStatus()
{
  String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Device Status</title></head><body style='font-family:sans-serif;padding:2rem;'>";
  html += "<h1>Device Status</h1>";
  html += "<p>Registration: " + String(isRegisteredWithHub ? "Registered" : "Not Registered") + "</p>";
  html += "<p>Tally State: " + String(isProgram ? "Program" : (isPreview ? "Preview" : "Off")) + "</p>";
  html += "<p>Uptime: " + formatUptime() + "</p>";
  html += "<a href='/'>Back to Main</a></body></html>";
  server.send(200, "text/html", html);
}

void handleNotFound()
{
  String message = "File Not Found\n\nURI: " + server.uri() + "\n";
  server.send(404, "text/plain", message);
}