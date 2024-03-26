#include <WiFi.h>
#include <Servo.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include "SPIFFS.h"
#include <ArduinoJson.h>
#include "time.h"

Servo servo;
int startingPos = 90;
int maxPos = 180;
int minPos = 0;

//how many cycles to feed the cat for next
int nextFeedCycles = 0;

// Save how long the esp has to sleep for next 
int64_t nextTimeInMicroSec = 0;

// WIFI credentials

// Credentials for accsess point 
const char* ssidHost = "ESP32test";
const char* passwordHost = "123456789";

// Credentials for router that esp connects to
const char* ssid = "M.A.A. Wifi";
const char* password = "271750540";

AsyncWebServer server(80);

// time stuff
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 3600; //offset for timezone
const int   daylightOffset_sec = 3600;

// Value we get from function getLocalTime()
int localHourMinute;

//functions

void detachServo(){
  servo.detach();
}

void attachServo(){
  servo.attach(3);
  servo.write(startingPos);
}

void pulse(int cycles){
  Serial.print("Feeding cat ");
  Serial.print(cycles);
  Serial.println(" times");
  for(int i = 0; i < cycles; i++){
    servo.write(minPos);
    delay(400);
    servo.write(maxPos);
    delay(400);
    Serial.print("Cycles left");
    Serial.println(cycles - i);
  }
  servo.write(startingPos);
}

float isBatteryEmpty(){
  return analogRead(A1) * 3.3f / 4096.f * 5;
}

void connectToRouter(){
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("WiFi connected.");
  server.begin();
}

void getLocalTime(){
  connectToRouter();

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  struct tm timeinfo;

  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return;
  }

  char timeHour[3];
  strftime(timeHour,3, "%H", &timeinfo);

  char timeMinute[3];
  strftime(timeMinute,3, "%M", &timeinfo);

  String hourMinute = String(timeHour) + String(timeMinute);
  localHourMinute = hourMinute.toInt();

  // Disconnect from wifi as it is not needed anymore
  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);
}

void calculateSleepyTime(){
  getLocalTime();

  File file = SPIFFS.open("/feedTimes.json", "r");

  StaticJsonDocument<1024> doc;
  deserializeJson(doc, file);
  file.close();

  // Get times array
  JsonArray times = doc["times"];

  int minTime = 9999999;
  String nextTime;

  // Get next feeding time
  for (size_t i = 0; i < times.size(); i++) {
    String timesTime = times[i]["time"];
    int timesHourMinute = timesTime.substring(0, 2).toInt() + timesTime.substring(3, 5).toInt();
    int timesCycles = times[i]["cycles"];

    int timeDiff = timesHourMinute - localHourMinute;

    // if timeDiff is a negative
    if(timeDiff < 0){
      timeDiff = timesHourMinute + 2400 - localHourMinute;
    }

    // if time in times is closer then replace nextTime and nextFeedCycles.
    if(timeDiff < minTime){
      minTime = timeDiff;
      nextTime = timesTime;
      nextFeedCycles = timesCycles;
    }
  }

  // If nextTime isSet (Should only be NULL if times is empty)
  if (nextTime != NULL)
  {
      // Calculate next feeding time in microseconds.
      if ((nextTime.substring(0,2) + nextTime.substring(3,5)).toInt() - localHourMinute < 0) {
        // see näeb rets välja XD
        nextTimeInMicroSec = (nextTime.substring(0,2).toInt() * 3600000000 + nextTime.substring(3,5).toInt() * 60000000) + (24 * 3600000000) - (String(localHourMinute).substring(0, 2).toInt() * 3600000000 + String(localHourMinute).substring(2, 4).toInt() * 60000000);
      } 
      else {
        nextTimeInMicroSec = nextTime.substring(0,2).toInt() * 3600000000 + nextTime.substring(3,5).toInt() * 60000000;
      }
  }

  Serial.print("Next time in microsecs: ");
  Serial.println(nextTimeInMicroSec);

  Serial.print("Next feed cycles: ");
  Serial.println(nextFeedCycles);
}

void handleDeleteRow(AsyncWebServerRequest *request) {
  // Read the JSON file
  File file = SPIFFS.open("/feedTimes.json", "r");

  StaticJsonDocument<1024> doc;
  deserializeJson(doc, file);
  file.close();


  // Get the id to delete from the request
  String idToDelete = request->arg("id");

  // Get times array
  JsonArray times = doc["times"];

  // Find the row using id and then delete it
  for (size_t i = 0; i < times.size(); i++) {
    if (times[i]["id"] == idToDelete.toInt()) {
      times.remove(i);
      break;
    }
  }

  // Write to the JSON file
  file = SPIFFS.open("/feedTimes.json", "w");

  serializeJson(doc, file);

  file.close();

  request->send(SPIFFS, "/index.html");
}

