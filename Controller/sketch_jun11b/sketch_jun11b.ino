#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Firebase_ESP_Client.h>
#include <math.h>

// =========================
// Firebase / WiFi config
// =========================
#define WIFI_SSID ""  // Setup your wifi ssid
#define WIFI_PASSWORD "" // Setup your wifi password

#define API_KEY "" // Add firebase api key
#define DATABASE_URL "" // Add firebase database url

// =========================
// Player / car setup
// =========================
// Controller for car1 / player1 -> set 1
// Controller for car2 / player2 -> set 2
const int PLAYER_NUMBER = 1;

// Firebase dynamic paths
String playerName = "";
String playerNamePath = "";
String playerPenaltyPath = "";

// RTDB paths
const char *RACE_STATUS_PATH = "/race_status";
const char *LAP_SELECTION_PATH = "/lap_selection";
const char *FINAL_WINNER_PATH = "/finalwinner";
const char *RACE_BLOCK_REASON_PATH = "/race_block_reason";

// =========================
// Battery start protection
// =========================
const int MIN_START_BATTERY_PERCENT = 35;

// =========================
// Car status upload
// =========================
const unsigned long CAR_STATUS_UPDATE_INTERVAL = 2000;
unsigned long lastCarStatusUpdate = 0;
volatile unsigned long lastTelemetryReceivedMillis = 0;

// =========================
// Race status control
// =========================
String raceStatus = "unknown";
bool raceStarted = false;

const unsigned long RACE_STATUS_READ_INTERVAL = 500;
unsigned long lastRaceStatusRead = 0;

// Anti-reverse / minimum lap time.
// Set this LESS than fastest real lap time.
const unsigned long MIN_VALID_LAP_TIME_MS = 8000;

int localLapCount = 0;
int selectedLapsLocal = 0;
unsigned long raceStartMillis = 0;
unsigned long lastValidLapMillis = 0;

// =========================
// OLED config
// =========================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

// Destination MAC = car MAC
uint8_t receiverMAC[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; // Add relevant car MAC Address to connect with it

// =========================
// Firebase objects
// =========================
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
bool firebaseSignupOK = false;

// =========================
// Control message structure
// Must match car receiver message struct
// =========================
typedef struct __attribute__((packed)) message {
  int value1;
  int value2;
  int direction;
  float angle;
} message;

message bldcInfo;

// =========================
// Telemetry structure
// Must exactly match car telemetry struct
// =========================
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

// =========================
// Controller pins / constants
// =========================
const float DISTANCE = 11;
const int RAW_MAX_CONTROL = 7;

const int TRIG1 = 4;
const int ECHO1 = 17;
const int TRIG2 = 14;
const int ECHO2 = 27;
const unsigned long MAX_ECHO_TIME = 30000UL;

const int FORWARD_PIN = 25;
const int PIN_A = 18;
const int PIN_B = 19;
const int PIN_Z = 21;

// OLED I2C pins
const int SDA_PIN = 32;
const int SCL_PIN = 33;

const int PPR = 250;
const long COUNTS_PER_REV = (long)PPR * 4L;

volatile long encoderCount = 0;
volatile int lastEncoded = 0;
volatile float vSpeed = 0;
volatile int percent = -1;
volatile float temperature = NAN;

volatile bool zTriggered = false;
volatile long centerCount = 0;

portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

// =========================
// Penalty queue
// =========================
portMUX_TYPE penaltyMux = portMUX_INITIALIZER_UNLOCKED;

volatile int pendingPenaltyEvents = 0;
volatile uint8_t pendingPenaltySource = 0;
volatile uint32_t lastCollisionSeqReceived = 0;

int playerPenaltyLocal = 0;

// =========================
// Lap queue
// =========================
portMUX_TYPE lapMux = portMUX_INITIALIZER_UNLOCKED;

volatile int pendingLapEvents = 0;
volatile uint32_t lastLapSeqReceived = 0;

// =========================
// Display object
// =========================
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// =========================
// Forward declarations
// =========================
void OnDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status);
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len);

void configurePlayerPaths();
String getCarPath();
void readPlayerNameFromFirebase();
int readSelectedLapsFromFirebase();
void startNewRaceLocalState();

bool areBothCarsBatteryOK();
void updateOwnCarStatusToFirebase();

void queuePenaltyEvent(uint8_t source, uint32_t collisionSeq);
void processPendingPenaltyEvents();
bool incrementPlayerPenalty(uint8_t source);

