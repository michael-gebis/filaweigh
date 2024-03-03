#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <HX711.h>
#include <Preferences.h>
#include <WiFi.h>

#include <mutex>

#include "config.h"
#include "favicon.h"
#include "index.h"
#include "rolling_average.h"
#include "secrets.h"

const int VERBOSE = 1;

// HX711 load cell amplifier settings
const int LOADCELL_DOUT_PIN = 16;
const int LOADCELL_SCK_PIN = 4;

HX711 g_scale; // Avoid direct usage; use g_hx711_values instead

// Wifi settings
//int retryCount = 0;            // Wifi connection retry count
const int kWifiMaxRetryCount = 10;  // Maximum number of retry attempts
// Set the values for the ssid and password in secrets.h
const char* ssid = SECRET_SSID;
const char* password = SECRET_PASSWORD;
const char* hostname = FILAWEIGH_HOSTNAME;

// Webserver settings
AsyncWebServer server(80);

// Weight values.  Take the mutex before using.
std::mutex weight_mtx;
double g_tare = 0.0;
double g_count_per_gram = 427.576f;
RollingAverage<10> g_hx711_values;
// A rolling average gives us a more stable value for display
// but at cost of having to wait for the average to stablize 
// after the weight on the load cell changed.
//
// The HX711 runs at 10 samples per second.
//
// For now, using 10 samples (thus a stabilization time of 1 second)
// seems like an OK tradeoff.

// Preferences library namespace and keys.  The library
// limits the namespace and attrib length to 16 characters max
const char* pref_namespace = "scale";
const char* pref_attrib_tare = "tare";
const char* pref_attrib_count_per_gram = "countpergram";

Preferences preferences;

void setupScale() {
  preferences.begin(pref_namespace, true); // true = readonly mode
  g_tare = preferences.getDouble(pref_attrib_tare, 0.0f);
  g_count_per_gram = preferences.getDouble(pref_attrib_count_per_gram, 427.0f);
  preferences.end();
}

void setupWiFi() {
  // For arduino-esp32 V2.0.14, calling setHostname(...) followed by
  // config(...) and prior to both mode() and begin() will correctly
  // set the hostname.

  // The above ordering shouldn't really be required; in an ideal
  // world, calling setHostname() any time before begin() should be ok.
  // I am hopeful this will be true in the future. But in any case,
  // this code is what works for me now.

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

  static int wifiRetryCount = 0;

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
    wifiRetryCount++;
    if (wifiRetryCount >= kWifiMaxRetryCount) {
      Serial.println("Failed to connect to WiFi. Restarting...");
      ESP.restart();  // If maximum retry count is reached, restart the board
    }
  }
  wifiRetryCount = 0;  // Reset retry count on successful connection
  Serial.println("Connected to WiFi");
  Serial.println("IP address: " + WiFi.localIP().toString());
}

////

// Design tradeoffs for handling Web APIs, specifically POST/PUT:
// Since these handlers are not processing normal HTML form data (which would be sent
// as "Content-Type: application/x-www-form-urlencoded") they cannot
// rely upon the normal parameter parsing logic for POST or PUT.

// To parse the JSON data sent as part of the body, I see two possibilties:
// - Use the five argument server.on(), and use the ArBodyHandlerFunction
//   callback to parse the body JSON data.
// - Use server.addHandler() with a AsyncCallbackJsonWebHandler.
//   The handler would have to check if the request was GET/POST/PUT for itself

// For now, I have chosen to use the ArBodyHandlerFunction solution.

// Further reading: https://github.com/me-no-dev/ESPAsyncWebServer/issues/195

// Example of ArBodyHandlerFunction at https://github.com/e-tinkers/esp32_ir_remote/blob/master/src/main.cpp
// Another example of ArBodyHandlerFunction: https://www.dfrobot.com/blog-1172.html
// Example of AsyncCallbackJsonWebHandler: https://raphaelpralat.medium.com/example-of-json-rest-api-for-esp32-4a5f64774a05

void setupWebServer()
{
  // Minmimal web server support
  server.on("/", HTTP_GET, handleGETRoot);
  server.on("/favicon.ico", HTTP_GET, handleGETFavicon);

  // API routes
  server.on("/api/v1/scale/weight", HTTP_GET, handleGETScaleWeight);
  server.on("/api/v1/scale/debug", HTTP_GET, handleGETScaleDebug);
  server.on("/api/v1/scale", HTTP_PUT, handlePUTScaleRequest, handlePUTScaleFileUpload, handlePUTScaleBody);
  server.on("/api/v1/network", HTTP_GET, handleGETNetwork);

  // Start the server
  server.begin(); 
}

void handleGETRoot(AsyncWebServerRequest* request) {
  if (VERBOSE) { Serial.println("handleGETRoot"); }

  String html = "</html>\n";
  html += g_web_contents_head;
  html += g_web_contents_body;
  html += "</html>\n";

  request->send(200, "text/html", html);
}

void handleGETFavicon(AsyncWebServerRequest* request) {
  if (VERBOSE) { Serial.println("handleGETFavicon"); }  
  request->send_P(200, "image/x-icon", favicon, sizeof(favicon));
}

