#include <ESP32Servo.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_INA219.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <math.h>
#include <string.h>

// =======================================================
// IMPORTANT:
// Set this to the SAME WiFi channel printed by controller.
// Example: if controller prints WiFi Channel: 6, put 6 here.
// =======================================================
const uint8_t ESPNOW_CHANNEL = 6;

// =========================
// Speed smoothing
// =========================
const int SPEED_SMOOTHING_SAMPLES = 5;
float speedBuffer[SPEED_SMOOTHING_SAMPLES] = {0};
int speedBufferIndex = 0;

// =========================
// Actuators
// =========================
Servo esc;
Servo servo;

// =========================
// Pins / constants
// =========================
const int ESC_PIN = 18;
const int SERVO_PIN = 19;

const int THROTTLE_MIN = 1530;
const int THROTTLE_MAX_FORWARD = 2000;
const int THROTTLE_MAX_REVERSE = 1000;

const int RAW_MAX = 7;

// Ramp-to-neutral behavior
const int RAMP_STEP_US = 10;
const unsigned long RAMP_INTERVAL_MS = 10;

// Brake LEDs
const int LED_PIN1 = 12;
const int LED_PIN2 = 13;
const int LED_PIN3 = 26;
const int LED_PIN4 = 27;

// I2C / sensors
const int I2C_SDA = 17;
const int I2C_SCL = 16;
const int ONE_WIRE_BUS = 4;

// Collision system
const int COLLISION_SWITCH_PIN = 25; // KW12 all NO pins connected here
const int MPU_INT_PIN = 5;           // MPU6050 INT pin, optional

// =========================
// TCRT5000 5-channel IR lap detection
// =========================
// Change pins according to your wiring.
const int IR1_PIN = 32;
const int IR2_PIN = 33;
const int IR3_PIN = 34;
const int IR4_PIN = 35;
const int IR5_PIN = 23;

// Most TCRT5000 modules output LOW on black.
// If your module outputs HIGH on black, set this to false.
const bool BLACK_IS_LOW = true;

// Prevent same black line crossing from sending many lap events.
// Long anti-cheat/minimum lap time is handled in controller.
const unsigned long LAP_EVENT_COOLDOWN_MS = 1500;
const unsigned long IR_PRINT_INTERVAL_MS = 500;

// Event repeat improves ESP-NOW reliability.
// Controller ignores duplicates using collisionSeq/lapSeq.
const int EVENT_SEND_REPEAT_COUNT = 3;

// Collision source values
const uint8_t COLLISION_SOURCE_NONE = 0;
const uint8_t COLLISION_SOURCE_KW12 = 1;
const uint8_t COLLISION_SOURCE_MPU  = 2;
const uint8_t COLLISION_SOURCE_BOTH = 3;

// Collision tuning
const unsigned long COLLISION_COOLDOWN_MS = 1500;
const unsigned long KW12_DEBOUNCE_MS = 40;
const unsigned long MPU_SAMPLE_INTERVAL_MS = 20;

const float GRAVITY_MS2 = 9.80665f;
const float MPU_IMPACT_THRESHOLD_MS2 = 12.0f;

// Battery mapping
const float BATTERY_FULL_V = 11.1f;
const float BATTERY_EMPTY_V = 9.9f;

// Temperature update
const unsigned long TEMP_UPDATE_INTERVAL = 1000;

// Debug/print throttling
const unsigned long BATTERY_PRINT_INTERVAL = 1000;
const unsigned long SENSOR_WARN_INTERVAL = 3000;

// =========================
// Sensor objects
// =========================
TwoWire I2C_BUS = TwoWire(0);
Adafruit_INA219 ina219;
Adafruit_MPU6050 mpu;

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// =========================
// Sensor availability flags
// =========================
bool ina219Available = false;
bool ds18b20Available = false;
bool mpuAvailable = false;

// =========================
// Shared ESP-NOW receive data
// =========================
volatile int v_raw1 = 0;
volatile int v_raw2 = 0;
volatile int v_dir  = 0;
volatile float v_angle = 0;
volatile int percent = -1;

// =========================
// Message structs
// =========================
typedef struct __attribute__((packed)) message {
  int value1;
  int value2;
  int direction;
  float angle;
} message;

// IMPORTANT:
// Controller telemetry struct must match this exactly.
typedef struct __attribute__((packed)) telemetry {
  uint32_t seq;
  float v_ms;
  int percentage;
  float temperature;

  bool collisionHit;
  uint8_t collisionSource;
  uint32_t collisionSeq;

  bool lapHit;
  uint32_t lapSeq;
} telemetry;

