# Smart Parking Sensor (ESP32 + HC-SR04)

An IoT parking-spot occupancy monitor built on an ESP32. An ultrasonic sensor measures the distance to whatever is in front of it; the ESP32 decides whether the spot is **FREE** or **OCCUPIED** and serves a live, color-coded web dashboard over WiFi that any phone or laptop on the same network can open.

> IoT 2026 — course project. Individual implementation.

---

## Overview

The device watches a single parking spot. When empty, the sensor sees the far floor/wall (a large distance). When a car parks, the nearest surface is much closer (a small distance). A configurable threshold separates the two states, and a short debounce prevents the status from flickering when something briefly passes by.

The result is exposed two ways:
- A **web page** at the ESP32's IP address showing a full-screen `FREE` (green) / `OCCUPIED` (red) status plus the live distance.
- A **`/data` JSON endpoint** the page polls twice a second, also reusable by any other client.

This demonstrates the full IoT loop: **sense → process → transmit → display.**

---

## Features

- Real-time ultrasonic distance sensing (10 readings/second)
- Threshold-based occupancy detection with debounce (anti-flicker)
- Self-hosted web dashboard — no cloud account, no broker, no app required
- JSON API endpoint for integration
- Runs entirely on the ESP32, powered over USB

---

## Bill of Materials

| Component | Qty | Notes |
|---|---|---|
| ESP32 DevKit (WROOM-32, USB-C) | 1 | Classic ESP32, CP2102 USB-to-serial |
| HC-SR04 ultrasonic sensor | 1 | Distance sensor |
| Breadboard (400 points) | 1 | Solderless |
| Male–female jumper wires | 4 | Sensor → breadboard |
| USB-C data cable | 1 | Programming + power |

---

## Wiring

The ESP32 straddles the center groove of the breadboard. The HC-SR04 connects with four male–female jumpers — female ends onto the sensor pins, male ends into the breadboard rows matching the ESP32 pins.

| HC-SR04 pin | ESP32 pin | Wire (suggested) |
|---|---|---|
| VCC | **3V3** | Red |
| Trig | **GPIO 5** (`D5`) | Orange/Yellow |
| Echo | **GPIO 18** (`D18`) | Green |
| GND | **GND** | Grey |

### Why 3.3 V and no voltage divider

The HC-SR04 is officially a 5 V device, and its `Echo` pin would normally output 5 V — too high for the ESP32's 3.3 V GPIO, requiring a resistor voltage divider. By powering the sensor from the **3.3 V** pin instead of 5 V, the `Echo` signal is already ~3.3 V and safe to wire directly. This trades a small amount of maximum range (see *Limitations*) for a much simpler circuit — perfectly adequate for parking distances.

> Add a wiring photo here: `docs/wiring.jpg`

---

## Software Setup

Built with **PlatformIO** (VS Code) using the Arduino framework. No external libraries are needed — `WiFi` and `WebServer` ship with the ESP32 Arduino core.

### `platformio.ini`

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
; The two lines below are machine-specific — adjust to your serial port
; (find it with: ls /dev/cu.*  on macOS/Linux, or check Device Manager on Windows)
upload_port = /dev/cu.usbserial-0001
monitor_port = /dev/cu.usbserial-0001
```

### Configuration

Edit these at the top of `src/main.cpp` before flashing:

```cpp
const char* WIFI_SSID = "YOUR_WIFI_NAME";      // 2.4 GHz network only
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

const float OCCUPIED_CM       = 100.0;  // closer than this = OCCUPIED
const unsigned long DEBOUNCE_MS = 1500; // state must hold this long before it flips
```

- **`OCCUPIED_CM`** — the trip distance. Set it comfortably *below* your empty-spot reading. With an empty baseline around 145 cm, 100 cm works well; lower it for a more sensitive trigger.
- **`DEBOUNCE_MS`** — how long a reading must persist before the state changes. Higher = calmer display, lower = snappier.

> **The ESP32 only connects to 2.4 GHz WiFi**, not 5 GHz. A phone hotspot (set to 2.4 GHz) is the most reliable option for a portable demo.

### Build, flash, run

1. **Upload** (PlatformIO `→`). If it hangs on `Connecting....`, hold the **BOOT** button until flashing starts, then release.
2. Open the **Serial Monitor** (plug icon).
3. Press the **EN** (reset) button on the board.
4. It prints the dashboard address:

   ```
   Connecting to WiFi....
   Connected! Open http://192.168.x.x in your browser
   ```

5. Open that IP on any device on the same network.

---

## How It Works

```
HC-SR04 ──distance──> ESP32 ──threshold + debounce──> state (FREE/OCCUPIED)
                         │
                         ├── serves "/"      → HTML dashboard
                         └── serves "/data"  → { "distance": 142.3, "state": "FREE" }
```

1. Every 100 ms, the ESP32 pulses `Trig` and times the echo on `Echo` to compute distance in cm.
2. If the distance is below `OCCUPIED_CM`, the spot is a candidate for OCCUPIED; otherwise FREE.
3. The candidate state must hold for `DEBOUNCE_MS` before it is committed — this filters out brief disturbances.
4. The committed state and latest distance are served as a web page and a JSON endpoint. The page auto-refreshes every 500 ms.

---

## Firmware

Full source in [`src/main.cpp`](src/main.cpp):

```cpp
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