void queueLapEvent(uint32_t lapSeq);
void processPendingLapEvents();
bool handleLapDetected();

void connectWiFiAndFirebase();
void setupEspNow();
void updateRaceStatusFromFirebase();

// =========================
// Quadrature decode ISR
// =========================
void IRAM_ATTR handleAB() {
  int MSB = digitalRead(PIN_A);
  int LSB = digitalRead(PIN_B);

  int encoded = (MSB << 1) | LSB;
  int sum = (lastEncoded << 2) | encoded;

  if (sum == 0b0001 || sum == 0b0111 || sum == 0b1110 || sum == 0b1000) {
    encoderCount++;
  } else if (sum == 0b0010 || sum == 0b0100 || sum == 0b1101 || sum == 0b1011) {
    encoderCount--;
  }

  lastEncoded = encoded;
}

void IRAM_ATTR handleZ() {
  portENTER_CRITICAL_ISR(&mux);
  centerCount = encoderCount;
  zTriggered = true;
  portEXIT_CRITICAL_ISR(&mux);
}

long safeReadEncoder() {
  portENTER_CRITICAL(&mux);
  long c = encoderCount;
  portEXIT_CRITICAL(&mux);
  return c;
}

long safeReadCenter() {
  portENTER_CRITICAL(&mux);
  long c = centerCount;
  portEXIT_CRITICAL(&mux);
  return c;
}

// =========================
// Player / car paths
// =========================
void configurePlayerPaths() {
  if (PLAYER_NUMBER == 1) {
    playerNamePath = "/player1";
    playerPenaltyPath = "/player1_penalty";
  } else {
    playerNamePath = "/player2";
    playerPenaltyPath = "/player2_penalty";
  }

  Serial.print("Player number: ");
  Serial.println(PLAYER_NUMBER);

  Serial.print("Player name path: ");
  Serial.println(playerNamePath);

  Serial.print("Penalty path: ");
  Serial.println(playerPenaltyPath);
}

String getCarPath() {
  if (PLAYER_NUMBER == 1) {
    return "/car1";
  } else {
    return "/car2";
  }
}

void readPlayerNameFromFirebase() {
  if (!firebaseSignupOK || !Firebase.ready()) {
    Serial.println("Cannot read player name: Firebase not ready.");
    return;
  }

  if (Firebase.RTDB.getString(&fbdo, playerNamePath.c_str())) {
    playerName = fbdo.stringData();
    playerName.trim();

    Serial.print("Loaded player name: ");
    Serial.println(playerName);
  } else {
    Serial.print("Failed to read player name: ");
    Serial.println(fbdo.errorReason());
  }
}

int readSelectedLapsFromFirebase() {
  if (!firebaseSignupOK || !Firebase.ready()) {
    Serial.println("Cannot read /lap_selection: Firebase not ready.");
    return 0;
  }

  if (Firebase.RTDB.getInt(&fbdo, LAP_SELECTION_PATH)) {
    int laps = fbdo.intData();

    Serial.print("Selected laps: ");
    Serial.println(laps);

    return laps;
  } else {
    Serial.print("Failed to read /lap_selection: ");
    Serial.println(fbdo.errorReason());
    return 0;
  }
}

void startNewRaceLocalState() {
  localLapCount = 0;
  selectedLapsLocal = readSelectedLapsFromFirebase();
  raceStartMillis = millis();
  lastValidLapMillis = 0;

  portENTER_CRITICAL(&lapMux);
  pendingLapEvents = 0;
  lastLapSeqReceived = 0;
  portEXIT_CRITICAL(&lapMux);

  readPlayerNameFromFirebase();

  Serial.println();
  Serial.println("=================================");
  Serial.println("NEW RACE LOCAL STATE RESET");
  Serial.print("Player: ");
  Serial.println(playerName);
  Serial.print("Selected laps: ");
  Serial.println(selectedLapsLocal);
  Serial.println("=================================");
  Serial.println();
}

