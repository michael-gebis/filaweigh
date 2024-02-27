#ifndef INDEX_H
#define INDEX_H

const char* g_web_contents_head = R"=====(
  <head>
    <link rel="icon" type="image/x-icon" href="/favicon.ico">  
    <title>Filaweigh</title>
    <meta name='viewport' content='width=device-width, initial-scale=1'>
    <style>
      body {
        font-family: Arial, Helvetica, sans-serif;
      }
      .container {
        width: 100%;
        max-width: 400px;
        margin: 0 auto;
        padding: 20px;
      }
      label {
        display: block;
        margin-bottom: 10px;
        }
      input[type='number'] {
        width: 100%;
        padding: 10px;
        margin-bottom: 20px;
        border: 1px solid #ccc;
        border-radius: 4px;
        box-sizing: border-box;
      }
      button {
        background-color: #4CAF50;
        color: white;
        padding: 10px 20px;
        border: none;
        border-radius: 4px;
        cursor: pointer;
        width: 100%;
      }
      button:hover {
        background-color: #45a049;
      }
    </style>
  </head>
)=====";


const char* g_web_contents_body = R"=====(
  <body>
  <h1>Thoth Network Scale</h1>
  <p>HX711 readings:<br/> 
    <span style="color:green;"> 
      raw:<span id="raw">Loading...</span> <br/>
      tare:<span id="tare">Loading</span> <br/>
      adjusted:<span id="adjusted">Loading...</span><br/>
      weight(g):<span id="weight_g">Loading...</span><br/>
      stddev(g):<span id="stddev_g">Loading...</span><br/>
    </span>
  </p>

  <h1>Settings</h1>
  <p>IPv4: <span id="ipv4">Loading...</span></p>
  <p>IPv6: <span id="ipv6">Loading...</span></p>
  <p>Hostname: <span id="hostname">Loading...</span></p>

  <script>
    function fetchScale() {
      fetch("/api/v1/scale")
        .then(response => response.json())
        .then(data => {
          document.getElementById("raw").textContent = data.raw;
          document.getElementById("tare").textContent = data.tare;
          document.getElementById("adjusted").textContent = data.adjusted;
          document.getElementById("weight_g").textContent = data.weight_g;
          document.getElementById("stddev_g").textContent = data.stddev_g;
          
        })
        .catch(console.error);
    }

    function fetchSettings() {
      fetch("/api/v1/settings")
        .then(response => response.json())
        .then(data => {
          document.getElementById("ipv4").textContent = data.ipv4;
          document.getElementById("ipv6").textContent = data.ipv6;
          document.getElementById("hostname").textContent = data.hostname;
        })
        .catch(console.error);      
    }

    function sendTare() {
      const requestOptions =  { 
        method: "PUT", 
        headers: { "Content-type": "application/json" },
        body: JSON.stringify({ tare: true }),
      };

      fetch("/api/v1/scale", requestOptions)
        .then(response => response.json())
        .then(data => console.log(data))
        .catch(console.error);
    }

    function sendCalweight() {
    const requestOptions =  { 
      method: "PUT", 
      headers: { "Content-type": "application/json" },
      body: JSON.stringify({ calweight: document.getElementById('calweight-input').value}),
    };

    fetch("/api/v1/scale", requestOptions)
      .then(response => response.json())
      .then(data => console.log(data))
      .catch(console.error);
    }

    fetchScale();
    fetchSettings();
    setInterval(fetchScale, 1000);
    //setInterval(fetchSettings, 2000);
  </script>

  <h1>Commands</h1>
  <p>
    <button onclick="sendTare()">Tare</button>
  </p>
  <p>
    <input type="text" name="calweight-input" id="calweight-input">
    <button onclick="sendCalweight()">Send Calibration Weight</button>
  </body>
)=====";

#endif