void handleGETScaleWeight(AsyncWebServerRequest* request) {
  if (0 && VERBOSE) { Serial.println("handleGETScale");}
  
  double weight;
  double stddev;
  double count_per_gram;
  double tare;

  {
    std::lock_guard<std::mutex> lck(weight_mtx);
    weight = g_hx711_values.average();
    stddev = g_hx711_values.stddev();
    tare = g_tare;
    count_per_gram = g_count_per_gram;
  }

  JsonDocument data;
  String response;

  data["weight_g"] = String( (weight-tare) / count_per_gram );
  data["stddev_g"] = String( stddev / count_per_gram);

  serializeJson(data,response);
  request->send(200, "application/json", response);
}

void handleGETScaleDebug(AsyncWebServerRequest* request) {
  if (0 && VERBOSE) { Serial.println("handleGETScale");}
  
  double weight;
  double count_per_gram;
  double tare;

  {
    std::lock_guard<std::mutex> lck(weight_mtx);
    weight = g_hx711_values.average();
    //stddev = g_hx711_values.stddev();
    count_per_gram = g_count_per_gram;
    tare = g_tare;
  }

  JsonDocument data;
  String response;

  data["raw"] = String(weight);
  data["tare"] = String(tare);
  data["adjusted"] = String(weight-tare);
  data["count_per_gram"] = String(count_per_gram);

  serializeJson(data,response);
  request->send(200, "application/json", response);
}

void handlePUTScaleRequest(AsyncWebServerRequest *request) {
  Serial.println("handlePUTScaleRequest");
}

void handlePUTScaleFileUpload(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
  Serial.println("handlePUTScaleFileUpload");
}

void handlePUTScaleBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  Serial.println("handlePUTScaleBody");
  //double tare;
  //double count_per_gram;

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, (char*)data);
  if (!error) {
    if (doc.containsKey("tare")) {
      //Serial.println("tare found!");
      bool tare = doc["tare"];
      //Serial.println(tare ? "TRUE" : "FALSE");

      if (tare) {
        std::lock_guard<std::mutex> lck(weight_mtx);
        g_tare = g_hx711_values.average();
        preferences.begin(pref_namespace, false); // read/write
        preferences.putDouble(pref_attrib_tare, g_tare);
        preferences.end();
      }
    }
 
    if (doc.containsKey("calweight")) {
      // TODO: Sanitizing input!
      // TODO: Updating the global value
      // TODO: avoiding divide by zero!
      // TODO: saving to persistant memory
      Serial.println("calibration_weight found!");
      const char* calweight_txt = doc["calweight"];
      Serial.printf("calweight_txt='%s'\n", calweight_txt);

      double calweight = 0.0;
      try
      {
        calweight = std::stod(calweight_txt);
        Serial.printf("calweight_txt = '%s' = %0.4f\n", calweight_txt, calweight);
      }
      catch (const std::invalid_argument&) { Serial.println("caught std::invalid_argument"); }
      catch (const std::out_of_range&) { Serial.println("caught std::out_of_range"); }

      if (calweight >= 1.0) {
        double delta;

        {
          std::lock_guard<std::mutex> lck(weight_mtx);
          delta = g_hx711_values.average() - g_tare;
          g_count_per_gram = delta/calweight;
          preferences.begin(pref_namespace, false); // read/write
          preferences.putDouble(pref_attrib_count_per_gram, g_count_per_gram);
          preferences.end();          
        }

        Serial.printf("delta = %0.4f\n", delta);
        Serial.printf("g_count_per_gram = %0.4f\n", g_count_per_gram);

      } else {
        Serial.printf("calweight too low; ignored\n");
        // TODO: return error code to web API
      }
    }
  } else {
    Serial.println("handlePUTScaleBody ERROR");
  }

  JsonDocument responsedata;
  String response;
  serializeJson(responsedata,response);
  request->send(200, "application/json", response);  
}

void handleGETNetwork(AsyncWebServerRequest* request) {

  if (VERBOSE) { Serial.println("handleGETSettings"); }  
  JsonDocument data;
  String response;

  data["ipv4"] = String(WiFi.localIP().toString());
  data["ipv6"] = String(WiFi.localIPv6().toString());
  data["hostname"] = String(hostname);
  
  serializeJson(data,response);
  request->send(200, "application/json", response);
  //Serial.println(response);
}

void setup() {
  Serial.begin(115200);
  Serial.println("Starting Setup...");

  Serial.println("Initializing HX711...");
  g_scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  Serial.println("HX711 init done!");

  Serial.println("Reading saved scale tare and count_per_gram...");
  setupScale();
  Serial.println("Saved scale values done!");

  Serial.println("Starting WiFi...");
  setupWiFi();
  Serial.println("WiFi init done!");

  Serial.println("Starting WebServer...");
  setupWebServer();
  Serial.println("WebServer init done!");

  Serial.println("Setup Done!");
}

void read_hx711() {
  if (g_scale.wait_ready_timeout(1000)) {
    long reading = g_scale.read();

    {
      std::lock_guard<std::mutex> lck(weight_mtx);
      g_hx711_values.insert(reading);
    }
  } else {
    Serial.println("HX711 not found or not ready.");
  }
}

void loop() {
  // Max sample rate of the hx711 is 10 samples per second.
  // If read() or read_average() are called more often this,
  // they will block.
  read_hx711();

  // sleep to give other tasks time to run.
  delay(90); // milliseconds
}