// =========================
// Firebase / WiFi setup
// =========================
void connectWiFiAndFirebase() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to WiFi");

  unsigned long startAttempt = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 20000) {
    Serial.print(".");
    delay(500);
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected.");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("WiFi Channel: ");
    Serial.println(WiFi.channel());
    Serial.println("IMPORTANT: Put this same channel number in car code ESPNOW_CHANNEL.");
  } else {
    Serial.println("WARNING: WiFi not connected. Firebase will not work until WiFi connects.");
    return;
  }

  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;

  Firebase.reconnectWiFi(true);

  if (Firebase.signUp(&config, &auth, "", "")) {
    firebaseSignupOK = true;
    Serial.println("Firebase anonymous sign-up OK.");
  } else {
    firebaseSignupOK = false;
    Serial.print("Firebase sign-up failed: ");
    Serial.println(config.signer.signupError.message.c_str());
  }

  Firebase.begin(&config, &auth);
}

// =========================
// Battery check before race start
// =========================
bool areBothCarsBatteryOK() {
  if (!firebaseSignupOK || !Firebase.ready()) {
    Firebase.RTDB.setString(&fbdo, RACE_BLOCK_REASON_PATH, "Firebase not ready");
    return false;
  }

  int car1Battery = -1;
  int car2Battery = -1;

  if (Firebase.RTDB.getInt(&fbdo, "/car1/battery")) {
    car1Battery = fbdo.intData();
  } else {
    Firebase.RTDB.setString(&fbdo, RACE_BLOCK_REASON_PATH, "Car 1 battery data missing");
    return false;
  }

  if (Firebase.RTDB.getInt(&fbdo, "/car2/battery")) {
    car2Battery = fbdo.intData();
  } else {
    Firebase.RTDB.setString(&fbdo, RACE_BLOCK_REASON_PATH, "Car 2 battery data missing");
    return false;
  }

  Serial.print("Start battery check | car1=");
  Serial.print(car1Battery);
  Serial.print("% car2=");
  Serial.print(car2Battery);
  Serial.println("%");

  if (car1Battery < MIN_START_BATTERY_PERCENT) {
    Firebase.RTDB.setString(&fbdo, RACE_BLOCK_REASON_PATH, "Car 1 battery below 35%");
    return false;
  }

  if (car2Battery < MIN_START_BATTERY_PERCENT) {
    Firebase.RTDB.setString(&fbdo, RACE_BLOCK_REASON_PATH, "Car 2 battery below 35%");
    return false;
  }

  Firebase.RTDB.setString(&fbdo, RACE_BLOCK_REASON_PATH, "");
  return true;
}

// =========================
// Upload own car status to Firebase
// =========================
void updateOwnCarStatusToFirebase() {
  if (millis() - lastCarStatusUpdate < CAR_STATUS_UPDATE_INTERVAL) {
    return;
  }

  lastCarStatusUpdate = millis();

  if (lastTelemetryReceivedMillis == 0) {
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
    return;
  }

  if (!firebaseSignupOK || !Firebase.ready()) {
    return;
  }

  String carPath = getCarPath();

  int batteryLocal = percent;
  float tempLocal = temperature;
  float speedLocal = vSpeed;

  if (isnan(tempLocal)) {
    tempLocal = -127.0;
  }

  Firebase.RTDB.setInt(&fbdo, (carPath + "/battery").c_str(), batteryLocal);
  Firebase.RTDB.setFloat(&fbdo, (carPath + "/temperature").c_str(), tempLocal);
  Firebase.RTDB.setFloat(&fbdo, (carPath + "/speed").c_str(), speedLocal);
  Firebase.RTDB.setInt(&fbdo, (carPath + "/last_update").c_str(), millis());

  Serial.print("Updated ");
  Serial.print(carPath);
  Serial.print(" | Battery=");
  Serial.print(batteryLocal);
  Serial.print("% | Temp=");
  Serial.print(tempLocal, 1);
  Serial.print("C | Speed=");
  Serial.println(speedLocal, 3);
}

