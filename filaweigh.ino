#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <HX711.h>
#include <WiFi.h>

#include "config.h"
//#include "HX711.h"
#include "index.h"
#include "secrets.h"

// HX711 load cell amplifier settings
const int LOADCELL_DOUT_PIN = 16;
const int LOADCELL_SCK_PIN = 4;
HX711 scale;

// Wifi settings
int retryCount = 0;            // Wifi connection retry count
const int maxRetryCount = 10;  // Maximum number of retry attempts
// Set the values for the ssid and password in secrets.h
const char* ssid = SECRET_SSID;
const char* password = SECRET_PASSWORD;
const char* hostname = FILAWEIGH_HOSTNAME;

// Webserver settings
AsyncWebServer server(80);

long g_tare = 0;

void setupWiFi() {
  // For arduino-esp32 V2.0.14, calling setHostname(...) followed by
  // config(...) and prior to both mode() and begin() will correctly
  // set the hostname.

  // The above ordering shouldn't really be required; in an ideal
  // world, calling setHostname() any time before begin() should be ok.
  // I am hopeful this will remain true in the future.  But in any case,
  // this is what works for me now.

  // Note that calling getHostname() isn't a reliable way to verify
  // the hostname, because getHostname() reads the current internal
  // variable, which may NOT have been the name sent in the actual
  // DHCP request. Thus the result from getHostname() may be out of
  // sync with the DHCP server.

  // For a little more info, please see:
  // https://github.com/tzapu/WiFiManager/issues/1403

  WiFi.setHostname(hostname);
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
  WiFi.mode(WIFI_STA);
  WiFi.enableIpV6();
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
    retryCount++;
    if (retryCount >= maxRetryCount) {
      Serial.println("Failed to connect to WiFi. Restarting...");
      ESP.restart();  // If maximum retry count is reached, restart the board
    }
  }
  retryCount = 0;  // Reset retry count on successful connection
  Serial.println("Connected to WiFi");
  Serial.println("IP address: " + WiFi.localIP().toString());
}
void handleRoot(AsyncWebServerRequest* request);
void handleCalibrate(AsyncWebServerRequest* request);
void handleTare(AsyncWebServerRequest* request);
void handleWeightJson(AsyncWebServerRequest* request);
void handleGetSettings(AsyncWebServerRequest* request);

void setupWebServer()
{
  // Add various routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/calibrate", HTTP_POST, handleCalibrate);
  server.on("/tare", HTTP_POST, handleTare);
  server.on("/weight", HTTP_GET, handleWeightJson);
  server.on("/settings", HTTP_GET, handleGetSettings);

  // Start the server
  server.begin(); 
}

const char* g_web_contents_body = R"=====(
  <h1>Data</h1>
  <p>HX711 reading: <span style="color:green;"> <span id="weight">Loading...</span> </span></p>
  <script>
    function fetchWeight() {
      fetch("/weight")
        .then(response => response.text())
        .then(data => {
          document.getElementById("weight").textContent = data;
        });
    }
    fetchWeight();
    setInterval(fetchWeight, 500);
  </script>
)=====";


