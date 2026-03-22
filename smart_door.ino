#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ESP32Servo.h>

// ── Wi-Fi Credentials ─────────────────────────────────────────────────────
const char* ssid     = "samosa";
const char* password = "nitin@123";

// ── Ngrok Backend ─────────────────────────────────────────────────────────
const String NGROK_URL = "https://82ed-2409-40c4-184-cdb4-cd54-c2f8-19fe-7b61.ngrok-free.app"; // <-- Automatically Generated

// ── Servo Configuration ───────────────────────────────────────────────────
Servo doorServo;
const int SERVO_PIN      = 13;
const int LOCKED_ANGLE   = 0;
const int UNLOCKED_ANGLE = 90;

// ── Internal State ────────────────────────────────────────────────────────
enum SystemState { IDLE, WAITING_FOR_OTP };
SystemState currentState = IDLE;
int otpAttemptsLocal = 0;

unsigned long lastPollTime = 0;
const unsigned long pollInterval = 1000; // Poll every 1 second

// ── Function Declarations ─────────────────────────────────────────────────
void connectToWiFi();
void pollBackend();
void processCommand(String message);
void submitOTP(String otp);
void unlockDoor();
void lockDoor();
void publishState(const char* stateMsg);

// ─────────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1000);

  doorServo.attach(SERVO_PIN);
  
  connectToWiFi();
  
  lockDoor(); // Sets initial condition and publishes it

  Serial.println("==========================================");
  Serial.println("Smart Door HTTP Polling System Initialized");
  Serial.println("Type 'hi' and press Enter to trigger doorbell manually");
  Serial.println("==========================================");
}

// ─────────────────────────────────────────────────────────────────────────
void loop() {
  // Reconnect WiFi if needed
  if (WiFi.status() != WL_CONNECTED) {
    connectToWiFi();
  }

  // Poll Backend periodically
  if (millis() - lastPollTime >= pollInterval) {
    lastPollTime = millis();
    pollBackend();
  }

  // Read Serial Monitor for manual ESP32 interaction
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim();

    if (input.length() > 0) {
      if (currentState == IDLE) {
        if (input.equalsIgnoreCase("hi")) {
          Serial.println("[ESP] Doorbell triggered locally! Publishing SOMEONE_AT_DOOR...");
          publishState("SOMEONE_AT_DOOR"); 
        } else {
          Serial.println("[ESP] Unknown command. Type 'hi' to ring doorbell.");
        }
      } else if (currentState == WAITING_FOR_OTP) {
        Serial.println("[ESP] Submitting OTP to Server for verification: " + input);
        submitOTP(input);
      }
    }
  }
}

// ── Wi-Fi Connection ──────────────────────────────────────────────────────
void connectToWiFi() {
  Serial.println("Starting WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("[WiFi] Connecting");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\n[WiFi] Connected!");
  Serial.print("[WiFi] IP Address: ");
  Serial.println(WiFi.localIP());
}

// ── HTTP Polling ──────────────────────────────────────────────────────────
void pollBackend() {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure secureClient;
  secureClient.setInsecure();
  HTTPClient http;

  http.begin(secureClient, NGROK_URL + "/api/esp/poll");
  int httpCode = http.GET();
  
  String cmd = "";
  if (httpCode == 200) {
    cmd = http.getString();
  }
  http.end(); // Close connection FIRST to free up ESP32 SSL memory!

  if (cmd != "NONE" && cmd.length() > 0) {
    Serial.println("[ESP] Received command from Backend: " + cmd);
    processCommand(cmd);
  }
}

void processCommand(String message) {
  if (message == "UNLOCK") {
    currentState = IDLE;
    unlockDoor();
  } 
  else if (message == "LOCK") {
    currentState = IDLE;
    lockDoor();
  }
  else if (message == "SIMULATE_BELL") {
    Serial.println("[ESP] Web commanded doorbell simulation.");
    publishState("SOMEONE_AT_DOOR");
  }
  else if (message == "WAITING_FOR_OTP") {
    Serial.println("[ESP] Server sent OTP via SMS. Awaiting Keypad/Serial input...");
    currentState = WAITING_FOR_OTP;
    otpAttemptsLocal = 0; // Reset local attempts counter
  }
}

void publishState(const char* stateMsg) {
  if (WiFi.status() != WL_CONNECTED) return;
  
  WiFiClientSecure secureClient;
  secureClient.setInsecure();
  HTTPClient http;
  
  http.begin(secureClient, NGROK_URL + "/api/esp/state");
  http.addHeader("Content-Type", "application/json");
  
  // Format JSON payload manually
  String payload = "{\"state\":\"" + String(stateMsg) + "\"}";
  int code = http.POST(payload);
  
  if(code <= 0) {
    Serial.print("[ESP] Failed to publish state: ");
    Serial.println(http.errorToString(code));
  }
  http.end();
}

// ── Securely Check OTP Against Server ─────────────────────────────────────
void submitOTP(String otp) {
  if (WiFi.status() != WL_CONNECTED) return;
  
  WiFiClientSecure secureClient;
  secureClient.setInsecure();
  HTTPClient http;
  
  http.begin(secureClient, NGROK_URL + "/api/esp/verify");
  http.addHeader("Content-Type", "application/json");
  
  Serial.println("[ESP] Verifying OTP with centralized Ubuntu Server...");

  String payload = "{\"otp\":\"" + otp + "\"}";
  int code = http.POST(payload);
  
  if (code > 0) {
    String response = http.getString();
    if(response == "VERIFIED") {
        Serial.println("[ESP] Server verified OTP securely. Processing UNLOCK command...");
        currentState = IDLE; 
    } else if(response == "MAX_TRIES") {
        Serial.println("[ESP] 3 Invalid OTP attempts reached! Door stays Locked and Dashboard reset.");
        currentState = IDLE;
    } else {
        otpAttemptsLocal++;
        Serial.printf("[ESP] Invalid OTP! Attempt %d of 3. Try again.\n", otpAttemptsLocal);
        // Remain in WAITING_FOR_OTP state so user can enter again
    }
  } else {
    Serial.print("[ESP] Failed to submit OTP: ");
    Serial.println(http.errorToString(code));
    currentState = IDLE;
  }
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
}