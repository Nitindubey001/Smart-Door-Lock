#include <ArduinoJson.h>
#include <ESP32Servo.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

// ── Wi-Fi Credentials ─────────────────────────────────────────────────────
const char *ssid = "samosa";
const char *password = "nitin@123";

// ── Server URL ────────────────────────────────────────────────────────────
// The local IP of your Ubuntu computer running 'node server.js'
// Replace 192.168.x.x with your actual IP if 172.26.57.104 is incorrect.
const String SERVER_URL = "http://172.26.57.104:3000";

// ── Servo Configuration ───────────────────────────────────────────────────
Servo doorServo;
const int SERVO_PIN = 13;
const int LOCKED_ANGLE = 0;
const int UNLOCKED_ANGLE = 90;

// ── Internal State ────────────────────────────────────────────────────────
enum SystemState { IDLE, WAITING_FOR_OTP };
SystemState currentState = IDLE;

// ── Function Declarations ─────────────────────────────────────────────────
void connectToWiFi();
void requestOTP();
void verifyOTP(String otp);
void unlockDoor();
void lockDoor();

// ─────────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1000);

  doorServo.attach(SERVO_PIN);

  connectToWiFi();
  lockDoor();

  Serial.println("==========================================");
  Serial.println("Smart Door System Initialized");
  Serial.println("Type 'hi' and press Enter to trigger doorbell");
  Serial.println("==========================================");
}

// ─────────────────────────────────────────────────────────────────────────
void loop() {
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim();

    if (input.length() > 0) {
      if (currentState == IDLE) {
        if (input.equalsIgnoreCase("hi")) {
          Serial.println("[ESP] Doorbell triggered! Requesting OTP...");
          requestOTP();
        } else {
          Serial.println(
              "[ESP] Unknown command. Type 'hi' to simulate arrival.");
        }
      } else if (currentState == WAITING_FOR_OTP) {
        Serial.println("[ESP] Verifying OTP: " + input);
        verifyOTP(input);
      }
    }
  }
}

// ── Wi-Fi Connection ──────────────────────────────────────────────────────
void connectToWiFi() {
  Serial.println("Starting WiFi...");
  WiFi.disconnect(true);
  delay(100);
  WiFi.mode(WIFI_STA);
  delay(100);

  WiFi.begin(ssid, password);
  Serial.print("[WiFi] Connecting");

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    attempts++;

    if (attempts % 10 == 0) {
      Serial.print(" [status: ");
      Serial.print(WiFi.status());
      Serial.println("]");
    }

    if (attempts >= 40) {
      Serial.println("\n[WiFi] Failed to connect. Restarting...");
      delay(3000);
      ESP.restart();
    }
  }

  Serial.println("\n[WiFi] Connected!");
  Serial.print("[WiFi] IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.print("[WiFi] Signal Strength (RSSI): ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");
}

// ── Request OTP from server ───────────────────────────────────────────────
void requestOTP() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[ESP] Error: Wi-Fi not connected");
    return;
  }

  // Use standard HTTPClient for local HTTP communication
  HTTPClient http;
  
  // POST request to trigger the OTP generation via your local Node server
  String url = SERVER_URL + "/api/generate-otp";
  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  // Send empty JSON payload as required for POST
  int code = http.POST("{}");

  if (code > 0) {
    String response = http.getString();
    Serial.println("[Server] Response: " + response);

    if (code == 200) {
      Serial.println("[ESP] OTP sent to phone via SMS!");
      Serial.println("[ESP] Enter the OTP you received and press Enter...");
      currentState = WAITING_FOR_OTP;
    } else {
      Serial.print("[ESP] Server error: HTTP ");
      Serial.println(code);
    }
  } else {
    Serial.print("[ESP] Request failed: ");
    Serial.println(http.errorToString(code));
  }

  http.end();
}

// ── Verify OTP entered on keypad ──────────────────────────────────────────
void verifyOTP(String otp) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[ESP] Error: Wi-Fi not connected");
    return;
  }

  HTTPClient http;
  String url = SERVER_URL + "/api/verify-otp";
  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  // Create JSON payload properly for POST request
  StaticJsonDocument<200> doc;
  doc["otp"] = otp;
  String requestBody;
  serializeJson(doc, requestBody);

  int code = http.POST(requestBody);

  if (code > 0) {
    String response = http.getString();
    Serial.println("[Server] Response: " + response);

    if (code == 200) {
      Serial.println("[ESP] OTP correct! Unlocking door...");
      unlockDoor();
    } else {
      Serial.println("[ESP] Wrong OTP. Try again or type 'hi' for a new code.");
    }
  } else {
    Serial.print("[ESP] Request failed: ");
    Serial.println(http.errorToString(code));
  }

  currentState = IDLE;
  http.end();
}

// ── Door Control ──────────────────────────────────────────────────────────
void unlockDoor() {
  doorServo.write(UNLOCKED_ANGLE);
  Serial.println("[ESP] Door UNLOCKED");
  delay(5000);
  Serial.println("[ESP] Auto-locking door...");
  lockDoor();
}

void lockDoor() {
  doorServo.write(LOCKED_ANGLE);
  Serial.println("[ESP] Door LOCKED");

  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(SERVER_URL + "/api/lock");
    http.addHeader("Content-Type", "application/json");
    http.POST("{}");
    http.end();
  }
}