const char* g_web_contents_body_json = R"=====(
  <h1>Data</h1>
  <p>HX711 reading 
    <span style="color:green;"> 
      raw:<span id="raw">Loading...</span> 
      tare:<span id="tare">Loading</span> 
      adjusted:<span id="adjusted">Loading...</span>
    </span>
  </p>

  <h1>Settings</h1>
  <p>IPv4: <span id="ipv4">Loading...</span></p>
  <p>IPv6: <span id="ipv6">Loading...</span></p>
  <script>
    function fetchWeight() {
      fetch("/weight")
        .then(response => response.json())
        .then(data => {
          document.getElementById("raw").textContent = data.raw;
          document.getElementById("tare").textContent = data.tare;
          document.getElementById("adjusted").textContent = data.adjusted;
          
        })
        .catch(console.error);
    }

    function fetchSettings() {
      fetch("/settings")
        .then(response => response.json())
        .then(data => {
          document.getElementById("ipv4").textContent = data.ipv4;
          document.getElementById("ipv6").textContent = data.ipv6;
        })
        .catch(console.error);      
    }

    function sendTare() {
      let xhr = new XMLHttpRequest();
      let url = "tare";

      xhr.open("POST", url, true);
      xhr.onreadystatechange = function() {
        if (xhr.readyState === 4 && xhr.status === 200) {

          // Print received data from server
          result.innerHTML = this.responseText;
        }
      }        
      var data = JSON.stringify({"dummy": "foo"});
      xhr.send(data);
    }
    fetchWeight();
    fetchSettings();
    setInterval(fetchWeight, 1000);
    //setInterval(fetchSettings, 2000);
  </script>
  <h1>Actions</h1>
  <p>
    <button onclick="sendTare()">Tare</button>
  </p1>
)=====";

void handleRoot(AsyncWebServerRequest* request) {
  String html = g_web_contents_head;

  // Body
  html += "<body>";
  html += g_web_contents_body_json;

  html += "<div class='container'>";

  html += "<h2>Debug</h2>";
  html += "<br/><div>hostname = " + String(hostname) + "</div>";

  long reading = scale.read_average(3);
  html += "<br/><div>reading = " + String(reading) + "</div>";

  String esptime(esp_timer_get_time());
  html += "<br/><div>time=" + esptime  + "</div>";

  html += "</div>";
  html += "</body>";
  html += "</html>";


  request->send(200, "text/html", html);

}

void handleTare(AsyncWebServerRequest* request) {
  Serial.println("handleTare");
  g_tare = scale.read_average(20);
  Serial.println("tare set!");
  JsonDocument data;
  data["tare"] = g_tare;
  String response;
  serializeJson(data,response);
  request->send(200, "application/json", response);
  Serial.println(response);
  //request->send(200, "text/plain", weightStr);
}

void handleWeight(AsyncWebServerRequest* request) {
  long weight = scale.read_average(10);
  String weightStr = String(weight);
  request->send(200, "text/plain", weightStr);
  //request->send(200, "application/json", "");
}

void handleWeightJson(AsyncWebServerRequest* request) {
  long weight = scale.read_average(3); // blocking
  //String weightStr = String(weight);
  JsonDocument data;
  data["raw"] = String(weight);
  data["tare"] = String(g_tare);
  data["adjusted"] = String(weight-g_tare);
  String response;
  serializeJson(data,response);
  request->send(200, "application/json", response);
  Serial.println(response);
}

void handleCalibrate(AsyncWebServerRequest* request) {
  request->send(200, "application/json", "");
}

void handleGetSettings(AsyncWebServerRequest* request) {
  //long weight = scale.read_average(3); // blocking
  //String weightStr = String(weight);
  Serial.println("handleGetSettings");
  JsonDocument data;
  data["ipv4"] = String(WiFi.localIP().toString());
  data["ipv6"] = String(WiFi.localIPv6().toString());
  String response;
  serializeJson(data,response);
  request->send(200, "application/json", response);
  Serial.println(response);
}

void setup() {
  Serial.begin(115200);
  Serial.println("Starting Setup...");

  Serial.println("Initializing HX711...");
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  Serial.println("HX711 init done!");

  Serial.println("Starting WiFi...");
  setupWiFi();
  Serial.println("WiFi init done!");

  Serial.println("Starting WebServer...");
  setupWebServer();
  Serial.println("WebServer init done!");

  Serial.println("Setup Done!");
}

void l2() {
  
  //if (scale.is_ready()) {
  if (scale.wait_ready_timeout(200)) {
    long reading = scale.read_average(1);
    Serial.print("HX711 reading: ");
    Serial.println(reading);
  } else {
    Serial.println("HX711 not found or not ready.");
  }

  delay(5000);
}

void loop() {
  l2();
  //Serial.println(WiFi.localIPv6());
  //Serial.println(WiFi.localIP());
}

