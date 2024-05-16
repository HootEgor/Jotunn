#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <IRrecv.h>
#include <IRac.h>
#include <IRutils.h>

#include <OneWire.h>
#include <DallasTemperature.h>

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

#include <EEPROM.h>
#include <ArduinoWebsockets.h>
#include <ArduinoJson.h>


const char* apSSID = "Jotunn"; //address: 192.168.4.1
const char* apPassword = "password";
char ssid[32] = "";
char password[32] = "";
char username[32] = "";
struct WiFiSettings {
  char ssid[32];
  char password[32];
  char username[32];
};
bool config = false;

const char* websockets_server_host = "hoot.com.ua"; //Enter server adress
const uint16_t websockets_server_port = 8081; // Enter server port

using namespace websockets;
WebsocketsClient client;

ESP8266WebServer server(80);

// Data wire is plugged into port 2 on the Arduino
#define ONE_WIRE_BUS 2 //d4
// GPIO to use to control the IR LED circuit. Recommended: 4 (D2).
const uint16_t kIrLed = 4; //d2 
IRac ac(kIrLed); 

const uint16_t kRecvPin = 14; //(D5)

const uint32_t kBaudRate = 115200;

const uint16_t kCaptureBufferSize = 1024;
const uint8_t kTimeout = 50; 

const uint16_t kFrequency = 38; 

unsigned long previousMillis = 0;
unsigned long previousMillisRead = 0;
const long intervalSend = 10000;
const long intervalRead = 2000;
float sumTemp = 0;
int readingsCount = 0; 

// The IR transmitter.
IRsend irsend(kIrLed);
// The IR receiver.
IRrecv irrecv(kRecvPin, kCaptureBufferSize, kTimeout, false);
// Somewhere to store the captured message.
decode_results results;

// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature.
DallasTemperature sensors(&oneWire);


void setup ()
{
  irrecv.enableIRIn();  // Start up the IR receiver.
  irsend.begin(); 

  Serial.begin(kBaudRate, SERIAL_8N1);

  while (!Serial)  // Wait for the serial connection to be establised.
    delay(50);
  Serial.println();

  Serial.print("DumbIRRepeater is now running and waiting for IR input "
               "on Pin ");
  Serial.println(kRecvPin);
  Serial.print("and will retransmit it on Pin ");
  Serial.println(kIrLed);

  Serial.println("Dallas Temperature IC Control Library Demo");

  acInit();

  WiFi.softAP(apSSID, apPassword);
  
  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.begin();
  
  Serial.println("HTTP server started");

  connectToWiFi();
  sensors.begin();
}

void loop ()
{
  delay(100);
  if (WiFi.status() == WL_CONNECTED) {
    if(!client.available()) {
      connectToWebSocket();
    }else{
      client.poll();
      if(config){
        readACprotocol();
      }

      agrigateTemp();
    }
  }else{
    connectToWiFi();
  }

  server.handleClient();
}

void agrigateTemp(){
  
  if (ac.next.power) {
    unsigned long currentMillis = millis();
    if (currentMillis - previousMillis >= intervalSend) {
      previousMillis = currentMillis;

      if (readingsCount > 0) {
        float averageTemp = sumTemp / readingsCount;  
        sendTempToServer(averageTemp);
        Serial.print("Temperature is: ");
        Serial.println(averageTemp);
        sumTemp = 0;
        readingsCount = 0;
      } else {
        Serial.println("No temperature readings for the last 10 sec.");
      }
    }

    if (currentMillis - previousMillisRead >= intervalRead) {
      previousMillisRead = currentMillis;
      getTemperature(); 
    }
  }
}


void getTemperature() {
  sensors.requestTemperatures();

  float tempC = sensors.getTempCByIndex(0);

  sumTemp += tempC;
  readingsCount++;
}

void readACprotocol(){
  if (irrecv.decode(&results)) {  // We have captured something.
    // The capture has stopped at this point.
    decode_type_t protocol = results.decode_type;
    // If the protocol is supported by the IRac class ...
    if (ac.isProtocolSupported(protocol)) {
      Serial.println("Protocol " + String(protocol) + " / " +
                     typeToString(protocol) + " is supported.");
      ac.next.protocol = protocol;  // Change the protocol used.
      sengConfigToServer();
      config = false;
    }
    else{
      Serial.println("Protocol " + String(protocol) + " / " +
                     typeToString(protocol) + " isn't supported.");
    }
  }
  Serial.println("Nothing captured ...");
}

