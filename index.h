#ifndef INDEX_H
#define INDEX_H

const char* g_web_contents_head = R"=====(
<html>
  <head>
    <link rel="shortcut icon" href="#">  
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

#endif