message lastMsg;
telemetry txTelem;
uint32_t telemSeq = 0;

// Destination MAC = controller MAC
uint8_t peerMAC[6] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }; // Add relevant controller MAC Address to connect with it

// =========================
// Runtime state
// =========================
unsigned long lastRampMillis = 0;
int currentThrottleUs = THROTTLE_MIN;
int targetThrottleUs  = THROTTLE_MIN;
bool brakeApplied = false;

float currentTemperatureC = NAN;
float batteryVoltage_V = NAN;

// Collision runtime state
bool pendingCollision = false;
uint8_t pendingCollisionSource = COLLISION_SOURCE_NONE;
uint32_t collisionSeqCounter = 0;
unsigned long lastCollisionMillis = 0;
int collisionRepeatRemaining = 0;

// Lap runtime state
bool pendingLap = false;
uint32_t lapSeqCounter = 0;
unsigned long lastLapMillis = 0;
bool previousBlackDetected = false;
int lapRepeatRemaining = 0;

// =========================
// Helpers
// =========================
float clampf(float v, float lo, float hi) {
  return (v < lo) ? lo : (v > hi) ? hi : v;
}

int voltageToPercent(float volts) {
  volts = clampf(volts, BATTERY_EMPTY_V, BATTERY_FULL_V);
  float pct = (volts - BATTERY_EMPTY_V) / (BATTERY_FULL_V - BATTERY_EMPTY_V) * 100.0f;

  int ipct = (int)round(pct);
  if (ipct < 0) ipct = 0;
  if (ipct > 100) ipct = 100;

  return ipct;
}

int mapRawToThrottleOffset(int raw) {
  raw = constrain(raw, 0, RAW_MAX);
  return map(raw, 0, RAW_MAX, 0, 85);
}

void setBrakeLights(bool on) {
  digitalWrite(LED_PIN1, on ? HIGH : LOW);
  digitalWrite(LED_PIN2, on ? HIGH : LOW);
  digitalWrite(LED_PIN3, on ? HIGH : LOW);
  digitalWrite(LED_PIN4, on ? HIGH : LOW);
}

// =========================
// Collision helpers
// =========================
void queueCollision(uint8_t source) {
  if (source == COLLISION_SOURCE_NONE) return;

  unsigned long now = millis();

  if (pendingCollision) {
    pendingCollisionSource |= source;

    if (pendingCollisionSource > COLLISION_SOURCE_BOTH) {
      pendingCollisionSource = COLLISION_SOURCE_BOTH;
    }

    collisionRepeatRemaining = EVENT_SEND_REPEAT_COUNT;
    return;
  }

  if (now - lastCollisionMillis < COLLISION_COOLDOWN_MS) {
    return;
  }

  lastCollisionMillis = now;
  pendingCollision = true;
  pendingCollisionSource = source;
  collisionSeqCounter++;
  collisionRepeatRemaining = EVENT_SEND_REPEAT_COUNT;

  Serial.print("COLLISION DETECTED | source=");
  Serial.print(pendingCollisionSource);
  Serial.print(" | collisionSeq=");
  Serial.println(collisionSeqCounter);
}

void checkKW12Collision() {
  static int lastReading = LOW;
  static int stableState = LOW;
  static unsigned long lastChangeTime = 0;

  int reading = digitalRead(COLLISION_SWITCH_PIN);

  if (reading != lastReading) {
    lastChangeTime = millis();
    lastReading = reading;
  }

  if ((millis() - lastChangeTime) >= KW12_DEBOUNCE_MS) {
    if (reading != stableState) {
      stableState = reading;

      if (stableState == HIGH) {
        queueCollision(COLLISION_SOURCE_KW12);
      }
    }
  }
}

void checkMPUCollision(unsigned long now) {
  static unsigned long lastMpuSample = 0;
  static unsigned long lastMpuWarn = 0;

  if (!mpuAvailable) {
    if (now - lastMpuWarn >= SENSOR_WARN_INTERVAL) {
      lastMpuWarn = now;
      Serial.println("MPU6050 unavailable.");
    }
    return;
  }

  if (now - lastMpuSample < MPU_SAMPLE_INTERVAL_MS) return;
  lastMpuSample = now;

  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  float ax = a.acceleration.x;
  float ay = a.acceleration.y;
  float az = a.acceleration.z;

  float accelMag = sqrtf(ax * ax + ay * ay + az * az);
  float impactValue = fabsf(accelMag - GRAVITY_MS2);

  if (impactValue >= MPU_IMPACT_THRESHOLD_MS2) {
    Serial.print("MPU impact value: ");
    Serial.println(impactValue, 3);
    queueCollision(COLLISION_SOURCE_MPU);
  }
}

