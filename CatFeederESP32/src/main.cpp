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
float nextTimeInMicroSec = 0;

// WIFI credentials

// Credentials for accsess point 
const char* ssidHost = "Cat Feeder";
const char* passwordHost = "123456789";

// Credentials for router that esp connects to
const char* ssid = "PaulKerese45";
const char* password = "6M6TWMAEGE";

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
  return analogReadMilliVolts(A1) / 200;
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

int currentHour;
int currentMinute;

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

  currentHour = String(timeHour).substring(0,2).toInt();
  currentMinute = String(timeMinute).substring(0,2).toInt();

  String hourMinute = String(timeHour).substring(0,2) + String(timeMinute).substring(0,2);
  localHourMinute = hourMinute.toInt();

  // Disconnect from wifi as it is not needed anymore
  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);
}

boolean calculateSleepyTime(int hour, int minute){
  File file = SPIFFS.open("/feedTimes.json", "r");

  StaticJsonDocument<1024> doc;
  deserializeJson(doc, file);
  file.close();

  // Get times array
  JsonArray times = doc["times"];

  String nextTime;

  float nextFeedTimeInSeconds = 5184000, nextFeedindex = 0;

  // Find next feeding time
  for (size_t i = 0; i < times.size(); i++) 
  {
    Serial.println("Current Hour & Minute: " + String(hour) + ":" + String(minute));

    String timesTime = times[i]["time"];
    Serial.println("Target Clock: " + String(timesTime));

    int feedHour = timesTime.substring(0,2).toInt();
    int feedMinute = timesTime.substring(3,5).toInt();

    int targetHour = (feedHour - hour); 
    int targetMinute = (feedMinute - minute); 

    Serial.println("Feed Hour & Minute Difference: " + String(targetHour) + " " + String(targetMinute));


    if ((targetHour >= 0 && targetMinute >= 0)) //Kas feed on täna?
    {
      int nextFeedInSeconds = int(targetHour * 3600) + int(targetMinute * 60); //Mitme sekundi pärast on feed? 

      Serial.println("Feeding cat today, in: " + String(nextFeedInSeconds));

      if (nextFeedTimeInSeconds > nextFeedInSeconds)
      {
        Serial.println("Updating closest feed time to: " + String(nextFeedInSeconds));
        nextFeedTimeInSeconds = nextFeedInSeconds;
        nextFeedindex = i;
      }
    }
    else if ((targetHour > 0 && targetMinute < 0))
    {
      int nextFeedInSeconds = int((targetHour - 1) * 3600) + int((60 + targetMinute) * 60); //Mitme sekundi pärast on feed? 

      Serial.println("Feeding cat today, in: " + String(nextFeedInSeconds));

      if (nextFeedTimeInSeconds > nextFeedInSeconds)
      {
        Serial.println("Updating closest feed time to: " + String(nextFeedInSeconds));
        nextFeedTimeInSeconds = nextFeedInSeconds;
        nextFeedindex = i;
      }
    }
    Serial.println(" ");
  }
  
  if (nextFeedTimeInSeconds == 5184000) 
  {
    Serial.println("No feeding happening today!");
    return false;
  }
  nextTimeInMicroSec = nextFeedTimeInSeconds;
  nextFeedCycles = times[nextFeedindex]["cycles"];

  Serial.print("Next feed cycles & time in s :");
  Serial.println(nextFeedCycles);
  Serial.println(nextTimeInMicroSec);

  return true;
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

  attachServo();

  if(!SPIFFS.begin(true)){
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }

  while (true)
  {
    getLocalTime();
    if (!calculateSleepyTime(currentHour, currentMinute))
    {
      Serial.println("No more feed times left today.");
      break;
    }


    // If the next feeding time is less than 5 minutes away feed cat
    if (nextTimeInMicroSec / 60 < 5)
    {
      Serial.print(nextTimeInMicroSec / 60);
      Serial.println(" minutes until next feeding");
      delay((nextTimeInMicroSec * 1000));
      pulse(nextFeedCycles);
      delay(60000);
    } else {
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

  // stays awake for 2 minutes after everything is done
  for(int timre = 0; timre < 120; timre += 10){
    Serial.print("Going to sleep in ");
    Serial.println(120 - timre);
    delay(10000);
  }

  // End other processes
  server.end();
  servo.detach();

  // Connect to router to get local time and calculate sleepy time
  getLocalTime();
  if (!calculateSleepyTime(currentHour, currentMinute))
  {
    calculateSleepyTime(0,0);

    Serial.print("Feed time after midnight ");
    Serial.println(nextTimeInMicroSec);
    
    // Calculates time til next day and adds it to the counter
    nextTimeInMicroSec = nextTimeInMicroSec + ((23 - currentHour) * 3600) + ((59 - currentMinute) * 60);

    Serial.print("Next time in seconds ");
    Serial.println(nextTimeInMicroSec);
  }

  // goes to sleep for the next feeding - 1 minute
  Serial.print("Going to sleep for ");
  Serial.print(nextTimeInMicroSec / 60);
  Serial.println(" minutes");

  ESP.deepSleep(nextTimeInMicroSec * 1000000 - 60 * 1000000);
  ESP.restart();
}
void loop(){}
