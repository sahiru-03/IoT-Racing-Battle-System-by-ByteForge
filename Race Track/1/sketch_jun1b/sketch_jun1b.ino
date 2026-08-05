#include <WiFi.h>
#include <FirebaseESP32.h>
#include <ESP32Servo.h>

// ---------- Credentials ----------
#define WIFI_SSID "" // Setup your wifi ssid
#define WIFI_PASSWORD "" // Setup your wifi password
#define FIREBASE_HOST "" // Add firebase host
#define FIREBASE_AUTH "" // Add firebase auth

// ---------- Firebase ----------
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// ---------- Servos ----------
Servo servo1, servo2, servo3;

#define SERVO1_PIN 18
#define SERVO2_PIN 19
#define SERVO3_PIN 21

// ---------- Firebase polling ----------
unsigned long lastFirebasePoll = 0;
const unsigned long FIREBASE_POLL_MS = 150;

// ---------- Firebase values ----------
int pathValue = 0;
String raceStatus = "stopped";

// ---------- Servo 3 Barrier Variables ----------
int currentPulse = 1500;       // reset/middle position
int minPulse = 500;            // minimum side
int maxPulse = 2500;           // maximum side
int stepSize = 30;              // movement step
unsigned long lastUpdate = 0;
int interval = 1;             // movement speed delay
bool goingUp = true;
bool barrierActive = false;

// ---------- Barrier Function ----------
void barrier() {
  // Barrier only works when race is started
  if (raceStatus != "started") {
    return;
  }

  // Barrier only works for path 1 or path 3
  if (!barrierActive) {
    return;
  }

  if (millis() - lastUpdate >= interval) {
    lastUpdate = millis();

    if (goingUp) {
      currentPulse += stepSize;

      if (currentPulse >= maxPulse) {
        currentPulse = maxPulse;
        goingUp = false;
      }
    } 
    else {
      currentPulse -= stepSize;

      if (currentPulse <= minPulse) {
        currentPulse = minPulse;
        goingUp = true;
      }
    }

    servo3.writeMicroseconds(currentPulse);
  }
}

// ---------- Servo Path Functions ----------
void path1() {
  servo1.write(90);
  servo2.write(115);

  // Servo 3 moves between 1000 and 1500
  currentPulse = 1000;
  minPulse = 1000;
  maxPulse = 1500;
  goingUp = true;
  barrierActive = true;

  servo3.writeMicroseconds(currentPulse);

  Serial.println("Servo Path 1 - Barrier active 1000 to 1500");
}

void path2() {
  servo1.write(150);
  servo2.write(115);

  // Servo 3 reset position
  barrierActive = false;
  currentPulse = 1000;
  servo3.writeMicroseconds(currentPulse);

  Serial.println("Servo Path 2 - Barrier reset");
}

void path3() {
  servo1.write(95);
  servo2.write(70);

  // Servo 3 moves between 1000 and 500
  currentPulse = 1000;
  minPulse = 500;
  maxPulse = 1000;
  goingUp = false;
  barrierActive = true;

  servo3.writeMicroseconds(currentPulse);

  Serial.println("Servo Path 3 - Barrier active 1000 to 500");
}

// ---------- Firebase Check ----------
void checkFirebase() {
  if (!Firebase.ready()) return;

  if (millis() - lastFirebasePoll < FIREBASE_POLL_MS) return;
  lastFirebasePoll = millis();

  // Read path_selection
  if (Firebase.getInt(fbdo, "/path_selection")) {
    int newPathValue = fbdo.intData();

    static int lastPath = -1;

    if (newPathValue != lastPath) {
      pathValue = newPathValue;

      if (pathValue == 1) {
        path1();
      } 
      else if (pathValue == 2) {
        path2();
      } 
      else if (pathValue == 3) {
        path3();
      }
      else {
        barrierActive = false;
        currentPulse = 1000;
        servo3.writeMicroseconds(currentPulse);

        Serial.println("Invalid path_selection - Barrier disabled");
      }

      Serial.print("Firebase path_selection changed to: ");
      Serial.println(pathValue);

      lastPath = pathValue;
    }
  } 
  else {
    Serial.print("Failed to read path_selection: ");
    Serial.println(fbdo.errorReason());
  }

  // Read race_status
  if (Firebase.getString(fbdo, "/race_status")) {
    String newRaceStatus = fbdo.stringData();

    static String lastRaceStatus = "";

    if (newRaceStatus != lastRaceStatus) {
      raceStatus = newRaceStatus;

      Serial.print("Firebase race_status changed to: ");
      Serial.println(raceStatus);

      if (raceStatus != "started") {
        Serial.println("Race not started - Barrier stopped");
      }

      lastRaceStatus = raceStatus;
    }
  } 
  else {
    Serial.print("Failed to read race_status: ");
    Serial.println(fbdo.errorReason());
  }
}

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);

  servo1.attach(SERVO1_PIN);
  servo2.attach(SERVO2_PIN);
  servo3.attach(SERVO3_PIN);

  // Initial servo positions
  servo1.write(90);
  servo2.write(90);

  // Servo 3 reset position
  currentPulse = 1000;
  servo3.writeMicroseconds(currentPulse);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi Connected");

  config.host = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_AUTH;

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  Serial.println("System Started - Servo Path Control with Barrier Servo");
}

// ---------- Loop ----------
void loop() {
  checkFirebase();

  // Barrier moves only if race_status = started
  barrier();
}