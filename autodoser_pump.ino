//*********************************************
//   AUTODOSER CODE (Peristaltic Pump @ Motor 1)
//*********************************************

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>
#include <EEPROM.h>

#define D0 16
#define D1 5
#define D2 4
#define LED_BUILTIN 2
#define D5 14
#define D6 12
#define D7 13
#define D8 15
#define SK 10

#define MOTOR_ENABLE 0
#define MOTOR_DISABLE 1

#define stepPin1 D0
#define dirPin1 D1
#define ENA_M1 SK

#define stepPin2 D6
#define dirPin2 D5
#define ENA_M2 D8

#define stepPin3 D2
#define dirPin3 D7

#define CLK_WISE HIGH

#define M1_NO_OF_ROTN_ADDR 0
#define M2_NO_OF_ROTN_ADDR 1
#define M3_NO_OF_ROTN_ADDR 2
#define M1_SPEED_ADDR 3

const char* ssid = "ESP_SERVER";
const char* password = "1234567890";
ESP8266WebServer server(80);
String json = "{\"time\":093025,\"temperature\":25,\"dox\":8,\"ph\":7.5,\"TDS\":500}";

int speed_level = 1;
uint16_t speed_delay_us = 500;
uint16_t no_of_rotations[3] = {1, 1, 1};
volatile uint8_t motor_flag[3] = {0, 0, 0};
volatile uint32_t step_counter[3] = {0, 0, 0};
bool step_state[3] = {LOW, LOW, LOW};
uint32_t step_tick_us[3] = {0, 0, 0};
const uint16_t STEPS_PER_REV = 1620;

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1.0"><title>AUTODOSE V2</title></head>
<body>
<h1>AUTODOSER DASHBOARD</h1>
<input type="text" id="do-threshold" value="1"><input type="text" id="ph-threshold" value="1"><input type="text" id="tds-threshold" value="1">
<button onclick="set_val()">SET</button>
<button onclick="runFunction_1()">DISPENSE 1</button><button onclick="stopFunction_1()">STOP 1</button>
<button onclick="runFunction_2()">DISPENSE 2</button><button onclick="stopFunction_2()">STOP 2</button>
<button onclick="runFunction_3()">DISPENSE 3</button><button onclick="stopFunction_3()">STOP 3</button>
<script>
function set_val(){var x=new XMLHttpRequest();x.open("GET","/set?do="+do_threshold.value+"&ph="+ph_threshold.value+"&tds="+tds_threshold.value,true);x.send();}
function runFunction_1(){var x=new XMLHttpRequest();x.open("GET","/update?motor_no=1&run=1",true);x.send();}
function stopFunction_1(){var x=new XMLHttpRequest();x.open("GET","/update?motor_no=1&run=0",true);x.send();}
function runFunction_2(){var x=new XMLHttpRequest();x.open("GET","/update?motor_no=2&run=1",true);x.send();}
function stopFunction_2(){var x=new XMLHttpRequest();x.open("GET","/update?motor_no=2&run=0",true);x.send();}
function runFunction_3(){var x=new XMLHttpRequest();x.open("GET","/update?motor_no=3&run=1",true);x.send();}
function stopFunction_3(){var x=new XMLHttpRequest();x.open("GET","/update?motor_no=3&run=0",true);x.send();}
</script></body></html>
)rawliteral";

uint16_t mapSpeedLevel(int lvl) {
  switch (lvl) { case 1: return 500; case 2: return 450; case 3: return 400; case 4: return 350; case 5: return 300; default: return 500; }
}

void stopMotor(uint8_t i) {
  motor_flag[i] = 0;
  step_state[i] = LOW;
  if (i == 0) { digitalWrite(stepPin1, LOW); digitalWrite(ENA_M1, MOTOR_DISABLE); }
  if (i == 1) { digitalWrite(stepPin2, LOW); digitalWrite(ENA_M2, MOTOR_DISABLE); }
  if (i == 2) { digitalWrite(stepPin3, LOW); }
}

void startMotor(uint8_t i) {
  step_counter[i] = 0;
  step_tick_us[i] = micros();
  step_state[i] = LOW;
  motor_flag[i] = 1;
  if (i == 0) digitalWrite(ENA_M1, MOTOR_ENABLE); // Peristaltic pump
  if (i == 1) digitalWrite(ENA_M2, MOTOR_ENABLE);
}