// =========================
// Race status Firebase read
// =========================
void updateRaceStatusFromFirebase() {
  if (millis() - lastRaceStatusRead < RACE_STATUS_READ_INTERVAL) {
    return;
  }

  lastRaceStatusRead = millis();

  if (WiFi.status() != WL_CONNECTED) {
    raceStarted = false;
    raceStatus = "wifi_disconnected";
    WiFi.reconnect();

    Serial.println("Race status check failed: WiFi disconnected. Car forced to STOP.");
    return;
  }

  if (!firebaseSignupOK || !Firebase.ready()) {
    raceStarted = false;
    raceStatus = "firebase_not_ready";

    Serial.println("Race status check failed: Firebase not ready. Car forced to STOP.");
    return;
  }

  if (Firebase.RTDB.getString(&fbdo, RACE_STATUS_PATH)) {
    raceStatus = fbdo.stringData();
    raceStatus.trim();

    bool oldRaceStarted = raceStarted;
    bool firebaseSaysStarted = raceStatus.equalsIgnoreCase("started");

    if (firebaseSaysStarted) {
      // Battery check only when moving from not-started to started.
      if (!oldRaceStarted) {
        if (areBothCarsBatteryOK()) {
          raceStarted = true;
          Serial.println("Race allowed: both car batteries OK.");
          startNewRaceLocalState();
        } else {
          raceStarted = false;
          raceStatus = "pending";

          Serial.println("Race blocked: one or both car batteries are below 35%.");
          Firebase.RTDB.setString(&fbdo, RACE_STATUS_PATH, "pending");
        }
      } else {
        raceStarted = true;
      }
    } else {
      raceStarted = false;
    }

    if (oldRaceStarted != raceStarted) {
      Serial.print("raceStarted changed: ");
      Serial.println(raceStarted ? "YES" : "NO");
    }
  } else {
    raceStarted = false;
    raceStatus = "read_failed";

    Serial.print("Failed to read /race_status: ");
    Serial.println(fbdo.errorReason());
    Serial.println("Car forced to STOP.");
  }
}

// =========================
// ESP-NOW setup
// =========================
void setupEspNow() {
  Serial.println("\n=== ESP-NOW Setup ===");

  if (esp_now_init() != ESP_OK) {
    Serial.println("esp_now_init failed");
    while (true) delay(1000);
  }

  esp_err_t rc;

  rc = esp_now_register_send_cb(OnDataSent);

  if (rc != ESP_OK) {
    Serial.print("Warning: esp_now_register_send_cb returned ");
    Serial.println(rc);
  }

  rc = esp_now_register_recv_cb(OnDataRecv);

  if (rc != ESP_OK) {
    Serial.print("Error: esp_now_register_recv_cb returned ");
    Serial.println(rc);
  }

  int channel = 0;

  if (WiFi.status() == WL_CONNECTED) {
    channel = WiFi.channel();
  }

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, receiverMAC, 6);

#ifdef ESP_IF_WIFI_STA
  peerInfo.ifidx = ESP_IF_WIFI_STA;
#endif

  peerInfo.channel = channel;
  peerInfo.encrypt = false;

  esp_err_t pr = esp_now_add_peer(&peerInfo);

  if (pr == ESP_OK) {
    Serial.println("ESP-NOW peer added.");
  } else if (pr == ESP_ERR_ESPNOW_EXIST) {
    Serial.println("ESP-NOW peer already exists.");
  } else {
    Serial.print("esp_now_add_peer returned: ");
    Serial.println(pr);
  }
}

// =========================
// ESP-NOW callbacks
// =========================
void OnDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  char macStr[18] = "unknown";

  if (info && info->des_addr) {
    snprintf(macStr, sizeof(macStr),
             "%02X:%02X:%02X:%02X:%02X:%02X",
             info->des_addr[0], info->des_addr[1], info->des_addr[2],
             info->des_addr[3], info->des_addr[4], info->des_addr[5]);
  }

  static unsigned long lastPrint = 0;

  if (millis() - lastPrint >= 1000) {
    lastPrint = millis();

    Serial.print("Sent to ");
    Serial.print(macStr);
    Serial.print(" -> ");
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Success" : "Fail");
  }
}

