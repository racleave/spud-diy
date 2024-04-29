/*******************************************

Copyright 2023 Ra Cleave, Boyd Benton

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
“Software”), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

**********************************************************/

/* To do:

 - [ ] More info in cfg structure (min, max, step)
 - [ ] Function based version of settings, so the value to be stored is 

*/

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>

#include "settings.h"

Preferences preferences;

AsyncWebServer server(80);

// Replace with your network credentials
const char* ssid     = "SPUD";
const char* password = "spud1234";

String indexHtml;

// HTML web page to handle input fields. Would be nice to add to this based on wifiCfgParams.
const char index_html[] = R"rawliteral(
<!DOCTYPE HTML><html><head>
  <title>EV-SPUD settings</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <script>
    function reload() {
     //alert("Saved. Click OK to reload.");
      setTimeout(function(){ document.location.reload(false); }, 1000);   
      document.getElementById("hidden-form").submit();
    }
  </script>
  <style type="text/css">
    body {
      font-family: 'Roboto, sans-serif';
    }
    h1 {color: #FF0000;}
    input:invalid {border: solid red 3px;}
    form {
      width: 100vh;
      max-width: 500px;
      display: flex;
      flex-direction: row;
      justify-content: space-between;
    }
    .button {
      background-color: #4CAF50;
      border: none;
      border-radius: 2px;
      color: white;
      padding: 4px 10px;
      text-align: center;
      text-decoration: none;
      display: inline-block;
      font-size: 16px;
    }
  </style>
  </head><body>
  <svg version="1.1" width="200px" height="200px" id="Layer_1" xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" x="0px" y="0px"
  	 viewBox="0 0 75 75" style="enable-background:new 0 0 75 75;" xml:space="preserve">
  <style type="text/css">
  	.st0{fill:none;stroke:#FF0000;stroke-width:2.3438;stroke-linecap:square;stroke-miterlimit:11.7188;}
	.st1{fill:none;stroke:#FF0000;stroke-width:2.3438;stroke-miterlimit:11.7188;}
	.st2{fill:#FF0000;stroke:#FF0000;stroke-width:0.4;stroke-miterlimit:10;}
  </style>
  <polyline class="st0" points="18.8,65.6 18.8,72.7 5.9,72.7 5.9,65.6 "/>
  <polyline class="st0" points="69.1,65.6 69.1,72.7 56.2,72.7 56.2,65.6 "/>
  <path class="st1" d="M21.1,25.8h-4c-1.1,0-2,0.7-2.3,1.8l-4.2,17"/>
  <path class="st1" d="M64.5,44.5l-4.2-17c-0.3-1-1.2-1.8-2.3-1.8h-4"/>
  <path class="st0" d="M69.1,65.6H5.9V50.2c0-0.6,0.2-1.2,0.7-1.7l4-4h53.9l4,4c0.4,0.4,0.7,1,0.7,1.7L69.1,65.6L69.1,65.6z"/>
  <circle class="st0" cx="16.4" cy="55.1" r="3.5"/>
  <circle class="st0" cx="58.6" cy="55.1" r="3.5"/>
  <line class="st0" x1="31.6" y1="55.1" x2="43.4" y2="55.1"/>
  <g>
  <g>
    <path class="st2" d="M35.7,42.1l3-15.8H24.1L46.6,0.8l-3,15.8h14.5L35.7,42.1z M28.4,24.3H41l-2.1,11.2l15-17H41.2l2.1-11.2
			L28.4,24.3z"/>
  </g>
  </g>
  </svg>
  <h1>REV settings</h1>)rawliteral";

void not_found(AsyncWebServerRequest *request) {
  request->send(404, "text/plain", "Not found");
}

// Replaces placeholder with stored values
String processor(const String& var) {
  for (int i = 0; i < nWiFiCfgParams; i++) {
    if (var == wifiCfgParams[i].name) {
      //return readFile(SPIFFS, "/maxChargeRate.txt");

      return wifiCfgParams[i].value;
    }
  }
  return String();
}

void settings_server(uint8_t eepromOffset=0) {
  
  IPAddress Ip(192, 168, 1, 1);
  IPAddress NMask(255, 255, 255, 0);
  WiFi.softAPConfig(Ip, Ip, NMask);
  // channel=1, invisible=0, max_connections=1:
  WiFi.softAP(ssid, password, 1, 0, 1);

  // Read EEPROM here
  // like: odoPrev = (EEPROM.read(0) << 16) + (EEPROM.read(1) << 8) + EEPROM.read(2);
  
  
  // Modify html string. Add HTML form for each config parameter. Need
  // to make this less scripty and more "embedded".
  indexHtml = index_html;
  String formStr;
  for (int i = 0; i < nWiFiCfgParams; i++) {
    formStr = "<form id=\"config-form\" action=\"/get\" target=\"hidden-form\">";
    formStr += wifiCfgParams[i].text + ":" ;
    formStr += " (%" + wifiCfgParams[i].name + "%)" + wifiCfgParams[i].units;
    
    formStr += "<input";
    if (wifiCfgParams[i].varType == TEXT) {
      formStr += " style=\"width: 8em;\" type=\"text\"";
      formStr += " maxlength=\"8\"";
    }
    else if (wifiCfgParams[i].varType == NUMBER) {
      formStr += " style=\"width: 5em;\" type=\"number\"";
    }
    else if (wifiCfgParams[i].varType == FUNCTION) {
      formStr += " style=\"width: 0em;\" type=\"hidden\"";
    }
    formStr += " name=\"" + wifiCfgParams[i].name + "\"";
    
    formStr += ">";
    formStr += "<input class=\"button\" type=\"submit\" value=\"Save\" onclick=\"reload()\">";
    formStr += "</form><br>\n";
    Serial.println(formStr);
    indexHtml += formStr;
  }
  indexHtml += "</body></html>";
  
  // Send web page with input fields to client
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", indexHtml.c_str(), processor);
  });

  // Send a GET request to <Ip>/get?inputString=<inputMessage>
  server.on("/get", HTTP_GET, [] (AsyncWebServerRequest *request) {
    String inputMessage;
    int offset = 0;
    for (int i = 0; i < nWiFiCfgParams; i++) {
      offset += wifiCfgParams[i].nBytes;
      if (request->hasParam(wifiCfgParams[i].name)) {
        wifiCfgParams[i].value = wifiCfgParams[i].func(request->getParam(wifiCfgParams[i].name)->value());
        Serial.println(wifiCfgParams[i].value);
        
        if (wifiCfgParams[i].varType == NUMBER) {
          int intValue = wifiCfgParams[i].value.toInt();
          preferences.begin(settingsName);
          preferences.putInt(wifiCfgParams[i].name.c_str(), intValue);
          preferences.end();
        } else {
          preferences.begin(settingsName);
          preferences.putString(wifiCfgParams[i].name.c_str(), wifiCfgParams[i].value);
          preferences.end();
        }
      }
    }
    Serial.println(inputMessage);
    request->send(200, "text/text", inputMessage);
  });
  
  server.onNotFound(not_found);
  server.begin();
}

int getIntVal(String name) {
  for (int i = 0; i < nWiFiCfgParams; i++) {
    if (name == wifiCfgParams[i].name)
      return wifiCfgParams[i].value.toInt();
  }
}

String getStrVal(String name) {
  for (int i = 0; i < nWiFiCfgParams; i++) {
    if (name == wifiCfgParams[i].name)
      return wifiCfgParams[i].value;
  }
}

void ReadEepromValues() {
  preferences.begin(settingsName);
  
  for (int i = 0; i < nWiFiCfgParams; i++) {
    if (wifiCfgParams[i].varType == NUMBER) {
      int intValue = preferences.getInt(wifiCfgParams[i].name.c_str(), wifiCfgParams[i].value.toInt());
      Serial.print(wifiCfgParams[i].name);
      Serial.print(": ");
      Serial.println(intValue);
      wifiCfgParams[i].value = String(intValue); // Overwrite (default gets used if no EEPROM found.
    } else {
      String strValue = preferences.getString(wifiCfgParams[i].name.c_str(), wifiCfgParams[i].value);
      Serial.print(wifiCfgParams[i].name);
      Serial.print(": ");
      Serial.println(strValue);
      wifiCfgParams[i].value = strValue; // Overwrite (default gets used if no EEPROM found.
    }
  }
  
  preferences.end();
}