// =========================
// TCRT5000 lap helpers
// =========================
bool isBlackDetectedOnPin(int pin) {
  int value = digitalRead(pin);

  if (BLACK_IS_LOW) {
    return value == LOW;
  } else {
    return value == HIGH;
  }
}

void queueLapEvent() {
  unsigned long now = millis();

  if (pendingLap) {
    lapRepeatRemaining = EVENT_SEND_REPEAT_COUNT;
    return;
  }

  if (now - lastLapMillis < LAP_EVENT_COOLDOWN_MS) {
    return;
  }

  lastLapMillis = now;
  pendingLap = true;
  lapSeqCounter++;
  lapRepeatRemaining = EVENT_SEND_REPEAT_COUNT;

  Serial.println();
  Serial.println("=================================");
  Serial.println("BLACK FINISH LINE DETECTED!");
  Serial.print("lapSeq=");
  Serial.println(lapSeqCounter);
  Serial.println("Lap event queued for controller.");
  Serial.println("=================================");
  Serial.println();
}

void checkLapLine() {
  unsigned long now = millis();

  int s1 = digitalRead(IR1_PIN);
  int s2 = digitalRead(IR2_PIN);
  int s3 = digitalRead(IR3_PIN);
  int s4 = digitalRead(IR4_PIN);
  int s5 = digitalRead(IR5_PIN);

  bool black1 = isBlackDetectedOnPin(IR1_PIN);
  bool black2 = isBlackDetectedOnPin(IR2_PIN);
  bool black3 = isBlackDetectedOnPin(IR3_PIN);
  bool black4 = isBlackDetectedOnPin(IR4_PIN);
  bool black5 = isBlackDetectedOnPin(IR5_PIN);

  bool blackDetected = black1 || black2 || black3 || black4 || black5;

  // Edge detection: only no-black -> black
  if (blackDetected && !previousBlackDetected) {
    queueLapEvent();
  }

  previousBlackDetected = blackDetected;

  static unsigned long lastIrPrint = 0;

  if (now - lastIrPrint >= IR_PRINT_INTERVAL_MS) {
    lastIrPrint = now;

    Serial.print("IR: ");
    Serial.print(s1);
    Serial.print(" ");
    Serial.print(s2);
    Serial.print(" ");
    Serial.print(s3);
    Serial.print(" ");
    Serial.print(s4);
    Serial.print(" ");
    Serial.print(s5);

    Serial.print(" | Black: ");
    Serial.print(blackDetected ? "YES" : "NO");

    Serial.print(" | pendingLap: ");
    Serial.print(pendingLap ? "YES" : "NO");

    Serial.print(" | lapSeq: ");
    Serial.println(lapSeqCounter);
  }
}

// =========================
// ESP-NOW callbacks
// =========================
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len != sizeof(message)) return;

  message tmp;
  memcpy(&tmp, data, sizeof(tmp));
  lastMsg = tmp;

  v_raw1 = tmp.value1 < 0 ? 0 : tmp.value1;
  v_raw2 = tmp.value2 < 0 ? 0 : tmp.value2;
  v_dir  = tmp.direction;
  v_angle = tmp.angle;
}

void OnDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  static unsigned long lastPrint = 0;
  unsigned long now = millis();

  if (now - lastPrint >= 1000) {
    lastPrint = now;
    Serial.print("ESP-NOW telemetry send: ");
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Success" : "Fail");
  }
}

