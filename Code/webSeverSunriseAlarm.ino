#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <Hash.h>
#include <FS.h>
#include <ESPAsyncWebServer.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

#define CW_PIN D1
#define WW_PIN D2
#define BUTTON_PIN D5

AsyncWebServer server(80);

uint8_t lightStatus = 0;
unsigned long currentTime = 0;
unsigned long lastTime = 0;

// REPLACE WITH YOUR NETWORK CREDENTIALS
const char* ssid = "your_ssid";
const char* password = "your_password";

const char* PARAM_HOUR = "setHour";
const char* PARAM_MINUTE = "setMinute";
const char* PARAM_WEEKEND = "setMode";

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

// HTML web page to handle 3 input fields (setHour, setMinute, setMode)
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html><head>
  <title>ESP Input Form</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <script>
    function submitMessage() {
      alert("Saved value to ESP SPIFFS");
      setTimeout(function(){ document.location.reload(false); }, 500);   
    }
  </script></head><body>
  <form action="/get" target="hidden-form">
    Set Hour 0-23 (current value %setHour%): <input type="text" name="setHour">
    <input type="submit" value="Submit" onclick="submitMessage()">
  </form><br>
  <form action="/get" target="hidden-form">
    Set Minute 0-59 (current value %setMinute%): <input type="number " name="setMinute">
    <input type="submit" value="Submit" onclick="submitMessage()">
  </form><br>
  <form action="/get" target="hidden-form">
    Mode 0 - Off, 1 - Weekdays Only, 2 - Everyday (current value %setMode%): <input type="number " name="setMode">
    <input type="submit" value="Submit" onclick="submitMessage()">
  </form>
  <iframe style="display:none" name="hidden-form"></iframe>
</body></html>)rawliteral";

void notFound(AsyncWebServerRequest* request) {
  request->send(404, "text/plain", "Not found");
}

String readFile(fs::FS& fs, const char* path) {
  File file = fs.open(path, "r");
  if (!file || file.isDirectory()) {
    Serial.println("- empty file or failed to open file");
    return String();
  }
  String fileContent;
  while (file.available()) {
    fileContent += String((char)file.read());
  }
  file.close();
  return fileContent;
}

void writeFile(fs::FS& fs, const char* path, const char* message) {
  Serial.printf("Writing file: %s\r\n", path);
  File file = fs.open(path, "w");
  if (!file) {
    Serial.println("- failed to open file for writing");
    return;
  }
  if (file.print(message)) {
    Serial.println("- file written");
  } else {
    Serial.println("- write failed");
  }
  file.close();
}

// Replaces placeholder with stored values
String processor(const String& var) {
  if (var == "setHour") {
    return readFile(SPIFFS, "/setHour.txt");
  } else if (var == "setMinute") {
    return readFile(SPIFFS, "/setMinute.txt");
  } else if (var == "setMode") {
    return readFile(SPIFFS, "/setMode.txt");
  }
  return String();
}

// function to fade on the warm white and then the cold white LEDs over the course of 10 minutes
// if the button is pressed during this time, the function will return 0
int simulateSunrise(void) {
  for (int i = 0; i < 1023; i++) {
    analogWrite(WW_PIN, i);
    delay(293);
    if (!digitalRead(BUTTON_PIN)) {
      return 0;
    }
  }

  for (int i = 0; i < 1023; i++) {
    analogWrite(CW_PIN, i);
    delay(293);
    if (!digitalRead(BUTTON_PIN)) {
      return 0;
    }
  }
  return 1;
}

