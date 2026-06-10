#define BLYNK_TEMPLATE_ID "TMPL5pyPNEr_b"
#define BLYNK_TEMPLATE_NAME "NetworkBlink"
#define BLYNK_AUTH_TOKEN "M48Pm2mKoycKpG493HZNSSvnO1gZDTPH"
#define BLYNK_PRINT Serial

#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>

// --- PHYSICAL HARDWARE PIN REGISTRY ---
const int motorLeftFwd  = 0;   
const int motorLeftRev  = 1;   
const int motorRightFwd = 2;   
const int motorRightRev = 3;   

const int pinS1 = 18;          
const int pinS2 = 19;          
const int pinS3 = 20;          
const int pinS4 = 21;          
const int pinS5 = 22;          
const int pinNear = 12;        // CHANGED: Moved from problematic Pin 7 to safe Pin 12!

#define BATT_ADC_PIN 4         
#define DIVIDER_RATIO 2.0f     

#define LED_PIN     15         
#define NUM_LEDS    13         
#define BRIGHTNESS  25         
#define BLYNK_LED_PIN 2        

// --- INSTANCE DECLARATIONS ---
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);
WiFiClient espClient;
PubSubClient client(espClient);

char ssid[] = "mnet-home";
char pass[] = "mnet*2026";
const char* mqtt_server = "192.168.0.17";     
const int mqtt_port = 1883;
const char* mqtt_topic = "robot/telemetry";

// --- GLOBAL VARIABLES ---
bool robotEnabled = false;     
int globalSocPercent = 100;    
float batteryVoltage = 0.0f;

unsigned long lastMsg = 0;
unsigned long lastLedUpdate = 0;
unsigned long lastSerialPrint = 0; 
unsigned long lastBlynkCheck = 0;
const int animationInterval = 100;  
int animationFrame = 0;

enum RobotState { MOVING_FORWARD, TURNING_LEFT, TURNING_RIGHT, PIVOT_LEFT, PIVOT_RIGHT, OBSTACLE_STOP, LINE_LOST_STOP, BLYNK_DISABLED_STOP };
RobotState currentRobotState = BLYNK_DISABLED_STOP;

// --- MOTOR CONTROL FUNCTIONS ---
void moveForward() {
  digitalWrite(motorLeftFwd, HIGH);  digitalWrite(motorLeftRev, LOW);
  digitalWrite(motorRightFwd, HIGH); digitalWrite(motorRightRev, LOW);
}
void turnLeftGently() {
  digitalWrite(motorLeftFwd, LOW);   digitalWrite(motorLeftRev, LOW);
  digitalWrite(motorRightFwd, HIGH); digitalWrite(motorRightRev, LOW);
}
void pivotLeftSharp() {
  digitalWrite(motorLeftFwd, LOW);   digitalWrite(motorLeftRev, HIGH);
  digitalWrite(motorRightFwd, HIGH); digitalWrite(motorRightRev, LOW);
}
void turnRightGently() {
  digitalWrite(motorLeftFwd, HIGH);  digitalWrite(motorLeftRev, LOW);
  digitalWrite(motorRightFwd, LOW);  digitalWrite(motorRightRev, LOW);
}
void pivotRightSharp() {
  digitalWrite(motorLeftFwd, HIGH);  digitalWrite(motorLeftRev, LOW);
  digitalWrite(motorRightFwd, LOW);  digitalWrite(motorRightRev, HIGH);
}
void stopRobot() {
  digitalWrite(motorLeftFwd, LOW);   digitalWrite(motorLeftRev, LOW);
  digitalWrite(motorRightFwd, LOW);  digitalWrite(motorRightRev, LOW);
}

// --- BLYNK ---
BLYNK_CONNECTED() {
  Blynk.syncVirtual(V0); 
}

BLYNK_WRITE(V0) {
  int value = param.asInt(); 
  digitalWrite(BLYNK_LED_PIN, value); 
  
  if (value == 1) {
    robotEnabled = true;
  } else {
    robotEnabled = false;
    stopRobot();
    currentRobotState = BLYNK_DISABLED_STOP;
  }
}

void verifyMqttConnection() {
  if (!client.connected()) {
    client.connect("ESP32Client-Robot");
  }
}

void setup() {
  Serial.begin(115200);
  
  pinMode(motorLeftFwd, OUTPUT);  pinMode(motorLeftRev, OUTPUT);
  pinMode(motorRightFwd, OUTPUT); pinMode(motorRightRev, OUTPUT);
  pinMode(BLYNK_LED_PIN, OUTPUT);

  pinMode(pinS1, INPUT); pinMode(pinS2, INPUT); pinMode(pinS3, INPUT);
  pinMode(pinS4, INPUT); pinMode(pinS5, INPUT); 
  
  // CHANGED: Added INPUT_PULLUP to stabilize the sensor readings
  pinMode(pinNear, INPUT_PULLUP); 

  analogSetAttenuation(ADC_11db);

  strip.begin();
  strip.setBrightness(BRIGHTNESS);
  strip.clear();
  strip.show(); 

  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);
  client.setServer(mqtt_server, mqtt_port);
}