void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (info == nullptr || data == nullptr) return;

  char macStr[18];

  snprintf(macStr, sizeof(macStr),
           "%02X:%02X:%02X:%02X:%02X:%02X",
           info->src_addr[0], info->src_addr[1], info->src_addr[2],
           info->src_addr[3], info->src_addr[4], info->src_addr[5]);

  if (len == sizeof(telemetry)) {
    telemetry t;
    memcpy(&t, data, sizeof(t));

    vSpeed = t.v_ms;
    percent = t.percentage;
    temperature = t.temperature;
    lastTelemetryReceivedMillis = millis();

    Serial.print("Telemetry from ");
    Serial.print(macStr);
    Serial.print(" | seq=");
    Serial.print(t.seq);
    Serial.print(" | v_ms=");
    Serial.print(t.v_ms, 4);
    Serial.print(" | battery=");
    Serial.print(t.percentage);
    Serial.print("% | temp=");
    Serial.print(t.temperature, 1);
    Serial.print("C");

    if (t.collisionHit) {
      Serial.print(" | COLLISION source=");
      Serial.print(t.collisionSource);
      Serial.print(" collisionSeq=");
      Serial.print(t.collisionSeq);

      queuePenaltyEvent(t.collisionSource, t.collisionSeq);
    }

    if (t.lapHit) {
      Serial.print(" | LAP lapSeq=");
      Serial.print(t.lapSeq);

      queueLapEvent(t.lapSeq);
    }

    Serial.println();
    return;
  }

  if (len == sizeof(message)) {
    message tmp;
    memcpy(&tmp, data, sizeof(tmp));
    bldcInfo = tmp;

    Serial.print("Control message from ");
    Serial.print(macStr);
    Serial.print(" | v_raw1: ");
    Serial.print(tmp.value1);
    Serial.print(" | v_raw2: ");
    Serial.print(tmp.value2);
    Serial.print(" | dir: ");
    Serial.print(tmp.direction);
    Serial.print(" | angle: ");
    Serial.println(tmp.angle, 3);
    return;
  }

  Serial.print("Unknown ESP-NOW payload from ");
  Serial.print(macStr);
  Serial.print(" len=");
  Serial.println(len);
}

// =========================
// Penalty logic
// =========================
void queuePenaltyEvent(uint8_t source, uint32_t collisionSeq) {
  if (collisionSeq == 0) return;

  portENTER_CRITICAL(&penaltyMux);

  if (collisionSeq != lastCollisionSeqReceived) {
    lastCollisionSeqReceived = collisionSeq;
    pendingPenaltyEvents++;
    pendingPenaltySource = source;
  }

  portEXIT_CRITICAL(&penaltyMux);
}

void processPendingPenaltyEvents() {
  int eventsToProcess = 0;
  uint8_t source = 0;

  portENTER_CRITICAL(&penaltyMux);
  eventsToProcess = pendingPenaltyEvents;
  source = pendingPenaltySource;
  pendingPenaltyEvents = 0;
  portEXIT_CRITICAL(&penaltyMux);

  if (eventsToProcess <= 0) return;

  for (int i = 0; i < eventsToProcess; i++) {
    bool ok = incrementPlayerPenalty(source);

    if (!ok) {
      portENTER_CRITICAL(&penaltyMux);
      pendingPenaltyEvents += (eventsToProcess - i);
      pendingPenaltySource = source;
      portEXIT_CRITICAL(&penaltyMux);
      break;
    }
  }
}

bool incrementPlayerPenalty(uint8_t source) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Firebase update skipped: WiFi disconnected.");
    WiFi.reconnect();
    return false;
  }

  if (!firebaseSignupOK || !Firebase.ready()) {
    Serial.println("Firebase not ready yet.");
    return false;
  }

  int currentPenalty = 0;

  Serial.print("Reading ");
  Serial.println(playerPenaltyPath);

  if (Firebase.RTDB.getInt(&fbdo, playerPenaltyPath.c_str())) {
    currentPenalty = fbdo.intData();
  } else {
    Serial.print("Failed to read penalty: ");
    Serial.println(fbdo.errorReason());
    return false;
  }

  int newPenalty = currentPenalty + 10;

  if (newPenalty > 100) {
    newPenalty = 100;
  }

  Serial.print("Collision source=");
  Serial.print(source);
  Serial.print(" | Updating ");
  Serial.print(playerPenaltyPath);
  Serial.print(": ");
  Serial.print(currentPenalty);
  Serial.print(" -> ");
  Serial.println(newPenalty);

  if (Firebase.RTDB.setInt(&fbdo, playerPenaltyPath.c_str(), newPenalty)) {
    playerPenaltyLocal = newPenalty;
    Serial.println("Penalty updated successfully.");
    return true;
  } else {
    Serial.print("Failed to update penalty: ");
    Serial.println(fbdo.errorReason());
    return false;
  }
}

// =========================
// Lap logic
// =========================
void queueLapEvent(uint32_t lapSeq) {
  if (lapSeq == 0) return;

  portENTER_CRITICAL(&lapMux);

  if (lapSeq != lastLapSeqReceived) {
    lastLapSeqReceived = lapSeq;
    pendingLapEvents++;
  }

  portEXIT_CRITICAL(&lapMux);
}