void handleRoot() {
  String page = "<!DOCTYPE html><html><head><title>WiFi Setup</title>";
  page += "<style>body {font-family: Arial, sans-serif; text-align: center;}";
  page += "form {margin: 0 auto; max-width: 300px;}</style></head><body>";
  page += "<h1>Setup Wi-Fi</h1>";
  page += "<form method='post' action='/save'>";
  page += "WiFi name: <br><input type='text' name='ssid' style='width: 100%;'><br>";
  page += "Password: <br><input type='password' name='password' style='width: 100%;'><br>";
  page += "Telegram username: @<br><input type='text' name='username' style='width: 100%;'><br>";
  page += "<input type='submit' value='Save' style='width: 100%;'>";
  page += "</form></body></html>";

  server.send(200, "text/html", page);
}


void handleSave() {
  String newSSID = server.arg("ssid");
  String newPassword = server.arg("password");
  String newUsername = server.arg("username");
  
  newSSID.toCharArray(ssid, 32);
  newPassword.toCharArray(password, 32);
  newUsername.toCharArray(username, 32);

  WiFiSettings settings;
  strncpy(settings.ssid, ssid, sizeof(settings.ssid));
  strncpy(settings.password, password, sizeof(settings.password));
  strncpy(settings.username, username, sizeof(settings.username));
  saveWiFiSettings(settings);

  Serial.print("SSID: ");
  Serial.print(settings.ssid);
  Serial.print(" Password: ");
  Serial.println(settings.password);
  Serial.print(" Username: ");
  Serial.println(settings.username);
  
  server.send(200, "text/plain", "Saved");
}

void saveWiFiSettings(const WiFiSettings& settings) {
  EEPROM.begin(sizeof(WiFiSettings));
  EEPROM.put(0, settings);
  EEPROM.commit();
  EEPROM.end();
}

WiFiSettings readWiFiSettings() {
  WiFiSettings settings;
  EEPROM.begin(sizeof(WiFiSettings));
  EEPROM.get(0, settings);
  EEPROM.end();
  return settings;
}

void connectToWiFi(){
  WiFiSettings settings = readWiFiSettings();
  if (strlen(settings.ssid) > 0 && strlen(settings.password) > 0) {
    Serial.println("Found saved WiFi settings. Attempting connection...");
    WiFi.begin(settings.ssid, settings.password);
    int attempts = 0;
    Serial.print("SSID: ");
    Serial.print(settings.ssid);
    Serial.print(" Password: ");
    Serial.println(settings.password);
    Serial.print(" Username: ");
    Serial.println(settings.username);
    while (WiFi.status() != WL_CONNECTED && attempts < 5) {
      delay(1000);
      Serial.println("Connecting to WiFi...");
      attempts++;
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("Connected to WiFi");
    } else {
      Serial.println("Failed to connect to WiFi after 5 attempts");
    }
  }
}

void connectToWebSocket() {
  bool connected = client.connect(websockets_server_host, websockets_server_port, "/");
    if(connected) {
      sengConfigToServer();
    } else {
        Serial.println("Not Connected!");
    }
    
    msgHendler();
}

void sendTempToServer(float temp){
    if(client.available()) {
      String message = convertTempToJSON(temp);
      client.send(message);
    }else{
      Serial.println("Faild to send temp!");
    }
}


void sengConfigToServer(){
    if(client.available()) {
      String message = convertConfigToJSON();
      client.send(message);
    }else{
      Serial.println("Faild to send config!");
    }
}


void msgHendler() {
  client.onMessage([&](WebsocketsMessage message) {
        Serial.print("Got Message: ");
        parseACconf(message);
    });
}

String convertTempToJSON(float temp) {
  StaticJsonDocument<512> doc;
    WiFiSettings settings = readWiFiSettings();
    doc["temperature"] = temp;

    String json;
    serializeJson(doc, json);
    return json;
}

