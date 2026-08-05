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

Servo servo1, servo2, servo3;

#define SERVO1_PIN 18
#define SERVO2_PIN 19
#define SERVO3_PIN 21

// Barrier variables
int currentPulse = 500;
int minPulse = 500;
int maxPulse = 1500;
int stepSize = 30;
unsigned long lastUpdate = 0;
int interval = 1;
bool goingUp = true;

// Firebase checking interval
unsigned long lastFirebaseCheck = 0;
const unsigned long firebaseCheckInterval = 200;

// Race status and path variables
bool raceStarted = false;
int pathValue = 0; // <-- DECLARED GLOBALLY HERE

// ---------------- PATH FUNCTIONS ----------------
void path1() {
  servo2.write(90);
  servo1.write(115);
  Serial.println("Path 1");
}

void path2() {
  servo2.write(150);
  servo1.write(115);
  Serial.println("Path 2");
}

void path3() {
  servo2.write(95);
  servo1.write(70);
  Serial.println("Path 3");
}

// ---------------- BARRIER FUNCTION ----------------
void barrier1() {
  if (millis() - lastUpdate >= interval) {
    lastUpdate = millis();

    if (goingUp) {
      currentPulse += stepSize;
      if (currentPulse >= maxPulse) {
        goingUp = false;
      }
    } else {
      currentPulse -= stepSize;
      if (currentPulse <= minPulse) {
        goingUp = true;
      }
    }

    servo3.writeMicroseconds(currentPulse);
  }
}

void setup() {
  Serial.begin(115200);

  servo1.attach(SERVO1_PIN);
  servo2.attach(SERVO2_PIN);
  servo3.attach(SERVO3_PIN);

  servo3.writeMicroseconds(minPulse);

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
  if (Firebase.ready()) {

    // Read Firebase only every 200ms
    if (millis() - lastFirebaseCheck >= firebaseCheckInterval) {
      lastFirebaseCheck = millis();

      // ---------- Read path_selection ----------
      if (Firebase.getInt(fbdo, "/path_selection")) {
        
        // <-- REMOVED "int" HERE. WE NOW UPDATE THE GLOBAL VARIABLE
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
      }

      // ---------- Read race_status ----------
      if (Firebase.getString(fbdo, "/race_status")) {
        String statusValue = fbdo.stringData();
        statusValue.trim();

        if (statusValue == "started") {
          raceStarted = true;
        } else {
          raceStarted = false;
        }

        Serial.print("Race Status: ");
        Serial.println(statusValue);
      }
    }
  }

  // Barrier runs only when race_status == "started"
  // pathValue is now globally recognized here!
  if (raceStarted && pathValue == 1) {
    barrier1();
  }
}