// ---------- EDIT THESE ----------
const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

#define TRIG_PIN 5
#define ECHO_PIN 18

const float OCCUPIED_CM   = 100.0;       // closer than this = spot taken
const unsigned long DEBOUNCE_MS = 1500;  // reading must hold this long before flip
// --------------------------------

WebServer server(80);

float lastDistance = -1;
bool  occupied = false;
bool  candidate = false;
unsigned long candidateSince = 0;
unsigned long lastRead = 0;

float readDistanceCm() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  if (duration == 0) return -1;            // no echo
  return duration * 0.0343 / 2.0;
}

String pageHtml() {
  return R"HTML(
<!DOCTYPE html><html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Parking Spot</title>
<style>
  body{font-family:-apple-system,sans-serif;margin:0;height:100vh;display:flex;
       flex-direction:column;align-items:center;justify-content:center;transition:background .3s}
  .free{background:#0f6e56}.occupied{background:#a32d2d}
  h1{color:#fff;font-size:14vw;margin:0;letter-spacing:3px}
  p{color:rgba(255,255,255,.85);font-size:6vw;margin:.4em 0 0}
</style></head>
<body class="free">
<h1 id="state">--</h1>
<p id="dist">connecting...</p>
<script>
async function tick(){
  try{
    const d=await(await fetch('/data')).json();
    document.getElementById('state').textContent=d.state;
    document.getElementById('dist').textContent=
      d.distance===null?'no echo':d.distance.toFixed(1)+' cm';
    document.body.className=d.state==='OCCUPIED'?'occupied':'free';
  }catch(e){}
}
setInterval(tick,500);tick();
</script></body></html>
)HTML";
}

void handleRoot() { server.send(200, "text/html", pageHtml()); }

void handleData() {
  String json = "{\"distance\":";
  json += (lastDistance < 0 ? "null" : String(lastDistance, 1));
  json += ",\"state\":\"";
  json += (occupied ? "OCCUPIED" : "FREE");
  json += "\"}";
  server.send(200, "application/json", json);
}

void updateState() {
  float d = readDistanceCm();
  if (d > 0) lastDistance = d;
  bool nowOccupied = (d > 0 && d < OCCUPIED_CM);
  if (nowOccupied != candidate) { candidate = nowOccupied; candidateSince = millis(); }
  if (candidate != occupied && (millis() - candidateSince) > DEBOUNCE_MS) {
    occupied = candidate;
    Serial.printf("State -> %s\n", occupied ? "OCCUPIED" : "FREE");
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  Serial.print("Connecting to WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) { delay(400); Serial.print("."); }
  Serial.printf("\nConnected! Open http://%s in your browser\n", WiFi.localIP().toString().c_str());

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.begin();
}

void loop() {
  server.handleClient();
  if (millis() - lastRead > 100) {   // sample 10x/sec
    lastRead = millis();
    updateState();
  }
}
```

---

## Sensor Specs & Limitations (HC-SR04)

| Property | Value |
|---|---|
| Maximum range | ~400 cm (nominal) |
| Minimum range | ~2 cm |
| Beam angle | ~15° cone |
| Resolution | ~0.3 cm |
| Operating frequency | 40 kHz |

**Limitations to note:**
- Powered at **3.3 V** (not the rated 5 V), the reliable range drops to roughly **2–2.5 m** — still ample for a parking spot.
- The sensor reports the **nearest** object within its ~15° cone, so angled or soft/sound-absorbing surfaces can read inconsistently.
- Objects closer than ~2 cm cannot be measured; out-of-range/no-echo conditions return `-1` and are treated as FREE.

---

## Known Quirks (this board)

- **Manual BOOT on upload:** the board's auto-reset is unreliable, so flashing may require holding the **BOOT** button during `Connecting....`.
- **EN after flashing:** after a successful upload, press the **EN** (reset) button to start the program and see serial output from the beginning.
- **Garbled serial output** (e.g. `xxxx␀x...`) means a **baud mismatch** — confirm both `Serial.begin(115200)` and `monitor_speed = 115200` match.
- **Brownout risk at 3.3 V:** WiFi current spikes can briefly under-power the board; if it resets when WiFi activates, use a stronger USB source.

---

## Project Structure

```
smart-parking-sensor/
├── platformio.ini
├── src/
│   └── main.cpp
├── docs/
│   ├── wiring.jpg        # photo of the breadboard build
│   └── dashboard.png     # screenshot of the FREE/OCCUPIED page
└── README.md
```

---

## Possible Extensions

- **Local feedback:** add green/yellow/red LEDs + a buzzer for a parking-assist guide ("traffic light" + beep as you approach).
- **MQTT / cloud:** publish state to a broker (e.g. for Home Assistant or a multi-spot dashboard) instead of self-hosting.
- **Multiple spots:** one node per spot reporting to a central dashboard, scaling to a full parking lot.
- **Enterprise WiFi:** WPA2-Enterprise support to connect to campus/eduroam networks.

---

## Author

*Your Name* — IoT 2026

## License

MIT (or your choice) — for educational use.
