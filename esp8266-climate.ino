#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <DHT.h>
#include <Arduino.h>

// ===== USER CONFIG =====
const char* ssid     = ""; // <-- enter wi-fi name from earlier (ex: Climate)
const char* password = ""; // <-- enter wi-fi password from earlier (ex: cl!mate123)
const unsigned int UDP_PORT = 8081;

// Time between sampling (minutes)
#define INTERVAL_MINUTES   1            // <-- set to 1 or 5 

// Multiple sample timings
#define SAMPLE_WINDOW_SEC  15           // start sampling this many seconds before the tick
#define SAMPLE_PERIOD_MS   2000         // read DHT every 2s in the window

// DHT wiring
#define DHTPIN   14                     // D5 (GPIO14)
#define DHTTYPE  DHT22

// LED (on-board is usually active-LOW on ESP8266)
#define LED_PIN         LED_BUILTIN
#define LED_ACTIVE_LOW  1

// Smooth slow blink timing while trying to connect
#define SLOW_BLINK_ON_MS   300
#define SLOW_BLINK_OFF_MS  700

// Wi‑Fi retry rhythm (non-blocking scheduler)
#define WIFI_SINGLE_TRY_MS   5000       // re-issue Wi‑Fi begin() at most every 5s
#define WIFI_STATUS_POLL_MS   50

// =======================

WiFiUDP udp;
DHT dht(DHTPIN, DHTTYPE);
String deviceId;

// ---- Soft clock state (no external sync) ----
unsigned long bootMs;      // captured at setup
static const long manualOffsetSec = 0;

// ---------- LED helpers ----------
inline void ledWrite(bool on) {
  bool level = LED_ACTIVE_LOW ? !on : on;
  digitalWrite(LED_PIN, level);
}
void ledBlinkSentOnce() {           // single blink on send
  ledWrite(true);  delay(140);
  ledWrite(false); delay(120);
  yield();
}
void ledFlashConnectedFast() {      // fast triple blink on connect
  for (int i = 0; i < 3; i++) {
    ledWrite(true);  delay(80);
    ledWrite(false); delay(80);
    yield();
  }
}

// ---------- Time helpers ----------
inline uint32_t softNowSec() {
  return (uint32_t)((millis() - bootMs) / 1000UL) + (uint32_t)manualOffsetSec;
}
uint32_t nextAlignedTick(uint32_t now) {
  const uint32_t intervalSec = (uint32_t)INTERVAL_MINUTES * 60U;
  return (now - (now % intervalSec)) + intervalSec;
}
void waitUntilSoft(uint32_t target) {
  while (true) {
    uint32_t now = softNowSec();
    int32_t diff = (int32_t)(target - now);
    if (diff <= 0) break;
    delay(diff > 1 ? 250 : 10);
    yield();
  }
}

// Compute broadcast from IP + mask
IPAddress calcBroadcast(IPAddress ip, IPAddress mask) {
  return IPAddress((uint32)ip | ~((uint32)mask));
}

// Robust DHT read with clamps
bool readDHT_once(float &tC, float &h) {
  for (int i=0;i<3;i++) {
    h  = dht.readHumidity();
    tC = dht.readTemperature();
    if (!isnan(h) && !isnan(tC)) {
      if (h < 0)   h = 0;
      if (h > 100) h = 100;
      if (tC < -40) tC = -40;
      if (tC > 80)  tC = 80;
      return true;
    }
    delay(120);
    yield();
  }
  return false;
}

// Small-N median
float medianOf(float* arr, int n) {
  float tmp[10]; // up to ~8–10 samples
  for (int i=0;i<n;i++) tmp[i]=arr[i];
  for (int i=1;i<n;i++){
    float key=tmp[i]; int j=i-1;
    while (j>=0 && tmp[j]>key){ tmp[j+1]=tmp[j]; j--; }
    tmp[j+1]=key;
  }
  return (n%2) ? tmp[n/2] : 0.5f*(tmp[n/2 - 1] + tmp[n/2]);
}

// JSON builder (no real UTC — we send softEpochSec)
String buildJson(bool ok, float tMed, float hMed) {
  uint32_t softSec = softNowSec();
  String json = String("{\"time\":\"device-soft\",") +
                "\"softEpochSec\":" + String((unsigned long)softSec) + "," +
                "\"deviceId\":\"" + deviceId + "\"," +
                "\"temperature\":" + (ok ? String(tMed,1) : String("null")) + "," +
                "\"humidity\":"    + (ok ? String(hMed,1) : String("null")) + "}";
  return json;
}