void processMotor(uint8_t i, uint8_t stepPin) {
  if (!motor_flag[i]) return;
  uint32_t now = micros();
  if ((uint32_t)(now - step_tick_us[i]) < speed_delay_us) return;
  step_tick_us[i] = now;
  step_state[i] = !step_state[i];
  digitalWrite(stepPin, step_state[i]);
  if (step_state[i] == LOW) step_counter[i]++;
}

void motor_no_of_rotn_check() {
  if (motor_flag[0] && step_counter[0] >= (uint32_t)STEPS_PER_REV * no_of_rotations[0]) stopMotor(0);
  if (motor_flag[1] && step_counter[1] >= (uint32_t)STEPS_PER_REV * no_of_rotations[1]) stopMotor(1);
  if (motor_flag[2] && step_counter[2] >= (uint32_t)STEPS_PER_REV * no_of_rotations[2]) stopMotor(2);
}

void handleRoot() { server.send(200, "text/html", index_html); }
void senddata() { server.send(200, "application/json", json); }

void getdata() {
  String msg = "";
  for (uint8_t i = 0; i < server.args(); i++) msg += server.argName(i) + String(":") + server.arg(i) + "\n";
  json = msg;
  server.send(200, "text/plain", "RECEIVED");
}

void setdata() {
  int rot = server.arg("do").toInt();
  int spd = server.arg("ph").toInt();
  int sel = server.arg("tds").toInt();
  if (rot < 1) rot = 1;
  if (spd < 1 || spd > 5) spd = 1;
  if (sel < 1 || sel > 3) sel = 1;

  speed_level = spd;
  speed_delay_us = mapSpeedLevel(speed_level);
  no_of_rotations[sel - 1] = (uint16_t)rot;

  EEPROM.write(M1_SPEED_ADDR, (uint8_t)speed_level);
  EEPROM.write(M1_NO_OF_ROTN_ADDR, (uint8_t)no_of_rotations[0]);
  EEPROM.write(M2_NO_OF_ROTN_ADDR, (uint8_t)no_of_rotations[1]);
  EEPROM.write(M3_NO_OF_ROTN_ADDR, (uint8_t)no_of_rotations[2]);
  EEPROM.commit();
  server.send(200, "text/plain", "SET OK");
}

void updater() {
  int motor_no = server.arg("motor_no").toInt();
  int run = server.arg("run").toInt();
  if (motor_no < 1 || motor_no > 3) { server.send(400, "text/plain", "Invalid motor_no"); return; }
  uint8_t idx = motor_no - 1;
  if (run == 1) startMotor(idx); else stopMotor(idx);
  server.send(200, "text/plain", "OK");
}

void read_eeprom_data() {
  no_of_rotations[0] = max((int)EEPROM.read(M1_NO_OF_ROTN_ADDR), 1);
  no_of_rotations[1] = max((int)EEPROM.read(M2_NO_OF_ROTN_ADDR), 1);
  no_of_rotations[2] = max((int)EEPROM.read(M3_NO_OF_ROTN_ADDR), 1);
  speed_level = EEPROM.read(M1_SPEED_ADDR);
  if (speed_level < 1 || speed_level > 5) speed_level = 1;
  speed_delay_us = mapSpeedLevel(speed_level);
}

void setup() {
  Serial.begin(115200);
  EEPROM.begin(512);
  WiFi.softAP(ssid, password);

  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(stepPin1, OUTPUT); pinMode(dirPin1, OUTPUT); pinMode(ENA_M1, OUTPUT);
  pinMode(stepPin2, OUTPUT); pinMode(dirPin2, OUTPUT); pinMode(ENA_M2, OUTPUT);
  pinMode(stepPin3, OUTPUT); pinMode(dirPin3, OUTPUT);

  digitalWrite(dirPin1, CLK_WISE); digitalWrite(dirPin2, CLK_WISE); digitalWrite(dirPin3, CLK_WISE);
  digitalWrite(ENA_M1, MOTOR_DISABLE); digitalWrite(ENA_M2, MOTOR_DISABLE);

  read_eeprom_data();

  server.on("/", handleRoot);
  server.on("/data", HTTP_POST, getdata);
  server.on("/sensor", senddata);
  server.on("/update", updater);
  server.on("/set", setdata);
  server.begin();
}

void loop() {
  server.handleClient();
  processMotor(0, stepPin1);
  processMotor(1, stepPin2);
  processMotor(2, stepPin3);
  motor_no_of_rotn_check();
}
