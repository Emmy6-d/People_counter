/*
  ESP32 Dual-Sensor Directional People Counter
  ------------------------------------------------
  Hardware:
    S1 = GPIO 34
    S2 = GPIO 35
    Buzzer = GPIO 25
    LCD = I2C 0x27, 16x2

  Logic:
    S1 -> S2 outside the simultaneous window = person enters (+1)
    S2 -> S1 outside the simultaneous window = person leaves (-1)
    Same/near-simultaneous = NO COUNT
    One sensor only until timeout = NO COUNT
    After an event, both sensors must clear before a new event.

  Network:
    On every valid directional crossing, send one POST event to Flask.
*/

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

// ---------------- Hardware ----------------
const uint8_t PIN_S1     = 34;
const uint8_t PIN_S2     = 35;
const uint8_t PIN_BUZZER = 25;

LiquidCrystal_I2C lcd(0x27, 16, 2);

// ---------------- Wi-Fi / server ----------------
const char* WIFI_SSID     = "Emmy";
const char* WIFI_PASSWORD = "Emmanuel";

// Render HTTPS endpoint.
const char* FLASK_API_URL =
  "https://people-counter-api-2tn2.onrender.com/api/device/event";

// Must match DEVICE_API_KEY in Flask .env
const char* DEVICE_API_KEY = "fDr15jScnzBpcQV6vlteQbcPtlZ70T14m6C2YsCOQyQ";

const char* DEVICE_ID = "ESP32-COUNTER-01";

// ---------------- Timing ----------------
const unsigned long DEBOUNCE_MS            = 50;
const unsigned long SIMULTANEOUS_WINDOW_MS = 100;
const unsigned long DETECTION_TIMEOUT_MS   = 10000;
const unsigned long BUZZER_MS              = 200;
const unsigned int BUZZER_FREQUENCY_HZ     = 2000;
// Render may need several seconds to wake after inactivity.
const unsigned long HTTP_TIMEOUT_MS        = 10000;

// ---------------- Debounce ----------------
bool s1RawLast = HIGH, s2RawLast = HIGH;
bool s1Debounced = HIGH, s2Debounced = HIGH;
unsigned long s1DebounceTimer = 0, s2DebounceTimer = 0;

bool s1PrevStable = HIGH, s2PrevStable = HIGH;

// ---------------- State machine ----------------
enum EventState {
  WAIT_FOR_FIRST,
  WAIT_FOR_SECOND,
  WAIT_FOR_CLEAR
};

EventState eventState = WAIT_FOR_FIRST;

uint8_t firstSensor = 0;
unsigned long firstTriggerTime = 0;

// Local totals shown on the LCD. They reset when the ESP32 restarts.
long enteredCount = 0;
long exitedCount = 0;
long objectCount = 0;

// ---------------- Network retry ----------------
unsigned long lastWiFiAttempt = 0;
const unsigned long WIFI_RETRY_MS = 10000;
bool statusNeedsUpdate = true;
bool lastReportedS1 = HIGH;
bool lastReportedS2 = HIGH;

struct NetworkMessage {
  bool isStatus;
  char eventType[24];
  char sensorSequence[8];
  char status[5];
  int countDelta;
};

QueueHandle_t networkQueue;

// =====================================================

void setup() {
  Serial.begin(115200);

  pinMode(PIN_S1, INPUT);
  pinMode(PIN_S2, INPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);

  // Keep the Wi-Fi radio awake so local HTTP requests do not wait for wake-up.
  WiFi.setSleep(false);

  Wire.begin();
  lcd.init();
  lcd.backlight();

  updateLCD("Starting...", "Please wait");

  connectWiFi();

  networkQueue = xQueueCreate(8, sizeof(NetworkMessage));
  xTaskCreatePinnedToCore(
    networkTask,
    "networkTask",
    8192,
    nullptr,
    1,
    nullptr,
    0
  );

  // Server timestamps events, but NTP is useful for diagnostics.
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  updateLCDDashboard();
  Serial.println("System ready.");
}

