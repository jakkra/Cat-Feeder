#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <Servo.h> 
#include <EEPROM.h>
#include "secrets.h"

#define DEBUG

#ifdef DEBUG
#define LOG(msg)  Serial.print(msg)
#define LOG_LN(msg) Serial.println(msg)
#define LOGF Serial.printf
#else
#define LOG(msg)
#define LOG_LN(msg)
#define LOGF(...)
#endif

static void handleRoot();
static void handleNotFound();
static void handleServoMove();
static void handleEditContainerPosition();
static void handleChangeContainer();
static void handleToggleContainer();
static void changeContainer(uint8_t newCon);
static void changeContainerSmooth(uint8_t prevCon, uint8_t newCon);

struct ContainerLocations {
  uint8_t containerOneLocation;
  uint8_t containerTwoLocation;
};

enum Container {
  CONTAINER_ONE = 1,
  CONTAINER_TWO,
  MAX_CONTAINER
};

#define EEPROM_CONTAINER_LOCATIONS  0
#define EEPROM_SELECTED_CONTAINER   EEPROM_CONTAINER_LOCATIONS + sizeof(ContainerLocations)

#define SMOOTH_STEPS    5
#define SMOOTH_DELAY    100

static ESP8266WebServer server(80);
static Servo servo;
static uint8_t selectedContainer = 1;
static ContainerLocations container;

void setup(void){
#ifdef DEBUG
  Serial.begin(115200);
#endif
  EEPROM.begin(512);
  servo.attach(D1);
  
  memset(&container, 0, sizeof(container));
  EEPROM.get(EEPROM_CONTAINER_LOCATIONS, container);
  EEPROM.get(EEPROM_SELECTED_CONTAINER, selectedContainer);
  LOGF("\nContainer locations: %d, %d, selected: %d\n", container.containerOneLocation, container.containerTwoLocation, selectedContainer);
  changeContainer(selectedContainer);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  LOG_LN("");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    LOG(".");
  }
  LOG_LN("");
  LOG("Connected to ");
  LOG_LN(ssid);
  LOG("IP address: ");
  LOG_LN(WiFi.localIP());

  if (MDNS.begin("cat-feeder")) {
    LOG_LN("MDNS responder started");
  }

  server.on("/", handleRoot);

  server.on("/servo", handleServoMove);

  server.on("/servo/pos", handleEditContainerPosition);

  server.on("/container", handleChangeContainer);

  server.on("/feed", handleToggleContainer);

  server.onNotFound(handleNotFound);

  server.begin();
}

void loop(void){
  server.handleClient();
}

/*
* /servo?position=newPos
* Move servo to newPos
*/
static void handleServoMove() {
  int position = server.arg("position").toInt();
  if (position >= 0 && position <= 180) {
    servo.write(position);
    server.send(200, "text/plain", "{success: true}");
  } else {
    server.send(200, "text/plain", "{success: false, message: Invalid params}");
  }
}

/*
* /container/pos?c1=pos1&c2=pos2
* Set servo location corresponding to the container
* c1 and c2 for container one and two.
*/
static void handleEditContainerPosition() {
  int posC1 = server.arg("c1").toInt();
  int posC2 = server.arg("c2").toInt();
  if (posC1 <= 180 && posC1 >= 0 && posC2 <= 180 && posC1 >= 0) {
    container.containerOneLocation = posC1;
    container.containerTwoLocation = posC2;
    LOGF("Container updated: %d, %d\n", container.containerOneLocation, container.containerTwoLocation);
    EEPROM.put(EEPROM_CONTAINER_LOCATIONS, container);
    server.send(200, "text/plain", "{success: true}");
    EEPROM.commit();
  } else {
     server.send(200, "text/plain", "{success: false, message: Invalid params}");
  }
}

/*
* /container?container=x
* Change to a specific coontainer
*/
static void handleChangeContainer() {
  int containerNum = server.arg("container").toInt();
  if (containerNum < MAX_CONTAINER && containerNum >= CONTAINER_ONE) {
    LOGF("Container selected: %d\n", containerNum);
    EEPROM.put(EEPROM_SELECTED_CONTAINER, containerNum);
    EEPROM.commit();
    server.send(200, "text/plain", "{success: true}");
    if (containerNum != selectedContainer) {
      changeContainerSmooth(selectedContainer, containerNum);
      selectedContainer = containerNum; 
    }
  } else {
     server.send(200, "text/plain", "{success: false, message: Invalid params}");
  }
}

/*
* /feed
* Feed the cat, i.e. change to the other container
*/
static void handleToggleContainer() {
  uint8_t newContainer = selectedContainer == CONTAINER_ONE ? CONTAINER_TWO : CONTAINER_ONE;
  EEPROM.put(EEPROM_SELECTED_CONTAINER, newContainer);
  EEPROM.commit();
  server.send(200, "text/plain", "{success: true}");
  changeContainerSmooth(selectedContainer, newContainer);
  selectedContainer = newContainer; 
}

static void changeContainer(uint8_t newCon) {
  uint8_t newPos = newCon == CONTAINER_TWO ? container.containerTwoLocation : container.containerOneLocation;
  LOGF("Changing to container %d, at position: %d\n", newCon, newPos);
  servo.write(newPos);
}

static void changeContainerSmooth(uint8_t prevCon, uint8_t newCon) {
  if (prevCon == newCon) return;
  uint8_t newPos = newCon == CONTAINER_TWO ? container.containerTwoLocation : container.containerOneLocation;
  uint8_t prevPos = newCon == CONTAINER_TWO ? container.containerOneLocation : container.containerTwoLocation;
  uint8_t steps;
  uint8_t currPos = prevPos;
  LOGF("Changing to container %d, at position: %d\n", newCon, newPos);
  LOGF("newPos: %d, prevPos: %d\n", newPos, prevPos);
  
  if (newPos > prevPos) {
    steps = (newPos - prevPos) / SMOOTH_STEPS;
    steps--;
    LOGF("Steps: %d\n", steps);
    for (uint8_t i = 0; i < steps; i++) {
      currPos += SMOOTH_STEPS;
      servo.write(currPos);
      delay(SMOOTH_DELAY);
    }
    servo.write(newPos);
  } else {
    steps = (prevPos - newPos) / SMOOTH_STEPS;
    steps--;
    LOGF("Steps: %d\n", steps);
    for (uint8_t i = 0; i < steps; i++) {
      currPos -= SMOOTH_STEPS;
      servo.write(currPos);
      delay(SMOOTH_DELAY);
    }
    servo.write(newPos);
  }
}

static void handleRoot() {
  server.send(200, "text/plain", "Cat Feeder");
}

static void handleNotFound(){
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i=0; i<server.args(); i++){
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}