void processPendingLapEvents() {
  int eventsToProcess = 0;

  portENTER_CRITICAL(&lapMux);
  eventsToProcess = pendingLapEvents;
  pendingLapEvents = 0;
  portEXIT_CRITICAL(&lapMux);

  if (eventsToProcess <= 0) return;

  for (int i = 0; i < eventsToProcess; i++) {
    bool ok = handleLapDetected();

    if (!ok) {
      portENTER_CRITICAL(&lapMux);
      pendingLapEvents += (eventsToProcess - i);
      portEXIT_CRITICAL(&lapMux);
      break;
    }
  }
}

bool handleLapDetected() {
  if (!raceStarted) {
    Serial.println("Lap ignored: race not started.");
    return true;
  }

  if (playerName == "") {
    readPlayerNameFromFirebase();

    if (playerName == "") {
      Serial.println("Lap processing failed: player name not loaded.");
      return false;
    }
  }

  unsigned long now = millis();

  if (localLapCount == 0) {
    if (raceStartMillis > 0 && now - raceStartMillis < MIN_VALID_LAP_TIME_MS) {
      Serial.println("Lap ignored: too soon after race start.");
      return true;
    }
  } else {
    if (now - lastValidLapMillis < MIN_VALID_LAP_TIME_MS) {
      Serial.println("Lap ignored: too soon after previous lap.");
      return true;
    }
  }

  int selectedLaps = readSelectedLapsFromFirebase();

  if (selectedLaps <= 0) {
    Serial.println("Lap processing failed: invalid /lap_selection.");
    return false;
  }

  selectedLapsLocal = selectedLaps;

  int newLap = localLapCount + 1;

  if (newLap > selectedLaps) {
    Serial.println("Lap ignored: selected lap count already completed.");
    return true;
  }

  localLapCount = newLap;
  lastValidLapMillis = now;

  int arrayIndex = newLap - 1;
  unsigned long lapTime = now - raceStartMillis;

  String lapTimePath = "/lap_times/" + playerName + "/" + String(arrayIndex);

  if (Firebase.RTDB.setInt(&fbdo, lapTimePath.c_str(), lapTime)) {
    Serial.print("Lap time saved: ");
    Serial.print(lapTimePath);
    Serial.print(" = ");
    Serial.println(lapTime);
  } else {
    Serial.print("Failed to save lap time: ");
    Serial.println(fbdo.errorReason());
    return false;
  }

  String winnerPath = "/lap" + String(newLap) + "winner";
  String currentWinner = "";

  if (Firebase.RTDB.getString(&fbdo, winnerPath.c_str())) {
    currentWinner = fbdo.stringData();
    currentWinner.trim();
  } else {
    Serial.print("Failed to read ");
    Serial.print(winnerPath);
    Serial.print(": ");
    Serial.println(fbdo.errorReason());
    return false;
  }

  if (currentWinner == "") {
    if (Firebase.RTDB.setString(&fbdo, winnerPath.c_str(), playerName)) {
      Serial.print("Lap ");
      Serial.print(newLap);
      Serial.print(" winner set: ");
      Serial.println(playerName);
    } else {
      Serial.print("Failed to set lap winner: ");
      Serial.println(fbdo.errorReason());
      return false;
    }

    if (newLap == selectedLaps) {
      String currentFinalWinner = "";

      if (Firebase.RTDB.getString(&fbdo, FINAL_WINNER_PATH)) {
        currentFinalWinner = fbdo.stringData();
        currentFinalWinner.trim();
      }

      if (currentFinalWinner == "") {
        Firebase.RTDB.setString(&fbdo, FINAL_WINNER_PATH, playerName);
        Firebase.RTDB.setString(&fbdo, RACE_STATUS_PATH, "finished");

        raceStarted = false;
        raceStatus = "finished";

        Serial.println();
        Serial.println("=================================");
        Serial.print("FINAL WINNER: ");
        Serial.println(playerName);
        Serial.println("Race status set to finished.");
        Serial.println("=================================");
        Serial.println();
      }
    }
  } else {
    Serial.print("Lap ");
    Serial.print(newLap);
    Serial.print(" winner already set: ");
    Serial.println(currentWinner);
  }

  return true;
}

