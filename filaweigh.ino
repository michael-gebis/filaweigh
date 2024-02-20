#include <Arduino.h>
//#include <WebServer.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>

#include "config.h"
#include "HX711.h"
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

void setupWebServer()
{
  // Add various routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/calibrate", HTTP_POST, handleCalibrate);
  server.on("/tare", HTTP_POST, handleTare);
  server.on("/weight", HTTP_GET, handleWeight);

  // Start the server
  server.begin(); 
}

void handleRoot(AsyncWebServerRequest* request) {
  String html = "<html><head>";
  html += "<title>Filaweigh</title>";
  html += "<title>Wandering Hour Clock</title>";
  html += "  <meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "  <style>";
  html += "    body {";
  html += "      font-family: Arial, Helvetica, sans-serif;";
  html += "    }";
  html += "";
  html += "    .container {";
  html += "      width: 100%;";
  html += "      max-width: 400px;";
  html += "      margin: 0 auto;";
  html += "      padding: 20px;";
  html += "    }";
  html += "";
  html += "    label {";
  html += "      display: block;";
  html += "      margin-bottom: 10px;";
  html += "    }";
  html += "";
  html += "    input[type='number'] {";
  html += "      width: 100%;";
  html += "      padding: 10px;";
  html += "      margin-bottom: 20px;";
  html += "      border: 1px solid #ccc;";
  html += "      border-radius: 4px;";
  html += "      box-sizing: border-box;";
  html += "    }";
  html += "";
  html += "    button {";
  html += "      background-color: #4CAF50;";
  html += "      color: white;";
  html += "      padding: 10px 20px;";
  html += "      border: none;";
  html += "      border-radius: 4px;";
  html += "      cursor: pointer;";
  html += "      width: 100%;";
  html += "    }";
  html += "";
  html += "    button:hover {";
  html += "      background-color: #45a049;";
  html += "    }";
  html += "  </style>";
  html += "</head><body>";

  html += "<div class='container'>";

  html += "<h2>Debug</h2>";
  html += "<br/><div>hostname = " + String(hostname) + "</div>";

  long reading = scale.read_average(3);
  html += "<br/><div>reading = " + String(reading) + "</div>";

  String esptime(esp_timer_get_time());
  html += "<br/><div>time=" + esptime  + "</div>";

  html += "</div>";


  request->send(200, "text/html", html);

}

void handleTare(AsyncWebServerRequest* request) {
  g_tare = scale.read_average(3);
}

void handleWeight(AsyncWebServerRequest* request) {
  request->send(200, "application/json", "");
}

void handleCalibrate(AsyncWebServerRequest* request) {
  request->send(200, "application/json", "");
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
  
  if (scale.is_ready()) {
    long reading = scale.read_average(3);
    Serial.print("HX711 reading: ");
    Serial.println(reading);
  } else {
    Serial.println("HX711 not found.");
  }

  delay(1000);
}

void loop() {
  l2();
}

