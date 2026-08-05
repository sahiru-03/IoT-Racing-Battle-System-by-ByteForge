#include <WiFi.h>
#include <FirebaseESP32.h>
#include <ESP32Servo.h>

// ---------- Credentials ----------
#define WIFI_SSID "" // Setup your wifi ssid
#define WIFI_PASSWORD "" // Setup your wifi password
#define FIREBASE_HOST "" // Add firebase host
#define FIREBASE_AUTH "" // Add firebase auth

// Objects
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

Servo servo1, servo2, servo3, servo4;

#define SERVO1_PIN 19
#define SERVO2_PIN 18
#define SERVO3_PIN 21
#define SERVO4_PIN 14

// ---------- Path variable ----------
int pathValue = 0;
String raceStatus = "";

// ---------- Firebase read timing ----------
unsigned long lastFirebaseRead = 0;
const unsigned long firebaseReadInterval = 200;

// ---------- Barrier 1 variables ----------
int currentPulseB1 = 500;
int minPulseB1 = 500;
int maxPulseB1 = 1500;
int stepSizeB1 = 30;
unsigned long lastUpdateB1 = 0;
int intervalB1 = 1;
bool goingUpB1 = true;

// ---------- Barrier 2 variables ----------
int currentPulseB2 = 500;
int minPulseB2 = 500;
int maxPulseB2 = 1500;
int stepSizeB2 = 30;
unsigned long lastUpdateB2 = 0;
int intervalB2 = 1;
bool goingUpB2 = true;

void path1() {
  servo1.write(90);
  servo2.write(65);
  Serial.println("Path 1");
}

void path2() {
  servo1.write(30);
  servo2.write(65);
  Serial.println("Path 2");
}

void path3() {
  servo1.write(95);
  servo2.write(110);
  Serial.println("Path 3");
}

// ---------- Barrier 1 function ----------
void barrier1() {
  if (millis() - lastUpdateB1 >= intervalB1) {
    lastUpdateB1 = millis();

    if (goingUpB1) {
      currentPulseB1 += stepSizeB1;
      if (currentPulseB1 >= maxPulseB1) goingUpB1 = false;
    } else {
      currentPulseB1 -= stepSizeB1;
      if (currentPulseB1 <= minPulseB1) goingUpB1 = true;
    }

    servo3.writeMicroseconds(currentPulseB1);
  }
}

// ---------- Barrier 2 function ----------
void barrier2() {
  if (millis() - lastUpdateB2 >= intervalB2) {
    lastUpdateB2 = millis();

    if (goingUpB2) {
      currentPulseB2 += stepSizeB2;
      if (currentPulseB2 >= maxPulseB2) goingUpB2 = false;
    } else {
      currentPulseB2 -= stepSizeB2;
      if (currentPulseB2 <= minPulseB2) goingUpB2 = true;
    }

    servo4.writeMicroseconds(currentPulseB2);
  }
}

void readFirebaseData() {
  if (millis() - lastFirebaseRead >= firebaseReadInterval) {
    lastFirebaseRead = millis();

    if (Firebase.ready()) {
      // Read path_selection
      if (Firebase.getInt(fbdo, "/path_selection")) {
        pathValue = fbdo.intData();

        static int lastPath = -1;

        if (pathValue != lastPath) {
          if (pathValue == 1) {
            path1();
          } else if (pathValue == 2) {
            path2();
          } else if (pathValue == 3) {
            path3();
          }

          lastPath = pathValue;
        }
      } else {
        Serial.print("Failed to read path_selection: ");
        Serial.println(fbdo.errorReason());
      }

      // Read race_status
      if (Firebase.getString(fbdo, "/race_status")) {
        raceStatus = fbdo.stringData();
      } else {
        Serial.print("Failed to read race_status: ");
        Serial.println(fbdo.errorReason());
      }
    }
  }
}

void setup() {
  Serial.begin(115200);

  servo1.attach(SERVO1_PIN);
  servo2.attach(SERVO2_PIN);
  servo3.attach(SERVO3_PIN);
  servo4.attach(SERVO4_PIN);

  servo3.writeMicroseconds(500);
  servo4.writeMicroseconds(500);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi Connected");

  config.host = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_AUTH;

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
}

void loop() {
  readFirebaseData();

  if (raceStatus == "started") {

    if (pathValue == 3) {
      barrier1();
    }

    if (pathValue == 1 || pathValue == 2 || pathValue == 3) {
      barrier2();
    }

  } else {
    servo3.writeMicroseconds(500);
    servo4.writeMicroseconds(500);
  }
}