// =========================
// Setup
// =========================
void setup() {
  Serial.begin(115200);
  delay(1000);

  configurePlayerPaths();

  pinMode(TRIG1, OUTPUT);
  pinMode(ECHO1, INPUT);
  digitalWrite(TRIG1, LOW);

  pinMode(TRIG2, OUTPUT);
  pinMode(ECHO2, INPUT);
  digitalWrite(TRIG2, LOW);

  delay(50);
  Serial.println("ESP32 Controller Started");

  connectWiFiAndFirebase();
  setupEspNow();

  pinMode(FORWARD_PIN, INPUT_PULLDOWN);

  pinMode(PIN_A, INPUT_PULLUP);
  pinMode(PIN_B, INPUT_PULLUP);

  if (PIN_Z >= 0) {
    pinMode(PIN_Z, INPUT_PULLUP);
  }

  int MSB = digitalRead(PIN_A);
  int LSB = digitalRead(PIN_B);
  lastEncoded = (MSB << 1) | LSB;

  attachInterrupt(digitalPinToInterrupt(PIN_A), handleAB, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_B), handleAB, CHANGE);

  if (PIN_Z >= 0) {
    attachInterrupt(digitalPinToInterrupt(PIN_Z), handleZ, RISING);
    Serial.println("Z index enabled.");
  } else {
    Serial.println("No Z index pin configured.");
  }

  Serial.println("Encoder ready.");
  Serial.print("Counts per revolution: ");
  Serial.println(COUNTS_PER_REV);
  Serial.print("Degrees per count: ");
  Serial.println(360.0 / COUNTS_PER_REV, 6);

  Wire.begin(SDA_PIN, SCL_PIN);

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;);
  }

  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 20);
  display.println("Controller");
  display.display();

  if (firebaseSignupOK && Firebase.ready()) {
    readPlayerNameFromFirebase();

    if (Firebase.RTDB.getInt(&fbdo, playerPenaltyPath.c_str())) {
      playerPenaltyLocal = fbdo.intData();

      Serial.print("Starting penalty: ");
      Serial.println(playerPenaltyLocal);
    }

    if (Firebase.RTDB.getString(&fbdo, RACE_STATUS_PATH)) {
      raceStatus = fbdo.stringData();
      raceStatus.trim();
      raceStarted = raceStatus.equalsIgnoreCase("started");

      Serial.print("Starting race status: ");
      Serial.println(raceStatus);

      if (raceStarted) {
        if (areBothCarsBatteryOK()) {
          startNewRaceLocalState();
        } else {
          raceStarted = false;
          raceStatus = "pending";
          Firebase.RTDB.setString(&fbdo, RACE_STATUS_PATH, "pending");
        }
      }
    } else {
      raceStarted = false;
      raceStatus = "read_failed";
      Serial.println("Starting race status read failed. Car forced to STOP.");
    }
  }
}

// =========================
// OLED helper
// =========================
void drawBatteryIcon(int level, int x, int y) {
  display.drawRect(x, y, 18, 10, SSD1306_WHITE);
  display.fillRect(x + 18, y + 3, 2, 4, SSD1306_WHITE);

  if (level < 0) level = 0;
  if (level > 100) level = 100;

  int fillWidth = map(level, 0, 100, 0, 16);
  display.fillRect(x + 1, y + 1, fillWidth, 8, SSD1306_WHITE);

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(x - 25, y + 1);
  display.print(level);
  display.print("%");
}

// =========================
// Ultrasonic helper
// =========================
float measureDistanceCm(int TRIG_PIN, int ECHO_PIN) {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  unsigned long duration = pulseIn(ECHO_PIN, HIGH, MAX_ECHO_TIME);

  if (duration == 0) {
    return -1.0;
  }

  float distance_cm = (duration * 0.0343f) / 2.0f;
  return distance_cm;
}