void handleInsertData(AsyncWebServerRequest *request) {
  // Read form data
  if (request->hasParam("timeInput", true) && request->hasParam("cycles", true)) {
    AsyncWebParameter* timeParam = request->getParam("timeInput", true);
    AsyncWebParameter* cyclesParam = request->getParam("cycles", true);
    String timeValue = timeParam->value();
    int cyclesValue = cyclesParam->value().toInt();

    // Read the JSON file
    File file = SPIFFS.open("/feedTimes.json", "r+");

    DynamicJsonDocument doc(1024);
    deserializeJson(doc, file);
    file.close();


    // Get times array
    JsonArray times = doc["times"];

    int maxId = 0;
    for (JsonVariant item : times) {
      int id = item["id"];
      if (id > maxId) {
        maxId = id;
      }
    }

    if (times.size() > 0) {
      maxId++;
    }

    JsonObject newTime = times.createNestedObject();
    newTime["id"] = maxId;
    newTime["time"] = timeValue;
    newTime["cycles"] = cyclesValue;

    // Write the combined JSON back to the file
    file = SPIFFS.open("/feedTimes.json", "w");

    serializeJson(doc, file);

    file.close();

    // Respond to the client
    request->send(SPIFFS, "/index.html");
  } else {
    request->send(SPIFFS, "/index.html");
  }
}
// ________________________________________________________________________________________________________________________________________________________________
void setup() {
  Serial.begin(115200);
  delay(500);

    Serial.println("BATTERY " + String(isBatteryEmpty()));

  if (isBatteryEmpty() < 7.1)
  {
    pinMode(21, INPUT);
    while (true)
    {
      Serial.println("BATTERY LOW" + String(isBatteryEmpty()));
      digitalWrite(21, HIGH); 
      delay(2000);
      digitalWrite(21, LOW);
      delay(2000);
    }
  }

  attachServo();

  if(!SPIFFS.begin(true)){
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }

  while (true)
  {
    calculateSleepyTime();

    if(nextTimeInMicroSec < 0){
      Serial.println("NO TIMES FOUND");
      break;
    }

    // If the next feeding time is less than 10 minutes away feed cat
    if (nextTimeInMicroSec / 60000000 < 10)
    {
      Serial.print(nextTimeInMicroSec / 60000000);
      Serial.println(" minutes until next feeding");
      delay(static_cast<uint32_t>(nextTimeInMicroSec / 1000));
      pulse(nextFeedCycles);
    }else{
      break;
    }
  }

  // WIFI STUFF
  Serial.print("Setting AP (Access Point");
  WiFi.softAP(ssidHost, passwordHost);

  IPAddress IP = WiFi.softAPIP();
  Serial.println(IP);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/index.html");
  });

  server.on("/testFeatures", HTTP_GET, [](AsyncWebServerRequest *request){
  request->send(SPIFFS, "/testFeatures.html");
  });
  
  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/style.css", "text/css");
  });

  server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/script.js");
  });

  server.on("/feedTimes.json", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/feedTimes.json");
  });

  server.on("/up", HTTP_GET, [](AsyncWebServerRequest *request){
    servo.write(maxPos);
    Serial.println("SERVO MOVED UP");
    request->send(SPIFFS, "/index.html");
  });

  server.on("/down", HTTP_GET, [](AsyncWebServerRequest *request){
    servo.write(minPos);
    Serial.println("SERVO MOVED DOWN");    
    request->send(SPIFFS, "/index.html");
  });

  server.on("/changeStartPos", HTTP_GET, [](AsyncWebServerRequest *request){
  if (request->hasParam("startPos")) {
    startingPos = request->getParam("startPos")->value().toInt();
  }
  request->send(SPIFFS, "/testFeatures.html");
  });

  server.on("/changeMaxPos", HTTP_GET, [](AsyncWebServerRequest *request){
  if (request->hasParam("startPos")) {
    maxPos = request->getParam("maxPos")->value().toInt();
  }
  request->send(SPIFFS, "/testFeatures.html");
  });

  server.on("/changeMinPos", HTTP_GET, [](AsyncWebServerRequest *request){
  if (request->hasParam("startPos")) {
    minPos = request->getParam("minPos")->value().toInt();
  }
  request->send(SPIFFS, "/testFeatures.html");
  });

  server.on("/pulseMode", HTTP_GET, [](AsyncWebServerRequest *request){
  if (request->hasParam("cycles")) {
    pulse(request->getParam("cycles")->value().toInt());
  }
  request->send(SPIFFS, "/testFeatures.html");
  });

  server.on("/deleteRow", HTTP_POST, handleDeleteRow);
  server.on("/insertData", HTTP_POST, handleInsertData);

  server.begin();

  // stays awake for 10 minutes after everything is done
  for(int timre = 0; timre < 600; timre += 10){
    Serial.print("Going to sleep in ");
    Serial.println(600 - timre);
    delay(10000);
  }

  // End other processes
  server.end();
  servo.detach();

  // Connect to router to get local time and calculate sleepy time
  calculateSleepyTime();

  // goes to sleep for the next feeding - 1 minute
  Serial.print("Going to sleep for ");
  Serial.print(nextTimeInMicroSec / 60000000);
  Serial.println(" minutes");

  // Kui nextTimeInMicroSec -1 minute on negatiivne siis läheb magama 0 sekundiks (see ei tohiks juhtuda suht kindel aga igaks juhuks)
  nextTimeInMicroSec -= 60000000;
  if (nextTimeInMicroSec  < 0)
  {
    nextTimeInMicroSec = 0;
  }
  ESP.deepSleep(nextTimeInMicroSec); 
}
void loop(){
  // poo
}

//kkirjuta button mingi nuppu sisendi, nuppu sisendi d8 siis voltage divider a1