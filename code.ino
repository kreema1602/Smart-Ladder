#include <WiFi.h>
#include <DHTesp.h>
#include <PubSubClient.h>

const char* ssid = "arduino";
const char* password = "00001111";

const int FlameSensorMin = 0;     //  sensor minimum
const int FlameSensorMax = 4095;  // sensor maximum
const int pinFlame = 32;

DHTesp dhtSensor;
const int DHT_pin = 16;

// Motion sensor
const int pinMotion = 33;

// Smoke sensor
const int pinSmoke = 35;

// Motor
const int pinMotor3 = 25;
const int pinMotor4 = 26;

// Buzzer
const int pinBuzzer = 21;

//***Set server***
const char* mqttServer = "broker.hivemq.com";
int port = 1883;

const String id = "071350";

bool ladder = 0;
bool preFire = 0;
bool preFlame = 0;
bool preHuman = 0;
bool fire = 0;
bool flame = 0;

bool initAll = 0;

bool dataReq = 0;

long long sendTime = 0;
long long ladderTime = 0;

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// Cam bien lua
bool isFire() {
  int sensorReading = analogRead(pinFlame);
  int range = map(sensorReading, FlameSensorMin, FlameSensorMax, 0, 2);

  switch (range) {
    case 0:
      return true;
      break;
    case 1:
      return true;
      break;
    case 2:
      return false;
      break;
  }
}

// Ket noi wifi
void wifiConnect() {
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("Connected!");
}

// MQTT
void mqttConnect() {
  while (!mqttClient.connected()) {
    Serial.println("Attemping MQTT connection...");
    String clientId = "ESP32Client-";
    clientId += id;
    if (mqttClient.connect(clientId.c_str())) {
      Serial.println("connected");

      //***Subscribe all topic you need***
      mqttClient.subscribe("071350/ladder");
      mqttClient.subscribe("071350/getSensor");
    } else {
      Serial.println("try again in 2 seconds");
      delay(2000);
    }
  }
}

//MQTT Receiver
void callback(char* topic, byte* message, unsigned int length) {
  String strMsg;
  for (int i = 0; i < length; i++) {
    strMsg += (char)message[i];
  }
  if (strcmp(topic, "071350/ladder") == 0) {
    if (strMsg == "up") ladderUp();
    else if (strMsg == "down") ladderDown();
  }
  if (strcmp(topic, "071350/getSensor") == 0) {
    dataReq = 1;
  }
}

// Dieu khien thang
void ladderUp() {
  if (!ladder) return;
  if (millis() - ladderTime < 8000) return;

  digitalWrite(pinMotor3, HIGH);
  digitalWrite(pinMotor4, LOW);
  delay(600);
  digitalWrite(pinMotor3, LOW);

  ladder = 0;

  char buffer[50];
  String topic = id + "/ladderState";
  sprintf(buffer, "%s", "Is collapsed");
  mqttClient.publish(topic.c_str(), buffer);
}

void ladderDown() {
  if (ladder) return;

  digitalWrite(pinMotor3, LOW);
  digitalWrite(pinMotor4, HIGH);
  delay(600);
  digitalWrite(pinMotor4, LOW);

  ladder = 1;
  char buffer[50];
  String topic = id + "/ladderState";
  sprintf(buffer, "%s", "Is expanded");
  mqttClient.publish(topic.c_str(), buffer);
  ladderTime = millis();
}

void setup() {
  Serial.begin(115200);
  Serial.print("Connecting to WiFi");

  pinMode(pinSmoke, INPUT);
  pinMode(pinFlame, INPUT);
  pinMode(pinMotion, INPUT);

  pinMode(pinBuzzer, OUTPUT);
  pinMode(pinMotor3, OUTPUT);
  pinMode(pinMotor4, OUTPUT);

  //wifi
  wifiConnect();
  mqttClient.setServer(mqttServer, port);
  mqttClient.setCallback(callback);
  mqttClient.setKeepAlive(90);

  //DHT
  dhtSensor.setup(DHT_pin, DHTesp::DHT11);
}

void loop() {
  if (!mqttClient.connected()) mqttConnect();
  mqttClient.loop();

  if (!initAll) {
    char buffer[50];
    String topic = id + "/ladderState";
    sprintf(buffer, "%s", "Is collapsed");
    mqttClient.publish(topic.c_str(), buffer);

    topic = id + "/human";
    sprintf(buffer, "%s", "no");
    mqttClient.publish(topic.c_str(), buffer);

    topic = id + "/flame";
    sprintf(buffer, "%d", 0);
    mqttClient.publish(topic.c_str(), buffer);

    initAll = 1;
  }

  //Get DHT data
  TempAndHumidity data = dhtSensor.getTempAndHumidity();
  float temp = data.temperature;
  float humid = data.humidity;

  char buffer[50];
  String topic;
  int smokeValue = analogRead(pinSmoke);

  //Code cam bien nguoi
  bool humanValue = digitalRead(pinMotion);
  char* human;
  if (humanValue) {
    human = "yes";
  } else {
    human = "no";
  }
  if (humanValue != preHuman) {
    topic = id + "/human";
    sprintf(buffer, "%s", human);
    mqttClient.publish(topic.c_str(), buffer);
  }
  preHuman = humanValue;

  // Code phat hien chay
  flame = isFire();
  if (flame || temp > 45.0 || smokeValue > 2300) {
    if (digitalRead(pinMotion) == HIGH) {
      digitalWrite(pinBuzzer, HIGH);
      delay(200);
      digitalWrite(pinBuzzer, LOW);
      delay(200);
    }

    ladderDown();
    fire = 1;
  }

  //Code keo thang khi an toan
  if (!flame && temp < 35 && smokeValue < 2000) {
    fire = 0;
    ladderUp();
  }

  // Code gui thong tin khi co chay
  if (preFire != fire) {
    topic = id + "/fire";
    int smokeMsg = (smokeValue > 2300) ? smokeValue : 0;
    int tempMsg = (temp > 45) ? temp : 0;
    sprintf(buffer, "%d,%d,%d,%d", fire, flame, tempMsg, smokeMsg);
    mqttClient.publish(topic.c_str(), buffer);
  }

  // Code gui thong tin cam bien lua
  if (preFlame != flame) {
    topic = id + "/flame";
    sprintf(buffer, "%d", flame);
    mqttClient.publish(topic.c_str(), buffer);
  }

  // Tra loi request tu Telegram
  if (dataReq) {
    Serial.print("datasend");
    topic = id + "/sensorData";
    sprintf(buffer, "%f,%f,%d", temp, humid, smokeValue);
    mqttClient.publish(topic.c_str(), buffer);
    dataReq = 0;
  }

  //Gui thong tin sensor len node-red
  if ((millis() - sendTime) >= 5000) {
    topic = id + "/temperature";
    sprintf(buffer, "%f", temp);
    mqttClient.publish(topic.c_str(), buffer);

    topic = id + "/humid";
    sprintf(buffer, "%f", humid);
    mqttClient.publish(topic.c_str(), buffer);


    topic = id + "/gas";
    sprintf(buffer, "%d", smokeValue);
    mqttClient.publish(topic.c_str(), buffer);
    sendTime = millis();

    topic = id + "/flame";
    sprintf(buffer, "%d", flame);
    mqttClient.publish(topic.c_str(), buffer);

    topic = id + "/human";
    sprintf(buffer, "%s", human);
    mqttClient.publish(topic.c_str(), buffer);
  }

  preFlame = flame;
  preFire = fire;
}