// =========================
// Setup
// =========================
void setup() {
  Serial.begin(115200);
  delay(50);

  pinMode(LED_PIN1, OUTPUT);
  pinMode(LED_PIN2, OUTPUT);
  pinMode(LED_PIN3, OUTPUT);
  pinMode(LED_PIN4, OUTPUT);
  setBrakeLights(false);

  pinMode(COLLISION_SWITCH_PIN, INPUT);
  pinMode(MPU_INT_PIN, INPUT);

  pinMode(IR1_PIN, INPUT);
  pinMode(IR2_PIN, INPUT);
  pinMode(IR3_PIN, INPUT);
  pinMode(IR4_PIN, INPUT);
  pinMode(IR5_PIN, INPUT);

  esc.setPeriodHertz(50);
  esc.attach(ESC_PIN);

  servo.setPeriodHertz(50);
  servo.attach(SERVO_PIN, 500, 2500);
  servo.write(90);

  esc.writeMicroseconds(THROTTLE_MIN);
  delay(200);

  I2C_BUS.begin(I2C_SDA, I2C_SCL, 100000);

  ina219Available = ina219.begin(&I2C_BUS);
  if (ina219Available) {
    Serial.println("INA219 initialized.");
  } else {
    Serial.println("WARNING: INA219 not found. Battery telemetry disabled.");
  }

  mpuAvailable = mpu.begin(0x68, &I2C_BUS);
  if (mpuAvailable) {
    Serial.println("MPU6050 initialized at address 0x68.");
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  } else {
    Serial.println("WARNING: MPU6050 not found. MPU collision detection disabled.");
  }

  sensors.begin();
  sensors.setWaitForConversion(false);

  if (sensors.getDeviceCount() > 0) {
    ds18b20Available = true;
    sensors.requestTemperatures();
    Serial.println("DS18B20 initialized.");
  } else {
    Serial.println("WARNING: DS18B20 not found. Temperature telemetry disabled.");
  }

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.disconnect();

  esp_err_t chRes = esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  Serial.print("ESP-NOW channel set to: ");
  Serial.print(ESPNOW_CHANNEL);
  Serial.print(" result=");
  Serial.println(chRes);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed.");
    while (true) delay(1000);
  }

  esp_now_register_recv_cb(OnDataRecv);
  esp_now_register_send_cb(OnDataSent);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, peerMAC, 6);

#ifdef ESP_IF_WIFI_STA
  peerInfo.ifidx = ESP_IF_WIFI_STA;
#endif

  peerInfo.channel = ESPNOW_CHANNEL;
  peerInfo.encrypt = false;

  esp_err_t peerRes = esp_now_add_peer(&peerInfo);

  if (peerRes == ESP_OK || peerRes == ESP_ERR_ESPNOW_EXIST) {
    Serial.println("ESP-NOW peer ready.");
  } else {
    Serial.print("ESP-NOW peer add error: ");
    Serial.println(peerRes);
  }

  Serial.println("Car system ready.");
}

