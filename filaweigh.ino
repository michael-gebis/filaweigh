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

  long average() {
    long sum = 0;

    if (valid_count == 0) { return 0; }

    for (int i = 0; i < valid_count; i++) {
      sum += values[i];
    }

    return sum/valid_count;
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


float g_grams_per_count = 1.0f/427.576f;

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
void handleRoot(AsyncWebServerRequest* request);
void handleCalibrate(AsyncWebServerRequest* request);
void handleTare(AsyncWebServerRequest* request);
void handleWeightJson(AsyncWebServerRequest* request);
void handleGetSettings(AsyncWebServerRequest* request);
void handleCommand(AsyncWebServerRequest* request);
void handleFavicon(AsyncWebServerRequest* request);

void setupWebServer()
{
  // Add various routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/calibrate", HTTP_POST, handleCalibrate);
  server.on("/tare", HTTP_POST, handleTare);
  server.on("/weight", HTTP_GET, handleWeightJson);
  server.on("/settings", HTTP_GET, handleGetSettings);
  server.on("/command", HTTP_POST, handleCommand);
  server.on("/favicon.ico", HTTP_GET, handleFavicon);
/*
  server.on("/", HTTP_GET, handleRoot);
  server.on("/scale", HTTP_GET, handleScaleGet);
  server.on("/scale", HTTP_PUT, handleScalePut);
  server.on("/network", HTTP_GET, handleNetworkGet);
  // server.on("/network", HTTP_PUT, handleNetworkPut);
*/
  // Start the server
  server.begin(); 
}

const char* g_web_contents_body = R"=====(
  <body>
  <h1>Thoth Network Scale at HOSTNAME</h1>
  <p>HX711 reading 
    <span style="color:green;"> 
      raw:<span id="raw">Loading...</span> 
      tare:<span id="tare">Loading</span> 
      adjusted:<span id="adjusted">Loading...</span>
      weight(g):<span id="weight_g">Loading...</span>
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
          document.getElementById("weight_g").textContent = data.weight_g;
          
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
      //xhr.onreadystatechange = function() {
      //  if (xhr.readyState === 4 && xhr.status === 200) {
      //
      //    // Print received data from server
      //    result.innerHTML = this.responseText;
      //  }
      //}        
      var data = JSON.stringify({"dummy": "foo"});
      xhr.send(data);
    }
    fetchWeight();
    fetchSettings();
    setInterval(fetchWeight, 1000);
    //setInterval(fetchSettings, 2000);
  </script>
  <h1>Commands</h1>
  <p>
    <button onclick="sendTare()">Tare</button>
  </p>
  </body>
)=====";

void handleRoot(AsyncWebServerRequest* request) {
  String html = "</html>\n";
  html += g_web_contents_head;
  html += g_web_contents_body;
  html += "</html>\n";

  request->send(200, "text/html", html);
}

void handleFavicon(AsyncWebServerRequest* request) {
  Serial.println("Sending favicon");
  request->send_P(200, "image/x-icon",  (uint8_t*)favicon, sizeof(favicon));
}

void handleTare(AsyncWebServerRequest* request) {
  Serial.println("handleTare");
  {
    std::lock_guard<std::mutex> lck(weight_mtx);
    //g_tare = g_value;
    g_tare = g_hx711_values.average();
  }
  Serial.println("tare set!");
  JsonDocument data;
  data["tare"] = g_tare;
  String response;
  serializeJson(data,response);
  request->send(200, "application/json", response);
  Serial.println(response);
  //request->send(200, "text/plain", weightStr);
}

void handleWeightJson(AsyncWebServerRequest* request) {
  long weight;
  {
    std::lock_guard<std::mutex> lck(weight_mtx);
    //weight = g_value;
    weight = g_hx711_values.average();
  }
  JsonDocument data;
  String response;

  data["raw"] = String(weight);
  data["tare"] = String(g_tare);
  data["adjusted"] = String(weight-g_tare);
  data["weight_g"] = String( (weight-g_tare) * g_grams_per_count);

  serializeJson(data,response);
  request->send(200, "application/json", response);
  //Serial.println(response);
}

void handleCalibrate(AsyncWebServerRequest* request) {
//  if(request->isPost()) {
//
//    if(request->hasParam("calibrationWeightGrams")) {
//      float calweight = request->getParam("calibrationWeightGrams")->value()->toFloat();
//      Serial.println(calweight);
//    }
//
//    request->send(200, "application/json", "");
//    return;
//  } 
  
  Serial.println("Got a calibrate that was not a POST");
  request->send(200, "application/json", "");
}

void handleCommand(AsyncWebServerRequest* request) {
  JsonDocument data;
  String response;

  request->send(200, "application/json", "");
}

void handleGetSettings(AsyncWebServerRequest* request) {
  Serial.println("handleGetSettings");
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
  
  //if (scale.is_ready()) {
  if (scale.wait_ready_timeout(1000)) {
    //int64_t start = esp_timer_get_time();
    long reading = scale.read_average(1);
    //int64_t stop = esp_timer_get_time();
    //Serial.print("HX711 value: ");
    //Serial.println(reading);
    //Serial.print("time(us):");
    //Serial.println(stop-start);

    {
      std::lock_guard<std::mutex> lck(weight_mtx);
      g_value = reading;
      g_hx711_values.insert(reading);
    }
    //Serial.print("HX711 rolling average:");
    //Serial.println(g_hx711_values.average());
  } else {
    Serial.println("HX711 not found or not ready.");
  }

  // sleep to give other tasks time to run.  This also gives time
  // for the HX711 to "recover".  Max sample rate is 10 samples per second.
  delay(90); // milliseconds

}

void loop() {
  read_hx711();
}