String convertConfigToJSON() {
  StaticJsonDocument<512> doc;
    WiFiSettings settings = readWiFiSettings();
    doc["username"] = settings.username;
    doc["config"] = config;
    doc["protocol"] = static_cast<int>(ac.next.protocol);
    doc["model"] = ac.next.model;
    doc["mode"] = static_cast<int>(ac.next.mode);
    doc["celsius"] = ac.next.celsius;
    doc["degrees"] = ac.next.degrees;
    doc["fanspeed"] = static_cast<int>(ac.next.fanspeed);
    doc["swingv"] = static_cast<int>(ac.next.swingv);
    doc["swingh"] = static_cast<int>(ac.next.swingh);
    doc["light"] = ac.next.light;
    doc["beep"] = ac.next.beep;
    doc["econo"] = ac.next.econo;
    doc["filter"] = ac.next.filter;
    doc["turbo"] = ac.next.turbo;
    doc["quiet"] = ac.next.quiet;
    doc["sleep"] = ac.next.sleep;
    doc["clean"] = ac.next.clean;
    doc["clock"] = ac.next.clock;
    doc["power"] = ac.next.power;

    String json;
    serializeJson(doc, json);
    return json;
}

void parseACconf(WebsocketsMessage message){
    Serial.print("deserializeJson: ");
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, message.data());

    if (error) {
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.c_str());
        return;
    }

    config = doc["config"];
    ac.next.protocol = static_cast<decode_type_t>(doc["protocol"].as<int>());
    ac.next.model = doc["model"];
    ac.next.mode = static_cast<stdAc::opmode_t>(doc["mode"].as<int>());
    ac.next.celsius = doc["celsius"];
    ac.next.degrees = doc["degrees"];
    ac.next.fanspeed = static_cast<stdAc::fanspeed_t>(doc["fanspeed"].as<int>());
    ac.next.swingv = static_cast<stdAc::swingv_t>(doc["swingv"].as<int>());
    ac.next.swingh = static_cast<stdAc::swingh_t>(doc["swingh"].as<int>());
    ac.next.light = doc["light"];
    ac.next.beep = doc["beep"];
    ac.next.econo = doc["econo"];
    ac.next.filter = doc["filter"];
    ac.next.turbo = doc["turbo"];
    ac.next.quiet = doc["quiet"];
    ac.next.sleep = doc["sleep"];
    ac.next.clean = doc["clean"];
    ac.next.clock = doc["clock"];
    ac.next.power = doc["power"];

    Serial.println(convertConfigToJSON());
    Serial.print("Protocol: ");
    Serial.println(ac.next.protocol);

    ac.sendAc();
}

void acInit(){
  // Set up what we want to send.
  // See state_t, opmode_t, fanspeed_t, swingv_t, & swingh_t in IRsend.h for
  // all the various options.
  ac.next.protocol = decode_type_t::DAIKIN;  // Set a protocol to use.
  ac.next.model = 1;  // Some A/Cs have different models. Try just the first.
  ac.next.mode = stdAc::opmode_t::kCool;  // Run in cool mode initially.
  ac.next.celsius = true;  // Use Celsius for temp units. False = Fahrenheit
  ac.next.degrees = 25;  // 25 degrees.
  ac.next.fanspeed = stdAc::fanspeed_t::kMedium;  // Start the fan at medium.
  ac.next.swingv = stdAc::swingv_t::kOff;  // Don't swing the fan up or down.
  ac.next.swingh = stdAc::swingh_t::kOff;  // Don't swing the fan left or right.
  ac.next.light = false;  // Turn off any LED/Lights/Display that we can.
  ac.next.beep = false;  // Turn off any beep from the A/C if we can.
  ac.next.econo = false;  // Turn off any economy modes if we can.
  ac.next.filter = false;  // Turn off any Ion/Mold/Health filters if we can.
  ac.next.turbo = false;  // Don't use any turbo/powerful/etc modes.
  ac.next.quiet = false;  // Don't use any quiet/silent/etc modes.
  ac.next.sleep = -1;  // Don't set any sleep time or modes.
  ac.next.clean = false;  // Turn off any Cleaning options if we can.
  ac.next.clock = -1;  // Don't set any current time if we can avoid it.
  ac.next.power = false;  // Initially start with the unit off.

  Serial.println("Try to turn on & off every supported A/C type ...");
}