/* --------- NEW: smooth, non-blocking forever Wi‑Fi retry with even slow blink ---------
   Uses a single while loop that:
     - toggles LED at exact on/off intervals via a millis() scheduler
     - re-issues WiFi.begin() at most every WIFI_SINGLE_TRY_MS
     - polls status frequently without blocking long delays
*/
void waitForWifiForever() {
  Serial.println("[WiFi] Trying to connect (will retry forever)...");
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);
  WiFi.persistent(false);

  // blink scheduler
  bool blinkState = false;
  unsigned long nextBlinkChange = millis();    // toggle immediately

  // Wi‑Fi attempt scheduler
  bool attemptIssued = false;
  unsigned long nextAttemptAt = 0;

  while (WiFi.status() != WL_CONNECTED) {
    unsigned long now = millis();

    // schedule Wi‑Fi attempts
    if (!attemptIssued || (now >= nextAttemptAt)) {
      WiFi.disconnect(true);
      delay(10);
      WiFi.begin(ssid, password);
      attemptIssued = true;
      nextAttemptAt = now + WIFI_SINGLE_TRY_MS;  // don't spam begin()
    }

    // smooth slow blink (even cadence)
    if (now >= nextBlinkChange) {
      blinkState = !blinkState;
      ledWrite(blinkState);
      nextBlinkChange = now + (blinkState ? SLOW_BLINK_ON_MS : SLOW_BLINK_OFF_MS);
    }

    delay(WIFI_STATUS_POLL_MS);
    yield();
  }

  // Connected!
  ledWrite(false);
  Serial.printf("[WiFi] Connected  IP=%s  Mask=%s  GW=%s\n",
    WiFi.localIP().toString().c_str(),
    WiFi.subnetMask().toString().c_str(),
    WiFi.gatewayIP().toString().c_str());

  udp.begin(0);
  ledFlashConnectedFast();   // fast triple blink on connect
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  ledWrite(false);

  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("=== ESP8266 DHT Broadcaster (smooth slow-blink reconnect + fast triple on connect) ===");

  bootMs = millis();
  deviceId = "esp8266-" + String(ESP.getChipId(), HEX);
  Serial.printf("[ID] %s\n", deviceId.c_str());

  dht.begin();
  Serial.println("[DHT] Warm-up 2s...");
  delay(2000);

  // Block here until Wi‑Fi connects; smooth slow-blink while trying
  waitForWifiForever();
}

void loop() {
  // If Wi‑Fi dropped for any reason, go back to smooth forever‑retry mode
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] Lost connection. Reconnecting...");
    waitForWifiForever();
  }

  // Determine next exact minute/5‑minute tick on the soft clock
  uint32_t now = softNowSec();
  uint32_t tick = nextAlignedTick(now);
  uint32_t samplingStart = tick - SAMPLE_WINDOW_SEC;

  // Wait until sampling window opens
  if ((int32_t)(samplingStart - now) > 0) {
    Serial.printf("[WAIT] Sampling window @ softSec=%lu (in %ld s)\n",
                  (unsigned long)samplingStart, (long)(samplingStart - now));
    waitUntilSoft(samplingStart);
  }

  // Sampling window loop
  const int MAX_SAMPLES = (SAMPLE_WINDOW_SEC / (SAMPLE_PERIOD_MS/1000)) + 2; // ~8 samples
  float tBuf[10]; float hBuf[10]; int n=0;
  Serial.printf("[SAMPLE] Window start. Target tick in %d s\n", SAMPLE_WINDOW_SEC);
  unsigned long lastMs = 0;

  // Recompute tick just before the loop
  tick = nextAlignedTick(softNowSec());

  while (true) {
    // Bail to reconnect loop if Wi‑Fi drops mid‑cycle
    if (WiFi.status() != WL_CONNECTED) break;

    uint32_t tnow = softNowSec();
    if ((int32_t)(tnow - tick) >= 0) break; // hit the boundary

    unsigned long ms = millis();
    if (ms - lastMs >= SAMPLE_PERIOD_MS) {
      lastMs = ms;
      float tC, h;
      if (readDHT_once(tC, h)) {
        if (n < MAX_SAMPLES) { tBuf[n]=tC; hBuf[n]=h; n++; }
        Serial.printf("  sample #%d -> T=%.1f C  H=%.1f %%\n", n, tC, h);
      } else {
        Serial.println("  sample -> read fail");
      }
    }
    delay(10);
    yield();
  }

  // If Wi‑Fi dropped, reconnect and redo the loop from the top
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[Wi‑Fi] Disconnected during sampling; reconnecting...");
    waitForWifiForever();
    return;
  }

  // Compute median and send
  bool ok = (n > 0);
  float tMed = ok ? medianOf(tBuf, n) : NAN;
  float hMed = ok ? medianOf(hBuf, n) : NAN;

  String json = buildJson(ok, tMed, hMed);

  Serial.println("-------------------------------------------------");
  Serial.printf("[SEND @ softSec=%lu] n=%d  %s\n", (unsigned long)softNowSec(), n, json.c_str());

  // UDP broadcast
  IPAddress bcast = calcBroadcast(WiFi.localIP(), WiFi.subnetMask());
  udp.beginPacket(bcast, UDP_PORT);
  udp.write((const uint8_t*)json.c_str(), json.length());
  udp.endPacket();

  // Single blink on each send
  ledBlinkSentOnce();

  // Schedule the next cycle; enter next sampling window
  uint32_t nextTick = tick + (INTERVAL_MINUTES * 60U);
  waitUntilSoft(nextTick - SAMPLE_WINDOW_SEC);
}