void loop() {
  unsigned long now = millis();

  maintainWiFi();

  updateDebounce(
    PIN_S1, s1RawLast, s1Debounced, s1DebounceTimer, now
  );

  updateDebounce(
    PIN_S2, s2RawLast, s2Debounced, s2DebounceTimer, now
  );

  bool s1Rose = (s1Debounced == LOW && s1PrevStable == HIGH);
  bool s2Rose = (s2Debounced == LOW && s2PrevStable == HIGH);

  switch (eventState) {

    case WAIT_FOR_FIRST:

      if (s1Rose && s2Rose) {
        Serial.println("Both triggered same cycle -> no count.");
        eventState = WAIT_FOR_CLEAR;

      } else if (s1Rose) {
        firstSensor = 1;
        firstTriggerTime = now;
        eventState = WAIT_FOR_SECOND;
        Serial.println("S1 triggered first.");

      } else if (s2Rose) {
        firstSensor = 2;
        firstTriggerTime = now;
        eventState = WAIT_FOR_SECOND;
        Serial.println("S2 triggered first.");
      }

      break;

    case WAIT_FOR_SECOND: {

      unsigned long elapsed = now - firstTriggerTime;
      bool secondRose = (firstSensor == 1) ? s2Rose : s1Rose;

      if (secondRose) {

        if (firstSensor == 1) {

          enteredCount++;
          objectCount++;

          Serial.print("VALID S1->S2. Entered. Local count = ");
          Serial.println(objectCount);

          // Start immediately on a valid count. The duration is handled by
          // tone(), so the Flask request cannot extend or suppress the beep.
          tone(PIN_BUZZER, BUZZER_FREQUENCY_HZ, BUZZER_MS);

          // The important part: one database event per valid crossing.
          sendCountEvent("valid_s1_to_s2", "S1->S2", 1);

        } else {
          if (objectCount > 0) {
            objectCount--;
            exitedCount++;

            Serial.print("VALID S2->S1. Exited. Local count = ");
            Serial.println(objectCount);
            tone(PIN_BUZZER, BUZZER_FREQUENCY_HZ, BUZZER_MS);

            sendCountEvent("valid_s2_to_s1", "S2->S1", -1);
          } else {
            Serial.println("S2->S1 ignored: nobody is inside.");
          }
        }

        eventState = WAIT_FOR_CLEAR;

      } else if (elapsed > DETECTION_TIMEOUT_MS) {

        Serial.println("Timeout waiting for second sensor -> no count.");
        eventState = WAIT_FOR_CLEAR;
      }

      break;
    }

    case WAIT_FOR_CLEAR:

      if (s1Debounced == HIGH && s2Debounced == HIGH) {
        eventState = WAIT_FOR_FIRST;

        Serial.println("Sensors cleared. Ready for next person.");
      }

      break;
  }

  if (s1Debounced != s1PrevStable || s2Debounced != s2PrevStable) {
    updateLCDDashboard();
  }

  if (statusNeedsUpdate ||
      s1Debounced != lastReportedS1 ||
      s2Debounced != lastReportedS2) {
    if (sendSensorStatus()) {
      lastReportedS1 = s1Debounced;
      lastReportedS2 = s2Debounced;
      statusNeedsUpdate = false;
    }
  }

  s1PrevStable = s1Debounced;
  s2PrevStable = s2Debounced;

}

// =====================================================

void updateDebounce(
  uint8_t pin,
  bool &rawLast,
  bool &debouncedState,
  unsigned long &timer,
  unsigned long now
) {
  bool raw = digitalRead(pin);

  if (raw != rawLast) {
    timer = now;
    rawLast = raw;
  }

  if ((now - timer) >= DEBOUNCE_MS) {
    debouncedState = raw;
  }
}

void updateLCD(const char *line1, const char *line2) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(line2);
}

void updateLCDDashboard() {
  char line1[17];
  char line2[17];
  const char *status = "OK";

  if (s1Debounced == LOW && s2Debounced == LOW) {
    status = "Both";
  } else if (s1Debounced == LOW) {
    status = "S1";
  } else if (s2Debounced == LOW) {
    status = "S2";
  }

  snprintf(line1, sizeof(line1), "IN:%4ld OUT:%4ld", enteredCount, exitedCount);
  snprintf(line2, sizeof(line2), "NOW:%4ld ST:%4s", objectCount, status);

  updateLCD(line1, line2);
}

// =====================================================
// Wi-Fi
// =====================================================

void connectWiFi() {
  Serial.print("Connecting to Wi-Fi");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(250);
    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Wi-Fi connected. ESP32 IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Wi-Fi not connected. Will retry.");
  }
}

void maintainWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  unsigned long now = millis();

  if (now - lastWiFiAttempt >= WIFI_RETRY_MS) {
    lastWiFiAttempt = now;
    Serial.println("Retrying Wi-Fi...");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
}

// =====================================================
// Send one valid event to Flask
// =====================================================

bool queueNetworkMessage(const NetworkMessage &message) {
  if (networkQueue == nullptr) {
    return false;
  }

  return xQueueSend(networkQueue, &message, 0) == pdTRUE;
}

bool sendCountEvent(const char *eventType, const char *sensorSequence, int countDelta) {
  NetworkMessage message = {};
  message.isStatus = false;
  message.countDelta = countDelta;
  snprintf(message.eventType, sizeof(message.eventType), "%s", eventType);
  snprintf(message.sensorSequence, sizeof(message.sensorSequence), "%s", sensorSequence);
  return queueNetworkMessage(message);
}

bool sendSensorStatus() {
  const char *status = "OK";
  if (s1Debounced == LOW && s2Debounced == LOW) {
    status = "Both";
  } else if (s1Debounced == LOW) {
    status = "S1";
  } else if (s2Debounced == LOW) {
    status = "S2";
  }

  NetworkMessage message = {};
  message.isStatus = true;
  snprintf(message.status, sizeof(message.status), "%s", status);
  return queueNetworkMessage(message);
}

void networkTask(void *parameter) {
  NetworkMessage message;

  for (;;) {
    if (xQueueReceive(networkQueue, &message, portMAX_DELAY) != pdTRUE) {
      continue;
    }

    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("Network message skipped: Wi-Fi disconnected.");
      continue;
    }

    HTTPClient http;
    WiFiClientSecure client;
    client.setInsecure();
    String url = String(FLASK_API_URL);

    if (message.isStatus) {
      url.replace("/api/device/event", "/api/device/status");
    }

    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-Device-Key", DEVICE_API_KEY);
    http.setTimeout(HTTP_TIMEOUT_MS);

    String payload;
    if (message.isStatus) {
      payload = String("{\"device_id\":\"") + DEVICE_ID +
                "\",\"status\":\"" + message.status + "\"}";
    } else {
      payload = String("{") +
                "\"device_id\":\"" + DEVICE_ID + "\"," +
                "\"event_type\":\"" + message.eventType + "\"," +
                "\"sensor_sequence\":\"" + message.sensorSequence + "\"," +
                "\"count_delta\":" + String(message.countDelta) +
                "}";
    }

    int httpCode = http.POST(payload);
    if (httpCode <= 0) {
      Serial.print("HTTP error: ");
      Serial.println(http.errorToString(httpCode));
    }
    http.end();
  }
}