// =========================
// Loop
// =========================
void loop() {
  updateRaceStatusFromFirebase();

  processPendingPenaltyEvents();
  processPendingLapEvents();

  // This updates /car1 or /car2 for home page battery display.
  updateOwnCarStatusToFirebase();

  bool forwardState = digitalRead(FORWARD_PIN);
  int direction = forwardState == HIGH ? 1 : 0;

  long count = safeReadEncoder();
  long center = safeReadCenter();

  float angle = (float)(count - center) * 360.0f / (float)COUNTS_PER_REV;

  static unsigned long lastPrint = 0;

  if (millis() - lastPrint >= 100) {
    lastPrint = millis();

    Serial.print("Angle: ");
    Serial.print(angle, 3);
    Serial.print(" deg | count: ");
    Serial.print(count);
    Serial.print(" center: ");
    Serial.print(center);
    Serial.print(" | race_status: ");
    Serial.print(raceStatus);
    Serial.print(" | raceStarted: ");
    Serial.print(raceStarted ? "YES" : "NO");
    Serial.print(" | player: ");
    Serial.print(playerName);
    Serial.print(" | lap: ");
    Serial.print(localLapCount);

    if (zTriggered) {
      Serial.print(" [Z triggered]");

      portENTER_CRITICAL(&mux);
      zTriggered = false;
      portEXIT_CRITICAL(&mux);
    }

    Serial.println();
  }

  if (Serial.available()) {
    char ch = Serial.read();

    if (ch == 'c') {
      portENTER_CRITICAL(&mux);
      centerCount = encoderCount;
      portEXIT_CRITICAL(&mux);

      Serial.println("Center calibrated to current encoder count.");
    } else if (ch == 'r') {
      portENTER_CRITICAL(&mux);
      encoderCount = 0;
      centerCount = 0;
      portEXIT_CRITICAL(&mux);

      Serial.println("Counts reset.");
    }
  }

  float dist1 = measureDistanceCm(TRIG1, ECHO1);
  float dist2 = measureDistanceCm(TRIG2, ECHO2);

  int rawValue = 0;
  int rawValue2 = 0;

  if (dist1 > 0) {
    rawValue = (int)(DISTANCE - dist1);
  }

  if (dist2 > 0) {
    rawValue2 = (int)(DISTANCE - dist2);
  }

  rawValue = constrain(rawValue, 0, RAW_MAX_CONTROL);
  rawValue2 = constrain(rawValue2, 0, RAW_MAX_CONTROL);

  // ====================================================
  // Main race-status control
  // ====================================================
  if (raceStarted) {
    bldcInfo.value1 = rawValue;
    bldcInfo.value2 = rawValue2;
  } else {
    // Stop car if pending, finished, Firebase error, WiFi error, or battery block.
    bldcInfo.value1 = 0;
    bldcInfo.value2 = 1;
  }

  bldcInfo.direction = direction;
  bldcInfo.angle = angle;

  esp_err_t result = esp_now_send(receiverMAC, (uint8_t *)&bldcInfo, sizeof(bldcInfo));

  if (result != ESP_OK) {
    Serial.print("esp_now_send error: ");
    Serial.println(result);
  }

  float speedLocal = vSpeed;
  int percentLocal = percent;
  float tempLocal = temperature;

  if (isnan(tempLocal)) {
    tempLocal = -127.0;
  }

  display.clearDisplay();

  drawBatteryIcon(percentLocal, 108, 0);

  display.drawRect(0, 0, 6, 20, SSD1306_WHITE);
  display.fillRect(2, 16, 2, 3, SSD1306_WHITE);
  display.fillCircle(3, 22, 3, SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(12, 6);
  display.print(tempLocal, 1);
  display.print(" ");
  display.cp437(true);
  display.write(248);
  display.print("C");

  display.setTextSize(1);
  display.setCursor(0, 24);

  if (raceStarted) {
    display.print("Race: STARTED");
  } else {
    display.print("Race: STOPPED");
  }

  display.setTextSize(3);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(20, 35);

  if (speedLocal < 0) {
    display.println(-speedLocal, 3);
  } else {
    display.println(speedLocal, 3);
  }

  display.setTextSize(1);
  display.setCursor(110, 40);
  display.print("m/s");

  display.setTextSize(1);
  display.setCursor(0, 56);
  display.print("P");
  display.print(PLAYER_NUMBER);
  display.print(":");
  display.print(playerPenaltyLocal);
  display.print(" L:");
  display.print(localLapCount);
  display.print("/");
  display.print(selectedLapsLocal);

  display.display();

  Serial.print("Raw ADC: ");
  Serial.println(rawValue);

  Serial.print("Raw ADC2: ");
  Serial.println(rawValue2);

  Serial.print("Direction: ");
  Serial.println(direction);

  Serial.print("Angle: ");
  Serial.println(angle, 3);

  Serial.print("Sending throttle value1: ");
  Serial.println(bldcInfo.value1);

  Serial.print("Sending brake value2: ");
  Serial.println(bldcInfo.value2);

  delay(100);
}