void loop() {
  Blynk.run();
  client.loop();

  unsigned long now = millis();

  if (now - lastBlynkCheck >= 5000) {
    lastBlynkCheck = now;
    if (Blynk.connected()) {
      verifyMqttConnection();
    }
  }

  // --- BATTERY MONITORING ---
  uint32_t pinMilliVolts = analogReadMilliVolts(BATT_ADC_PIN);
  batteryVoltage = (pinMilliVolts / 1000.0f) * DIVIDER_RATIO;
  globalSocPercent = constrain(((batteryVoltage - 3.00f) / (4.20f - 3.00f)) * 100.0f, 0, 100);

  // --- SENSOR READS ---
  int s1 = digitalRead(pinS1);
  int s2 = digitalRead(pinS2);
  int s3 = digitalRead(pinS3);
  int s4 = digitalRead(pinS4);
  int s5 = digitalRead(pinS5);
  int nearState = digitalRead(pinNear);

  // --- NAVIGATION LOGIC ---
  if (!robotEnabled) {
    stopRobot();
    currentRobotState = BLYNK_DISABLED_STOP;
  } 
  // CHANGED: Switched nearState trigger to LOW. 
  // (Standard IR obstacle modules output 1 normally, and flip to 0 when they see something)
  else if (nearState == LOW) { 
    stopRobot();
    currentRobotState = OBSTACLE_STOP;
  } 
  else if ((s1 == LOW && s2 == LOW && s3 == HIGH && s4 == LOW && s5 == LOW) ||
           (s1 == LOW && s2 == HIGH && s3 == HIGH && s4 == HIGH && s5 == LOW)) {
    moveForward();
    currentRobotState = MOVING_FORWARD;
  } 
  else if (s2 == HIGH && s4 == LOW) {
    turnLeftGently();
    currentRobotState = TURNING_LEFT;
  } 
  else if (s1 == HIGH) {
    pivotLeftSharp();
    currentRobotState = PIVOT_LEFT;
  } 
  else if (s4 == HIGH && s2 == LOW) {
    turnRightGently();
    currentRobotState = TURNING_RIGHT;
  } 
  else if (s5 == HIGH) {
    pivotRightSharp();
    currentRobotState = PIVOT_RIGHT;
  } 
  else {
    stopRobot();
    currentRobotState = LINE_LOST_STOP;
  }

  updateStatusIndicators();
  printLiveTelemetry(s1, s2, s3, s4, s5, nearState);

  // --- MQTT TELEMETRY SYSTEM ---
  if (now - lastMsg > 5000) {
    lastMsg = now;

    JsonDocument doc;
    doc["device_id"] = "device_01";
    doc["status"] = (robotEnabled) ? "discharging" : "charging";
    doc["remaining_mah"] = map(globalSocPercent, 0, 100, 0, 2400);
    doc["soc_percent"] = globalSocPercent; 
    doc["voltage_v"] = (batteryVoltage / 5.6) * 100;
    doc["current_a"] = random(50, 220) / 100.0;
    doc["soh_percent"] = random(970, 1000) / 10.0;
    doc["temperature_c"] = random(240, 350) / 100.0;
    doc["robot_state"] = (int)currentRobotState; 
    doc["blynk_enabled"] = robotEnabled;

    char buffer[256];
    serializeJson(doc, buffer);

    if (client.connected()) {
      client.publish(mqtt_topic, buffer);
    }
  }
}

// --- TELEMETRY LOGGING ---
void printLiveTelemetry(int s1, int s2, int s3, int s4, int s5, int near) {
  if (millis() - lastSerialPrint >= 1000) { 
    lastSerialPrint = millis();
    Serial.printf("SENSORS: [S1:%d S2:%d S3:%d S4:%d S5:%d] | NEAR:%d | BATT: %.2fV (%d%%)\n", 
                  s1, s2, s3, s4, s5, near, batteryVoltage, globalSocPercent);
  }
}

// --- ORIGINAL ANIMATIONS & VISUAL INDICATORS ---
void updateStatusIndicators() {
  if (millis() - lastLedUpdate < animationInterval) return;
  lastLedUpdate = millis();
  strip.clear(); 

  uint32_t green   = strip.Color(0, 180, 0);    
  uint32_t yellow  = strip.Color(160, 140, 0);  
  uint32_t red     = strip.Color(180, 0, 0);    
  uint32_t cyan    = strip.Color(0, 150, 150);
  uint32_t orange  = strip.Color(180, 60, 0);
  uint32_t white   = strip.Color(100, 100, 100);

  switch (currentRobotState) {
    case MOVING_FORWARD:
      for(int i = 0; i < NUM_LEDS; i++) strip.setPixelColor(i, cyan);
      break;
    case TURNING_LEFT:
    case PIVOT_LEFT:
      animationFrame--; if (animationFrame < 0 || animationFrame > 5) animationFrame = 5;
      for(int i = 5; i >= animationFrame; i--) strip.setPixelColor(i, orange);
      break;
    case TURNING_RIGHT:
    case PIVOT_RIGHT:
      animationFrame++; if (animationFrame < 7 || animationFrame > 12) animationFrame = 7;
      for(int i = 7; i <= animationFrame; i++) strip.setPixelColor(i, orange);
      break;
    case OBSTACLE_STOP:
      animationFrame = !animationFrame;
      for(int i = 0; i < NUM_LEDS; i++) strip.setPixelColor(i, animationFrame ? red : white);
      break;
    case LINE_LOST_STOP:
      animationFrame = !animationFrame;
      for(int i = 0; i < NUM_LEDS; i++) if (animationFrame) strip.setPixelColor(i, red);
      break;
    case BLYNK_DISABLED_STOP:
      if (globalSocPercent > 50) {
        for(int i = 0; i < NUM_LEDS; i++) strip.setPixelColor(i, green);
      } 
      else if (globalSocPercent > 15) {
        for(int i = 0; i < NUM_LEDS; i++) strip.setPixelColor(i, yellow);
      } 
      else {
        animationFrame = !animationFrame;
        if (animationFrame) {
          for(int i = 0; i < NUM_LEDS; i++) strip.setPixelColor(i, red);
        }
      }
      break;
  }
  strip.show(); 
}