void setup() {
  Serial.begin(115200);
  analogWriteRange(1023);
  pinMode(CW_PIN, OUTPUT);
  pinMode(WW_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  analogWrite(CW_PIN, 0);
  analogWrite(WW_PIN, 0);
  // Initialize SPIFFS
  if (!SPIFFS.begin()) {
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  if (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("WiFi Failed!");
    return;
  }
  Serial.println();
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  // Send web page with input fields to client
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send_P(200, "text/html", index_html, processor);
  });

  // Send a GET request to <ESP_IP>/get?setHour=<inputMessage>
  server.on("/get", HTTP_GET, [](AsyncWebServerRequest* request) {
    String inputMessage;
    // GET setHour value on <ESP_IP>/get?setHour=<inputMessage>
    if (request->hasParam(PARAM_HOUR)) {
      inputMessage = request->getParam(PARAM_HOUR)->value();
      writeFile(SPIFFS, "/setHour.txt", inputMessage.c_str());
    }
    // GET setMinute value on <ESP_IP>/get?setMinute=<inputMessage>
    else if (request->hasParam(PARAM_MINUTE)) {
      inputMessage = request->getParam(PARAM_MINUTE)->value();
      writeFile(SPIFFS, "/setMinute.txt", inputMessage.c_str());
    }
    // GET setMode value on <ESP_IP>/get?setMode=<inputMessage>
    else if (request->hasParam(PARAM_WEEKEND)) {
      inputMessage = request->getParam(PARAM_WEEKEND)->value();
      writeFile(SPIFFS, "/setMode.txt", inputMessage.c_str());
    } else {
      inputMessage = "No message sent";
    }
    Serial.println(inputMessage);
    request->send(200, "text/text", inputMessage);
  });
  server.onNotFound(notFound);
  server.begin();

  // Initialize a NTPClient to get time
  timeClient.begin();
  // SET TIMEZONE OFFSET HERE
  // Set offset time in seconds to adjust for your timezone, for example:
  // GMT +1 = 3600
  // GMT +8 = 28800
  // GMT -1 = -3600
  // GMT 0 = 0
  timeClient.setTimeOffset(0);
}

void loop() {
  currentTime = millis();

  // poll NTP server and check time againt set time every 25 seconds or so
  if (currentTime - lastTime > 25000) {
    int setHour = readFile(SPIFFS, "/setHour.txt").toInt();
    Serial.print("*** Set Hour: ");
    Serial.println(setHour);

    int setMinute = readFile(SPIFFS, "/setMinute.txt").toInt();
    Serial.print("*** Set Minute: ");
    Serial.println(setMinute);

    int triggerMinute = setMinute - 10;

    if (triggerMinute < 0) {
      triggerMinute += 60;
    }

    Serial.print("*** Trigger Minute: ");
    Serial.println(triggerMinute);

    int setMode = readFile(SPIFFS, "/setMode.txt").toInt();
    Serial.print("*** Set Mode: ");
    Serial.println(setMode);

    timeClient.update();

    time_t epochTime = timeClient.getEpochTime();
    Serial.print("Epoch Time: ");
    Serial.println(epochTime);

    String formattedTime = timeClient.getFormattedTime();
    Serial.print("Formatted Time: ");
    Serial.println(formattedTime);

    int currentHour = timeClient.getHours();
    int currentMinute = timeClient.getMinutes();
    int currentDay = timeClient.getDay();

    if ((currentHour == setHour && currentMinute == triggerMinute) && (setMode == 2 || (setMode == 1 && currentDay != 0 && currentDay != 6))) {
      digitalWrite(LED_BUILTIN, LOW);
      simulateSunrise();
      while (digitalRead(BUTTON_PIN)) {
        delay(100);
      }
      digitalWrite(LED_BUILTIN, HIGH);
      analogWrite(CW_PIN, 0);
      analogWrite(WW_PIN, 0);
      delay(1000);
    }
    lastTime = currentTime;
  }

  // fade warm white LED's on/off slowly the button is pressed.
  if (!digitalRead(BUTTON_PIN)) {
    if (lightStatus == 0) {
      for (int i = 0; i < 1023; i++) {
        analogWrite(WW_PIN, i);
        delay(1);
      }
      lightStatus = 1;
    }
    else {
      for (int i = 1023; i >= 0; i--) {
        analogWrite(WW_PIN, i);
        delay(1);
      }
      lightStatus = 0;
    }
  }
}