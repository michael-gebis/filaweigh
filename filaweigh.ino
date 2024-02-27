#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <HX711.h>
#include <WiFi.h>

#include <mutex>

#include "config.h"
#include "favicon.h"
#include "index.h"
#include "secrets.h"

const int VERBOSE = 1;

template<unsigned int N>
struct RollingAverage {
  long values[N];
  long valid_count = 0;
  long idx = 0;
  int64_t total_samples = 0;
  
  void insert(long value) {
    values[idx] = value;
    if (valid_count < N) {
      ++valid_count;
    }
    idx = (idx+1) % N;
    ++total_samples;
  }

  double average() {
    double sum = 0.0;

    if (valid_count == 0) { return 0.0; }

    for (int i = 0; i < valid_count; i++) {
      sum += values[i];
    }

    return sum/valid_count;
  }

  double stddev() {
    double a = this->average();
    double s = 0;

    if (valid_count <= 1) { return 0.0; }

    for (int i=0; i < valid_count; i++) {
      s += (a - values[i]) * (a - values[i]);
    }

    s /= (valid_count - 1);

    return std::sqrt(s);
  }

};

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

// Weight values
std::mutex weight_mtx;
long g_value = 0;
long g_tare = 0;
long g_calweight = 213245;

// A rolling average gives us a more stable value for display
// but at cost of having to wait for the average to stablize 
// after the weight on the load cell changed.
//
// The HX711 runs at 10 samples per second.
//
// For now, using 10 samples (thus a stabilization time of 1 second)
// seems like an OK tradeoff.
RollingAverage<10> g_hx711_values;

double g_grams_per_count = 1.0f/427.576f;

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

////

// Design tradeoffs for handling Web APIs, specifically POST/PUT:
// Since these handlers are not processing normal HTML form data (which would be sent
// as "Content-Type: application/x-www-form-urlencoded") they cannot
// rely upon the normal parameter parsing logic for POST or PUT.

// To parse the JSON data sent as part of the body, I see two possibilties:
// - Use the five argument server.on, and use the ArBodyHandlerFunction
//   callback to parse the body JSON data.
// - Use server.addHandler with a AsyncCallbackJsonWebHandler.
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
  server.on("/api/v1/scale", HTTP_GET, handleGETScale);
  server.on("/api/v1/scale", HTTP_PUT, handlePUTScaleRequest, handlePUTScaleFileUpload, handlePUTScaleBody); // Little weird
  server.on("/api/v1/settings", HTTP_GET, handleGETSettings);

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

void handleGETScale(AsyncWebServerRequest* request) {
  if (0 && VERBOSE) { Serial.println("handleGETScale");}
  
  double weight;
  double stddev;
  {
    std::lock_guard<std::mutex> lck(weight_mtx);
    weight = g_hx711_values.average();
    stddev = g_hx711_values.stddev();
  }
  JsonDocument data;
  String response;

  data["raw"] = String(weight);
  data["tare"] = String(g_tare);
  data["adjusted"] = String(weight-g_tare);
  data["weight_g"] = String( (weight-g_tare) * g_grams_per_count);
  data["stddev_g"] = String( stddev * g_grams_per_count);

  serializeJson(data,response);
  request->send(200, "application/json", response);
}

//  server.on("/api/v1/scale", HTTP_PUT, handlePUTScaleRequest, handlePUTScaleFileUpload, handlePUTScaleBody); // Little weird
void handlePUTScaleRequest(AsyncWebServerRequest *request) {
  Serial.println("handlePUTScaleRequest");
}

void handlePUTScaleFileUpload(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
  Serial.println("handlePUTScaleFileUpload");
}

void handlePUTScaleBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  Serial.println("handlePUTScaleBody");
  
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, (char*)data);
  if (!error) {
    if (doc.containsKey("tare")) {
      Serial.println("tare found!");
      bool tare = doc["tare"];
      Serial.println(tare ? "TRUE" : "FALSE");

      if (tare) {
        std::lock_guard<std::mutex> lck(weight_mtx);
        g_tare = g_hx711_values.average();
      }
    }
 
    if (doc.containsKey("calweight")) {
      Serial.println("calibration_weight found!");
      const char* calweight = doc["calweight"];
      Serial.println(calweight);
    }
  } else {
    Serial.println("handlePUTScaleBody ERROR");
  }

  JsonDocument responsedata;
  String response;
  serializeJson(responsedata,response);
  request->send(200, "application/json", response);  
}

/* 
void handlePUTScale(AsyncWebServerRequest* request) {
  if (VERBOSE) { Serial.println("handlePUTScale");}

  JsonDocument data;
  String response;
  serializeJson(data,response);
  request->send(200, "application/json", response);
}
*/

void handleGETSettings(AsyncWebServerRequest* request) {

  if (VERBOSE) { Serial.println("handleGETSettings"); }  
  JsonDocument data;
  String response;

  data["ipv4"] = String(WiFi.localIP().toString());
  data["ipv6"] = String(WiFi.localIPv6().toString());
  
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

void read_hx711() {
  if (scale.wait_ready_timeout(1000)) {
    long reading = scale.read();

    {
      std::lock_guard<std::mutex> lck(weight_mtx);
      g_value = reading;
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

