# 🏎️ IoT Racing Battle — By ByteForge

> **Real-time IoT-powered two-player RC car racing with live telemetry, collision detection, lap tracking, and a full-stack web dashboard.**

---

## 📖 Table of Contents

- [Overview](#-overview)
- [System Architecture](#-system-architecture)
- [Project Structure](#-project-structure)
- [Hardware Components](#-hardware-components)
- [Firmware — Car](#-firmware--car-sketchjun11aino)
- [Firmware — Controller](#-firmware--controller-sketchjun11bino)
- [Race Track Arena](#-race-track-arena)
- [Web Application](#-web-application)
- [Firebase Schema](#-firebase-database-schema)
- [Communication Protocol](#-communication-protocol)
- [Race Flow](#-race-flow)
- [Setup & Configuration](#-setup--configuration)
- [Libraries & Dependencies](#-libraries--dependencies)

---

## 🌐 Overview

**IoT Racing Battle** is a fully connected, two-player RC car racing system that combines embedded hardware, wireless communication, cloud data sync, and a modern web dashboard into a single cohesive project.

| Feature | Description |
|---|---|
| 🚗 Two RC Cars | Each car runs an ESP32 with real-time sensors |
| 🎮 Two Controllers | Handheld gesture-based controllers using ultrasonic + rotary encoder |
| 🏟️ Physical Arena | Race track with 3 selectable paths + 4 MG995 servo-controlled barriers |
| 📡 ESP-NOW | Sub-millisecond wireless control between each controller and its car |
| ☁️ Firebase | Cloud game server for race state, lap times, penalties, and winner |
| 🌐 Web Dashboard | React + Vite app for race management, leaderboard, and player profiles |
| 🏁 Lap Detection | 5-channel TCRT5000 IR sensor array on each car |
| 💥 Collision Detection | KW12 bumper switch + MPU6050 accelerometer, dual-source |
| 🔋 Battery Monitoring | INA219 current sensor with race-start gate (≥35% required) |
| 🌡️ Temperature Monitoring | DS18B20 non-blocking temperature sensor |

---

## 🏗️ System Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                     Firebase (Cloud)                                 │
│                                                                      │
│  RTDB: /race_status  /car1  /car2  /lap_times  /finalwinner         │
│  Firestore: users{}  matches{}                                       │
└──────────────┬───────────────────────────┬──────────────────────────┘
               │ WiFi (Firebase)            │ WiFi (Firebase)
    ┌──────────▼──────────┐     ┌──────────▼──────────┐
    │   Controller P1     │     │   Controller P2     │
    │   ESP32 + OLED      │     │   ESP32 + OLED      │
    │   Ultrasonic ×2     │     │   Ultrasonic ×2     │
    │   Rotary Encoder    │     │   Rotary Encoder    │
    └──────────┬──────────┘     └──────────┬──────────┘
               │ ESP-NOW                    │ ESP-NOW
    ┌──────────▼──────────┐     ┌──────────▼──────────┐
    │       Car 1         │     │       Car 2         │
    │   ESP32             │     │   ESP32             │
    │   ESC + Servo       │     │   ESC + Servo       │
    │   IR Lap Sensor ×5  │     │   IR Lap Sensor ×5  │
    │   INA219 Battery    │     │   INA219 Battery    │
    │   MPU6050 Accel     │     │   MPU6050 Accel     │
    │   DS18B20 Temp      │     │   DS18B20 Temp      │
    │   KW12 Bumper       │     │   KW12 Bumper       │
    └─────────────────────┘     └─────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│                  Physical Arena (Race Track)                 │
│                                                              │
│  Arena Node 1 (ESP32) ── Servo 1 (path gate) ─ WiFi ──┐    │
│                        ── Servo 2 (path gate)           │    │
│                        ── Servo 3 (barrier)             │    │
│                                                         │    │
│  Arena Node 2 (ESP32) ── Servo 1 (path gate) ─ WiFi ──┤    │
│                        ── Servo 2 (path gate)           │Firebase│
│                        ── Servo 3 (barrier 1)           │    │
│                        ── Servo 4 (barrier 2)           │    │
│                                                         │    │
│  Arena Node 3 (ESP32) ── Servo 1 (path gate) ─ WiFi ──┤    │
│                        ── Servo 2 (path gate)           │    │
│                        ── Servo 3 (barrier)             │    │
│                                                         │    │
│  Arena Node 4 (ESP32) ── Servo 1 (path gate) ─ WiFi ──┘    │
│                        ── Servo 2 (path gate)                │
└─────────────────────────────────────────────────────────────┘

              ┌──────────────────────────┐
              │   Web Dashboard (React)  │
              │   Deployed on Vercel     │
              └──────────────────────────┘
```

---

## 📁 Project Structure

```
IoT Based Racing Battle by ByteForge/
│
├── Car/
│   └── sketch_jun11a/
│       └── sketch_jun11a.ino         # Car firmware (common for Car 1 & Car 2)
│
├── Controller/
│   └── sketch_jun11b/
│       └── sketch_jun11b.ino         # Controller firmware (common for both)
│
├── Race Track/
│   ├── 1/
│   │   └── sketch_jun1b/
│   │       └── sketch_jun1b.ino      # Arena Node 1: 3 servos (2 path gates + 1 barrier)
│   ├── 2/
│   │   └── sketch_jun1a/
│   │       └── sketch_jun1a.ino      # Arena Node 2: 4 servos (2 path gates + 2 barriers)
│   ├── 3/
│   │   └── sketch_may31a/
│   │       └── sketch_may31a.ino     # Arena Node 3: 3 servos (2 path gates + 1 barrier)
│   └── 4/
│       └── sketch_jun2a/
│           └── sketch_jun2a.ino      # Arena Node 4: 2 servos (2 path gates only)
│
├── Web Application/                  # 🔗 Git Submodule → [IoT-Racing-Battle---ByteForge](https://github.com/thamod-03/IoT-Racing-Battle---ByteForge)
│   ├── src/
│   │   ├── pages/                    # 9 route pages
│   │   ├── components/               # 10 reusable components
│   │   ├── services/                 # Firebase service helpers
│   │   ├── firebaseConfig.js         # Firebase init (Firestore + RTDB)
│   │   └── App.jsx                   # Router + shared state
│   ├── package.json
│   ├── vite.config.js
│   └── .env                          # Firebase keys (not committed)
│
└── README.md                         # This file
```

> **Note:** The `Car` and `Controller` directories each contain a **single common sketch**. The same code is flashed to both Car 1 and Car 2 (or both controllers), with only a few constants changed per device.

---

## 🔧 Hardware Components

### Per Car (× 2)
| Component | Model | Purpose |
|---|---|---|
| Microcontroller | ESP32 | Main processor + ESP-NOW + WiFi |
| ESC | Standard BLDC ESC | Motor speed controller |
| Steering Servo | Standard Servo | Front wheel steering |
| IR Lap Sensor | TCRT5000 × 5 | Black finish line detection |
| Battery Monitor | Adafruit INA219 | Voltage/current → battery % |
| Accelerometer | MPU6050 | Impact detection (collision) |
| Bumper Switch | KW12 (NO) | Physical collision detection |
| Temperature Sensor | DS18B20 | Motor/ESC thermal monitoring |
| Brake LEDs | LED × 4 | Visual brake indicator |

### Per Controller (× 2)
| Component | Model | Purpose |
|---|---|---|
| Microcontroller | ESP32 | Main processor + ESP-NOW + WiFi |
| Throttle Sensor | HC-SR04 Ultrasonic #1 | Hand proximity → throttle (0–7) |
| Brake Sensor | HC-SR04 Ultrasonic #2 | Hand proximity → brake |
| Steering Input | Rotary Encoder (250 PPR) | Steering wheel angle |
| Direction Switch | Pushbutton/Toggle | Forward / Reverse selection |
| Display | SSD1306 OLED 128×64 | Speed, battery, lap, penalty HUD |

---

## 🚗 Firmware — Car (`sketch_jun11a.ino`)

### Pin Configuration
| Signal | GPIO | Component |
|---|---|---|
| ESC PWM | 18 | Brushless ESC |
| Servo PWM | 19 | Steering servo |
| Brake LED 1–4 | 12, 13, 26, 27 | Brake indicator LEDs |
| I2C SDA | 17 | INA219 + MPU6050 |
| I2C SCL | 16 | INA219 + MPU6050 |
| OneWire | 4 | DS18B20 temperature |
| KW12 Bumper | 25 | Collision switch (NO) |
| MPU INT | 5 | Accelerometer interrupt |
| IR Sensor 1–5 | 32, 33, 34, 35, 23 | TCRT5000 lap line |

### Key Constants to Configure Per Car
```cpp
// In sketch_jun11a.ino
uint8_t peerMAC[6] = { 0x00, ... };   // Set to THIS car's paired Controller MAC
const uint8_t ESPNOW_CHANNEL = 6;     // Must match the controller's WiFi channel
```

### How It Works
1. **Receives** `message` struct from its paired controller via ESP-NOW every ~100ms
2. **Steering**: Maps `angle / 3.0` → servo position (0°–180°)
3. **Throttle**: Maps raw value (0–7) → ESC pulse width (1530–2000µs forward / 1530–1000µs reverse)
4. **Braking**: `value2 > 0` → ESC to neutral instantly + brake LEDs ON
5. **Lap Detection**: 5× IR sensors; rising edge (white → black) triggers lap event
6. **Collision**: KW12 debounced + MPU6050 sampled every 20ms (threshold: 12 m/s²)
7. **Telemetry**: Sends `telemetry` struct every **50ms** → speed (m/s), battery%, temperature, collision & lap events
8. **Event Reliability**: Collision and lap events are repeated **3×** with unique sequence numbers; controller deduplicates

### ESC Pulse Width Reference
| State | Pulse Width |
|---|---|
| Neutral | 1530 µs |
| Full Forward | 2000 µs |
| Full Reverse | 1000 µs |

### Battery Mapping
```
3S LiPo: 11.1V (full) → 9.9V (empty)
Voltage → 0–100% SOC (linear mapping via INA219 bus voltage)
```

---

## 🎮 Firmware — Controller (`sketch_jun11b.ino`)

### Pin Configuration
| Signal | GPIO | Component |
|---|---|---|
| Ultrasonic #1 TRIG | 4 | Throttle sensor |
| Ultrasonic #1 ECHO | 17 | Throttle sensor |
| Ultrasonic #2 TRIG | 14 | Brake sensor |
| Ultrasonic #2 ECHO | 27 | Brake sensor |
| Encoder A | 18 | Rotary encoder (quadrature) |
| Encoder B | 19 | Rotary encoder (quadrature) |
| Encoder Z | 21 | Rotary encoder index (center ref) |
| Forward Switch | 25 | Forward/Reverse direction |
| OLED SDA | 32 | SSD1306 display |
| OLED SCL | 33 | SSD1306 display |

### Key Constants to Configure Per Controller
```cpp
// In sketch_jun11b.ino
const int PLAYER_NUMBER = 1;           // Set to 1 for Player 1, 2 for Player 2
uint8_t receiverMAC[6] = { 0x00, ... }; // Set to the paired Car's MAC address

// WiFi credentials (for Firebase)
#define WIFI_SSID     "your_wifi"
#define WIFI_PASSWORD "your_password"
#define API_KEY       "firebase_api_key"
#define DATABASE_URL  "https://your-project.firebaseio.com"
```

### Control Mapping
| Input | Sensor | Range | Effect |
|---|---|---|---|
| Throttle | Ultrasonic #1 (distance) | 0–11cm hand gap → raw 0–7 | Forward/reverse speed |
| Brake | Ultrasonic #2 (distance) | 0–11cm hand gap → raw 0–7 | Immediate brake |
| Steering | Rotary encoder | ±degrees from center | Servo angle |
| Direction | GPIO 25 pin | HIGH=forward, LOW=reverse | Drive direction |

### Encoder Calibration (Serial Commands)
```
'c'  →  Set current position as steering center
'r'  →  Reset encoder counts to zero
```

### OLED Display Layout
```
┌────────────────────────────┐
│ 🌡️ 28.5°C       [███░] 75% │
│ Race: STARTED              │
│                            │
│       12.340               │
│                        m/s │
│ P1:20  L:2/3              │
└────────────────────────────┘
```
*Bottom row: Player number, penalty score, current lap / total laps*

### Race Behaviour
- If `race_status = "started"` → sends live throttle/steering to car
- If race is pending, finished, or Firebase/WiFi fails → sends `value1=0, value2=1` (forced brake)

---

## 🏟️ Race Track Arena

The physical race track is a custom-built arena with **3 selectable racing paths** and **4 MG995 servo-controlled barriers**, all managed by **4 independent ESP32 nodes**. Each node connects to Firebase RTDB over WiFi, reads `path_selection` and `race_status`, and actuates its local servos accordingly — completely autonomously, with no input from controllers or cars.

### Arena Overview

| Property | Value |
|---|---|
| Number of ESP32 Nodes | 4 |
| Total Servo Motors | 12 (across all nodes) |
| Servo Model | MG995 (high-torque) |
| Path Switching | Servo angular position changes per `path_selection` (1, 2, or 3) |
| Active Barriers | Only when `race_status = "started"` |
| Barrier Motion | Continuous oscillation (PWM sweep, 500–2500µs range) |
| Firebase Poll Interval | 150–200ms |
| Path is chosen by | Web app `LapSelectionPage` (random 1–3) |

---

### Arena Node 1 — `Race Track/1/sketch_jun1b.ino`

**3 servos** — 2 path gate servos + 1 oscillating barrier

| Servo | GPIO | Role |
|---|---|---|
| Servo 1 | 18 | Path gate (angular position) |
| Servo 2 | 19 | Path gate (angular position) |
| Servo 3 | 21 | Oscillating barrier |

**Path gate positions:**
| `path_selection` | Servo 1 | Servo 2 | Barrier 3 (Servo 3) | Barrier Active? |
|---|---|---|---|---|
| 1 | 90° | 115° | Oscillates 1000–1500µs | ✅ Yes |
| 2 | 150° | 115° | Held at 1000µs (reset) | ❌ No |
| 3 | 95° | 70° | Oscillates 500–1000µs | ✅ Yes |

**Barrier motion:** Step size = 30µs per 1ms tick. Bounces between `minPulse` and `maxPulse` continuously while race is started and path is 1 or 3.

---

### Arena Node 2 — `Race Track/2/sketch_jun1a.ino`

**4 servos** — 2 path gate servos + 2 independent oscillating barriers

| Servo | GPIO | Role |
|---|---|---|
| Servo 1 | 19 | Path gate (angular position) |
| Servo 2 | 18 | Path gate (angular position) |
| Servo 3 | 21 | Oscillating barrier 1 |
| Servo 4 | 14 | Oscillating barrier 2 |

**Path gate positions:**
| `path_selection` | Servo 1 | Servo 2 |
|---|---|---|
| 1 | 90° | 65° |
| 2 | 30° | 65° |
| 3 | 95° | 110° |

**Barrier behaviour:**
| `path_selection` | Barrier 1 (Servo 3) | Barrier 2 (Servo 4) |
|---|---|---|
| 1 | ❌ Off (held at 500µs) | ✅ Oscillates 500–1500µs |
| 2 | ❌ Off (held at 500µs) | ✅ Oscillates 500–1500µs |
| 3 | ✅ Oscillates 500–1500µs | ✅ Oscillates 500–1500µs |

Both barriers are halted (held at 500µs) when `race_status ≠ "started"`.

---

### Arena Node 3 — `Race Track/3/sketch_may31a.ino`

**3 servos** — 2 path gate servos + 1 oscillating barrier

| Servo | GPIO | Role |
|---|---|---|
| Servo 1 | 18 | Path gate (angular position) |
| Servo 2 | 19 | Path gate (angular position) |
| Servo 3 | 21 | Oscillating barrier |

**Path gate positions:**
| `path_selection` | Servo 1 (GPIO 18) | Servo 2 (GPIO 19) |
|---|---|---|
| 1 | 115° | 90° |
| 2 | 115° | 150° |
| 3 | 70° | 95° |

**Barrier behaviour:**
- Active **only when `path_selection = 1`** and `race_status = "started"`
- Oscillates between 500µs and 1500µs (step 30µs per 1ms tick)

---

### Arena Node 4 — `Race Track/4/sketch_jun2a.ino`

**2 servos** — path gate servos only, no barrier

| Servo | GPIO | Role |
|---|---|---|
| Servo 1 | 18 | Path gate (angular position) |
| Servo 2 | 19 | Path gate (angular position) |

**Path gate positions:**
| `path_selection` | Servo 1 | Servo 2 |
|---|---|---|
| 1 | 90° | 65° |
| 2 | 30° | 65° |
| 3 | 95° | 110° |

> This node acts as a pure path-switcher with no active barrier. It still monitors `race_status` for future extensibility.

---

### Arena Firebase Logic (all nodes)

All 4 arena nodes share the same polling pattern:
1. Poll Firebase RTDB every **150–200ms**
2. Read `/path_selection` → only act if value has **changed** (change-detection using `lastPath`)
3. Call the matching `path1()` / `path2()` / `path3()` function → moves gate servos to the correct angle
4. Read `/race_status` → barriers oscillate **only when** `"started"`, else held at rest position

```
Firebase RTDB Change         Arena Node Response
──────────────────────────────────────────────────
path_selection = 1     →   Gate servos → Path 1 angles
path_selection = 2     →   Gate servos → Path 2 angles
path_selection = 3     →   Gate servos → Path 3 angles

race_status = "started" →  Barriers begin oscillating (node-specific rules)
race_status ≠ "started" →  Barriers halt at rest PWM position
```

### Arena Node Configuration (per device)

```cpp
// In each Race Track sketch — fill in your credentials
#define WIFI_SSID      "your_wifi_ssid"
#define WIFI_PASSWORD  "your_wifi_password"
#define FIREBASE_HOST  "your-project-default-rtdb.firebaseio.com"
#define FIREBASE_AUTH  "your_firebase_database_secret"
```

> ⚠️ Race Track nodes use the **legacy `FirebaseESP32` library** (database secret auth), while the Controller firmware uses the newer `Firebase_ESP_Client`. Both connect to the same RTDB project.

### Arena Hardware — Per Node Summary

| Node | Sketch | Path Gate Servos | Barrier Servos | Total Servos |
|---|---|---|---|---|
| Node 1 | `sketch_jun1b.ino` | 2 (GPIO 18, 19) | 1 (GPIO 21) | 3 |
| Node 2 | `sketch_jun1a.ino` | 2 (GPIO 19, 18) | 2 (GPIO 21, 14) | 4 |
| Node 3 | `sketch_may31a.ino` | 2 (GPIO 18, 19) | 1 (GPIO 21) | 3 |
| Node 4 | `sketch_jun2a.ino` | 2 (GPIO 18, 19) | None | 2 |
| **Total** | | **8** | **4** | **12** |

---

## 🌐 Web Application

> 🔗 **Separate repository:** [thamod-03/IoT-Racing-Battle---ByteForge](https://github.com/thamod-03/IoT-Racing-Battle---ByteForge)
> This directory is a **Git submodule** — it points to the standalone web app repo above.

**React 19 + Vite 8 + TailwindCSS 4 + Firebase JS SDK 12** — Deployed on Vercel.

### Pages & Routes
| Route | Page | Purpose |
|---|---|---|
| `/` | HomePage | Live car telemetry dashboard; gate to Start Battle |
| `/register` | RegisterPage | New player account creation |
| `/login` | LoginPage | Sequential 2-player login (must be 2 different users) |
| `/lap-selection` | LapSelectionPage | Select number of laps (1–5); initializes race in Firebase |
| `/race` | RacePage | Live race monitor: countdown, lap winners, penalties, final winner |
| `/players` | PlayerProfilePage | Search player stats by username |
| `/recent-matches` | RecentMatchesPage | Paginated historical match records |
| `/leaderboard` | LeaderboardPage | Best-time rankings filtered by path × laps |

### Firebase Dual-Database Design
| Service | Used For |
|---|---|
| **Realtime Database** | Live race data — `onValue()` real-time listeners for car telemetry, race status, countdown, penalties |
| **Firestore** | Persistent data — `users` (profiles + stats) and `matches` (race history) |

### Services
| File | Function | Description |
|---|---|---|
| `raceResultService.js` | `saveFinishedRace()` | Saves match to Firestore; increments player stats atomically |
| `leaderboardService.js` | `fetchLeaderboard()` | Reads matches, filters by path+laps, finds best time per player |
| `userService.js` | `fetchUserByUsername()` | Cached Firestore user lookup |

### Web App Setup
```bash
cd "Web Application"
npm install
```

Create a `.env` file:
```env
VITE_API_KEY=your_firebase_api_key
VITE_AUTH_DOMAIN=your_project.firebaseapp.com
VITE_DATABASE_URL=https://your_project-default-rtdb.firebaseio.com
VITE_PROJECT_ID=your_project_id
VITE_STORAGE_BUCKET=your_project.appspot.com
VITE_MESSAGING_SENDER_ID=your_sender_id
VITE_APP_ID=your_app_id
```

```bash
npm run dev       # Start development server
npm run build     # Build for production
```

---

## 🔥 Firebase Database Schema

### Realtime Database (RTDB)
```
/ (root)
├── race_status          "pending" | "countdown" | "started" | "finished"
├── countdown_value      3 → 2 → 1 → 0 (driven by web app RacePage)
├── lap_selection        Number (1–5, chosen at lap selection)
├── path_selection       Number (1–3, randomly assigned)
├── finalwinner          String (username of winner)
├── match_saved          Boolean (prevents duplicate Firestore saves)
├── race_block_reason    String (why race was blocked, e.g. low battery)
│
├── player1              String (username)
├── player2              String (username)
├── player1_penalty      Number (0–100, +10 per collision)
├── player2_penalty      Number (0–100, +10 per collision)
│
├── lap1winner … lap5winner   String (first player to complete each lap)
│
├── lap_times/
│   ├── {player1username}/
│   │   ├── 0            Number (Lap 1 time in ms from race start)
│   │   ├── 1            Number (Lap 2 time)
│   │   └── ...
│   └── {player2username}/
│       └── ...
│
├── car1/
│   ├── battery          Number (%)
│   ├── temperature      Number (°C)
│   ├── speed            Number (m/s)
│   └── last_update      Number (millis)
│
└── car2/
    └── (same as car1)
```

### Firestore
```
users/{docId}
├── firstName, lastName
├── username            (lowercase, used as query key)
├── password            (custom auth — stored as-is)
├── matchPlayed         Number
├── totalWin            Number
└── paths/
    ├── path1Matches, path1Wins
    ├── path2Matches, path2Wins
    └── path3Matches, path3Wins

matches/{docId}
├── player1, player2    (usernames)
├── final_winner        (username)
├── lap_count           Number
├── path                Number (1, 2, or 3)
├── lap_times           Object {username: [ms, ms, ...]}
├── lap_winners         Array
├── isPenalty           Boolean
├── player1_penalty, player2_penalty
└── created_at          Timestamp
```

---

## 📡 Communication Protocol

### Shared Structs (must match exactly on both Car and Controller)

#### `message` — Controller → Car (every ~100ms)
```cpp
typedef struct __attribute__((packed)) message {
  int   value1;     // Throttle raw (0–7)
  int   value2;     // Brake raw (>0 = brake)
  int   direction;  // 0 = forward, 1 = reverse
  float angle;      // Steering angle in degrees
} message;
```

#### `telemetry` — Car → Controller (every 50ms)
```cpp
typedef struct __attribute__((packed)) telemetry {
  uint32_t seq;             // Telemetry sequence number
  float    v_ms;            // Speed (m/s, smoothed 5-sample avg)
  int      percentage;      // Battery SOC (%)
  float    temperature;     // Temperature (°C)

  bool     collisionHit;    // Collision occurred
  uint8_t  collisionSource; // 1=KW12, 2=MPU6050, 3=Both
  uint32_t collisionSeq;    // Unique ID (dedup at controller)

  bool     lapHit;          // Lap line crossed
  uint32_t lapSeq;          // Unique ID (dedup at controller)
} telemetry;
```

---

## 🏁 Race Flow

```
1. [Web App — HomePage]
   Both cars report ≥ 35% battery to Firebase → "Start Battle" button enabled

2. [Web App — LoginPage]
   Two different players log in sequentially (Firestore auth)

3. [Web App — LapSelectionPage]
   Select laps (1–5) → path randomly assigned (1–3)
   Race config written to Firebase RTDB:
     race_status = "countdown", countdown_value = 3

4. [Hardware — Controllers]
   Detect race_status = "countdown"/"pending" → send brake signal to cars
   Both cars stop and wait

5. [Web App — RacePage]
   Countdown 3 → 2 → 1 → 0 (each second, updates RTDB countdown_value)
   Sets race_status = "started" at 0

6. [Hardware — Controllers]
   Detect race_status = "started" → send live throttle/steering to cars
   Cars drive!

7. [Hardware — Cars]
   IR sensors detect lap line crossing → sends lapHit + lapSeq in telemetry
   Collision sensors fire → sends collisionHit + collisionSource + collisionSeq

8. [Hardware — Controllers]
   Receive telemetry → write lap times + lap winners to Firebase RTDB
   Receive collision → increment penalty in Firebase (+10 per hit, max 100)

9. [Web App — RacePage]
   Monitors RTDB in real-time:
     If penalty ≥ 100 → opponent wins (penalty disqualification)
     If all laps complete → set finalwinner + race_status = "finished"

10. [Web App — RacePage]
    saveFinishedRace() called:
      → Saves match to Firestore matches collection
      → Increments player stats in Firestore users collection
      → Sets match_saved = true in RTDB

11. [Post-Race]
    Players can view results at /recent-matches, /leaderboard, /players
```

---

## ⚙️ Setup & Configuration

### Step 1 — Flash Car Firmware

**For Car 1:**
```cpp
// sketch_jun11a.ino — change these lines
uint8_t peerMAC[6] = { /* Controller 1 MAC address */ };
const uint8_t ESPNOW_CHANNEL = 6; // Use controller's WiFi channel
```

**For Car 2:**
```cpp
uint8_t peerMAC[6] = { /* Controller 2 MAC address */ };
const uint8_t ESPNOW_CHANNEL = 6;
```

### Step 2 — Flash Controller Firmware

**For Controller 1:**
```cpp
// sketch_jun11b.ino — change these lines
const int PLAYER_NUMBER = 1;
uint8_t receiverMAC[6] = { /* Car 1 MAC address */ };
#define WIFI_SSID     "your_wifi"
#define WIFI_PASSWORD "your_password"
#define API_KEY       "firebase_api_key"
#define DATABASE_URL  "https://your-project-rtdb.firebaseio.com"
```

**For Controller 2:**
```cpp
const int PLAYER_NUMBER = 2;
uint8_t receiverMAC[6] = { /* Car 2 MAC address */ };
// (same WiFi + Firebase config)
```

### Step 3 — Find MAC Addresses
Open Serial Monitor (115200 baud) on each ESP32 to read its MAC address from the boot log, then fill in the corresponding `receiverMAC[]` / `peerMAC[]` on the paired device.

### Step 4 — Firebase RTDB Setup
Initialize the RTDB root with these default values:
```json
{
  "race_status": "pending",
  "lap_selection": 1,
  "player1_penalty": 0,
  "player2_penalty": 0,
  "finalwinner": "",
  "match_saved": false
}
```

### Step 5 — Run the Web App
```bash
cd "Web Application"
npm install
# Create .env with your Firebase config keys
npm run dev
```

---

## 📚 Libraries & Dependencies

### Car Firmware (Arduino / ESP32)
| Library | Purpose |
|---|---|
| `ESP32Servo` | ESC + steering servo PWM |
| `WiFi` / `esp_now` / `esp_wifi` | ESP-NOW wireless |
| `Wire` | I2C bus |
| `OneWire` + `DallasTemperature` | DS18B20 temperature sensor |
| `Adafruit_INA219` | Battery voltage/current monitoring |
| `Adafruit_MPU6050` + `Adafruit_Sensor` | Accelerometer collision detection |

### Controller Firmware (Arduino / ESP32)
| Library | Purpose |
|---|---|
| `WiFi` / `esp_now` / `esp_wifi` | ESP-NOW + WiFi for Firebase |
| `Wire` | I2C for OLED |
| `Adafruit_GFX` + `Adafruit_SSD1306` | OLED display driver |
| `Firebase_ESP_Client` | Firebase RTDB read/write |

### Race Track Arena Firmware (Arduino / ESP32)
| Library | Purpose |
|---|---|
| `WiFi` | WiFi connection to Firebase |
| `FirebaseESP32` | Firebase RTDB read (legacy library, database secret auth) |
| `ESP32Servo` | MG995 servo PWM control |

### Web Application (Node.js)
| Package | Version | Purpose |
|---|---|---|
| `react` + `react-dom` | ^19.2.4 | UI framework |
| `react-router-dom` | ^7.15.1 | Client-side routing |
| `firebase` | ^12.11.0 | Firestore + RTDB client |
| `tailwindcss` | ^4.3.0 | Utility CSS styling |
| `react-icons` | ^5.6.0 | Icon library |
| `vite` | ^8.0.1 | Build tool + dev server |

---

## 🎓 Academic Context

This project was developed over a **one-year period** for the

**`IN24-S1-IN1901` — Microcontroller Based Application Development Project**

under the supervision of **Former Dean Mr. B.H. SUDANTHA**,
**Faculty of Information Technology, University of Moratuwa**.

---

## 👥 Team

**ByteForge** — Faculty of Information Technology, University of Moratuwa

| Name | GitHub |
|---|---|
| G.T. Idusara | [@thamod-03](https://github.com/thamod-03) |
| D.S. Dilmina | [@sandeepa-gittech](https://github.com/sandeepa-gittech) |
| M.H.S. Sheyan | [@sahiru-03](https://github.com/sahiru-03) |
|K. Lageesan | [@lageesan-k](https://github.com/lageesan-k) |
| M. Manoshiha | [@Manoshiha](https://github.com/Manoshiha) |

---

## 📄 License

This project was developed for academic purposes by the ByteForge team. Please contact the repository owners before using or redistributing this project.

---

*Built with ❤️ and a lot of soldering iron burns.*
