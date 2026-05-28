#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "secrets.h"

#define TRIG_PIN 5
#define ECHO_PIN 18

const float OCCUPIED_CM   = 100.0;   // closer than this = spot taken (tune below your empty distance)
const unsigned long DEBOUNCE_MS = 1500;  // reading must hold this long before the state flips
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