// =========================
// Loop
// =========================
void loop() {
  unsigned long now = millis();

  checkKW12Collision();
  checkMPUCollision(now);
  checkLapLine();

  int raw1 = v_raw1;
  int raw2 = v_raw2;
  int dir  = v_dir;
  float angle = v_angle * -1;

  int nAngle = (int)(angle / 3.0f);
  int servoPos = constrain(90 + nAngle, 0, 180);
  servo.write(servoPos);

  if (raw2 > 0) {
    targetThrottleUs = THROTTLE_MIN;
    currentThrottleUs = THROTTLE_MIN;
    esc.writeMicroseconds(THROTTLE_MIN);
    setBrakeLights(true);
    brakeApplied = true;
  } else {
    brakeApplied = false;
    setBrakeLights(false);

    if (raw1 > 0) {
      int offset = mapRawToThrottleOffset(raw1);

      if (dir == 0) {
        targetThrottleUs = THROTTLE_MIN + offset;
      } else if (dir == 1) {
        targetThrottleUs = THROTTLE_MIN - offset;
      } else {
        targetThrottleUs = THROTTLE_MIN;
      }

      currentThrottleUs = constrain(targetThrottleUs, THROTTLE_MAX_REVERSE, THROTTLE_MAX_FORWARD);
      esc.writeMicroseconds(currentThrottleUs);
    } else {
      targetThrottleUs = THROTTLE_MIN;

      if (now - lastRampMillis >= RAMP_INTERVAL_MS) {
        lastRampMillis = now;

        if (currentThrottleUs > THROTTLE_MIN) {
          currentThrottleUs = max(currentThrottleUs - RAMP_STEP_US, THROTTLE_MIN);
        } else if (currentThrottleUs < THROTTLE_MIN) {
          currentThrottleUs = min(currentThrottleUs + RAMP_STEP_US, THROTTLE_MIN);
        }

        esc.writeMicroseconds(currentThrottleUs);
      }
    }
  }

  // DS18B20 non-blocking read
  static unsigned long lastTempTime = 0;
  static unsigned long lastTempWarn = 0;

  if (ds18b20Available && (now - lastTempTime >= TEMP_UPDATE_INTERVAL)) {
    lastTempTime = now;

    float t = sensors.getTempCByIndex(0);

    if (t == DEVICE_DISCONNECTED_C) {
      ds18b20Available = false;
      currentTemperatureC = NAN;

      if (now - lastTempWarn >= SENSOR_WARN_INTERVAL) {
        lastTempWarn = now;
        Serial.println("WARNING: DS18B20 disconnected during runtime.");
      }
    } else {
      currentTemperatureC = t;
      sensors.requestTemperatures();
    }
  }

  // =========================
  // Speed estimate + telemetry send
  // =========================
  static unsigned long lastSpeedSample = 0;
  const unsigned long SPEED_SAMPLE_MS = 50;
  const float WHEEL_DIAMETER_M = 0.065f;
  const float MAX_RPM = 2200.0f * 11.1f;

  if (now - lastSpeedSample >= SPEED_SAMPLE_MS) {
    lastSpeedSample = now;

    float throttleRatio = 0.0f;

    if (currentThrottleUs > THROTTLE_MIN) {
      throttleRatio = (float)(currentThrottleUs - THROTTLE_MIN) / 500.0f;
    } else if (currentThrottleUs < THROTTLE_MIN) {
      throttleRatio = -((float)(THROTTLE_MIN - currentThrottleUs) / 500.0f);
    }

    float motorRPM = throttleRatio * MAX_RPM;
    float circumference = 3.1416f * WHEEL_DIAMETER_M;
    float v_ms = motorRPM * circumference / 60.0f;

    speedBuffer[speedBufferIndex] = v_ms;
    speedBufferIndex = (speedBufferIndex + 1) % SPEED_SMOOTHING_SAMPLES;

    float v_ms_avg = 0.0f;

    for (int i = 0; i < SPEED_SMOOTHING_SAMPLES; i++) {
      v_ms_avg += speedBuffer[i];
    }

    v_ms_avg /= SPEED_SMOOTHING_SAMPLES;

    bool sendCollision = pendingCollision;
    uint8_t sendCollisionSource = sendCollision ? pendingCollisionSource : COLLISION_SOURCE_NONE;
    uint32_t sendCollisionSeq = sendCollision ? collisionSeqCounter : 0;

    bool sendLap = pendingLap;
    uint32_t sendLapSeq = sendLap ? lapSeqCounter : 0;

    txTelem.seq = ++telemSeq;
    txTelem.v_ms = v_ms_avg;
    txTelem.percentage = percent;
    txTelem.temperature = currentTemperatureC;

    txTelem.collisionHit = sendCollision;
    txTelem.collisionSource = sendCollisionSource;
    txTelem.collisionSeq = sendCollisionSeq;

    txTelem.lapHit = sendLap;
    txTelem.lapSeq = sendLapSeq;

    esp_err_t res = esp_now_send(peerMAC, (uint8_t *)&txTelem, sizeof(txTelem));

    if (res == ESP_OK) {
      if (sendCollision) {
        collisionRepeatRemaining--;

        if (collisionRepeatRemaining <= 0) {
          pendingCollision = false;
          pendingCollisionSource = COLLISION_SOURCE_NONE;
          collisionRepeatRemaining = 0;
        }
      }

      if (sendLap) {
        lapRepeatRemaining--;

        if (lapRepeatRemaining <= 0) {
          pendingLap = false;
          lapRepeatRemaining = 0;
        }
      }
    } else if (res == ESP_ERR_ESPNOW_NOT_FOUND) {
      esp_now_peer_info_t peerInfo = {};
      memcpy(peerInfo.peer_addr, peerMAC, 6);

#ifdef ESP_IF_WIFI_STA
      peerInfo.ifidx = ESP_IF_WIFI_STA;
#endif

      peerInfo.channel = ESPNOW_CHANNEL;
      peerInfo.encrypt = false;
      esp_now_add_peer(&peerInfo);
    }
  }

  // =========================
  // INA219 battery read
  // =========================
  static unsigned long lastBatteryPrint = 0;
  static unsigned long lastInaWarn = 0;

  if (ina219Available) {
    float shuntVoltage_mV = ina219.getShuntVoltage_mV();
    float shuntVoltage_V  = shuntVoltage_mV / 1000.0f;
    float busVoltage_V    = ina219.getBusVoltage_V();

    batteryVoltage_V = busVoltage_V + shuntVoltage_V;
    percent = voltageToPercent(batteryVoltage_V);

    if (now - lastBatteryPrint >= BATTERY_PRINT_INTERVAL) {
      lastBatteryPrint = now;

      Serial.print("Battery V: ");
      Serial.print(batteryVoltage_V, 3);
      Serial.print("  SOC: ");
      Serial.print(percent);
      Serial.println("%");
    }
  } else {
    batteryVoltage_V = NAN;
    percent = -1;

    if (now - lastInaWarn >= SENSOR_WARN_INTERVAL) {
      lastInaWarn = now;
      Serial.println("Battery telemetry unavailable (INA219 missing).");
    }
  }

  delay(10);
}