#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ESP32Servo.h>
#include <LiquidCrystal.h>
#include <Keypad.h>

// ── Wi-Fi Credentials ─────────────────────────────────────────────────────
const char* ssid     = "samosa";
const char* password = "nitin@123";

// ── Ngrok Backend ─────────────────────────────────────────────────────────
const String NGROK_URL = "https://82ed-2409-40c4-184-cdb4-cd54-c2f8-19fe-7b61.ngrok-free.app";

// ── LCD Configuration ─────────────────────────────────────────────────────
// VSS -> GND, VDD -> 5V, V0 -> GND, RW -> GND, A -> 5V (220 ohm), K -> GND
const int rs = 22, en = 23, d4 = 18, d5 = 19, d6 = 21, d7 = 5;
LiquidCrystal lcd(rs, en, d4, d5, d6, d7);

// ── Keypad Configuration ──────────────────────────────────────────────────
const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[ROWS] = {13, 12, 14, 27};
byte colPins[COLS] = {26,  25, 33, 32};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// ── Servo & Button Configuration ──────────────────────────────────────────
Servo doorServo;
const int SERVO_PIN      = 4;
const int BUTTON_PIN     = 16;
const int LOCKED_ANGLE   = 0;
const int UNLOCKED_ANGLE = 90;

// ── Internal State ────────────────────────────────────────────────────────
enum SystemState { IDLE, WAITING_FOR_OTP };
SystemState currentState = IDLE;
int otpAttemptsLocal = 0;
String enteredOTP = "";

unsigned long lastPollTime = 0;
const unsigned long pollInterval = 1000; // Poll every 1 second

// Button debouncing
bool lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;
bool buttonPressed = false;

// ── Function Declarations ─────────────────────────────────────────────────
void connectToWiFi();
void pollBackend();
void processCommand(String message);
void submitOTP(String otp);
void unlockDoor();
void lockDoor();
void publishState(const char* stateMsg);
void updateLCD(String line1, String line2);
void readButton();
void readKeypad();

// ─────────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  
  // Initialize LCD
  lcd.begin(16, 2);
  updateLCD("System Booting", "Please wait...");
  delay(1000);

  // Initialize Servo
  doorServo.attach(SERVO_PIN);
  
  // Initialize Button
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  
  // Connect to Wi-Fi
  connectToWiFi();
  
  // Lock Door Initially
  lockDoor();

  Serial.println("==========================================");
  Serial.println("Smart Door HTTP Polling System Initialized");
  Serial.println("Hardware Connected: LCD, Keypad, Servo, Button");
  Serial.println("==========================================");
}

// ─────────────────────────────────────────────────────────────────────────
void loop() {
  // Reconnect WiFi if needed
  if (WiFi.status() != WL_CONNECTED) {
    connectToWiFi();
  }

  // Handle Hardware Inputs
  readButton();
  readKeypad();

  // Poll Backend periodically
  if (millis() - lastPollTime >= pollInterval) {
    lastPollTime = millis();
    pollBackend();
  }
}

// ── Hardware Input Handling ───────────────────────────────────────────────
void readButton() {
  bool reading = digitalRead(BUTTON_PIN);
  
  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }
  
  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading == LOW && !buttonPressed) {
      buttonPressed = true;
      if (currentState == IDLE) {
        Serial.println("[ESP] Doorbell pressed!");
        updateLCD("Visitor at Door", "Waiting for App");
        publishState("SOMEONE_AT_DOOR"); 
      }
    } else if (reading == HIGH) {
      buttonPressed = false;
    }
  }
  lastButtonState = reading;
}

void readKeypad() {
  char key = keypad.getKey();
  
  if (key) {
    if (currentState == WAITING_FOR_OTP) {
      if (key >= '0' && key <= '9') {
        enteredOTP += key;
        
        // Show masked OTP (optional: or show digits)
        lcd.setCursor(0, 1);
        lcd.print(enteredOTP);
        
        if (enteredOTP.length() == 4) {
          updateLCD("Verifying...", "");
          submitOTP(enteredOTP);
          enteredOTP = ""; // Reset for next time
        }
      } else if (key == '*') {
        // Clear entry
        enteredOTP = "";
        updateLCD("Enter OTP:", "");
      }
    }
  }
}

// ── Visual Output Handling ────────────────────────────────────────────────
void updateLCD(String line1, String line2) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(line2);
}

// ── Wi-Fi Connection ──────────────────────────────────────────────────────
void connectToWiFi() {
  Serial.println("Starting WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  updateLCD("Connecting WiFi", ssid);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\n[WiFi] Connected!");
  updateLCD("WiFi Connected!", WiFi.localIP().toString());
  delay(1500);
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
    updateLCD("Visitor at Door", "Waiting for App");
    publishState("SOMEONE_AT_DOOR");
  }
  else if (message == "WAITING_FOR_OTP") {
    Serial.println("[ESP] Server sent OTP via SMS. Awaiting Keypad input...");
    currentState = WAITING_FOR_OTP;
    enteredOTP = "";
    otpAttemptsLocal = 0; // Reset local attempts counter
    updateLCD("Enter OTP (4):", "");
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
        // the server will separately send 'UNLOCK' via pollQueue, but we can do it here too 
        // to be instantly responsive. Let's just wait for the UNLOCK poll or transition.
        updateLCD("OTP Verified!", "Unlocking...");
        currentState = IDLE; 
    } else if(response == "MAX_TRIES") {
        Serial.println("[ESP] 3 Invalid OTP attempts reached! Door stays Locked.");
        updateLCD("Max Tries!", "System Locked");
        currentState = IDLE;
        delay(2000);
        updateLCD("Secure & Locked", "Awaiting Visitor");
    } else {
        otpAttemptsLocal++;
        Serial.printf("[ESP] Invalid OTP! Attempt %d of 3. Try again.\n", otpAttemptsLocal);
        updateLCD("Wrong OTP!", "Try again:");
        enteredOTP = "";
        // Remain in WAITING_FOR_OTP state so user can enter again
    }
  } else {
    Serial.print("[ESP] Failed to submit OTP: ");
    Serial.println(http.errorToString(code));
    updateLCD("Network Error", "Try again");
    enteredOTP = "";
  }
  http.end();
}

// ── Door Control ──────────────────────────────────────────────────────────
void unlockDoor() {
  updateLCD("Door Unlocked", "You may enter");
  doorServo.write(UNLOCKED_ANGLE);
  Serial.println("[ESP] Door UNLOCKED");
  
  delay(5000);
  
  Serial.println("[ESP] Auto-locking door...");
  lockDoor();
}

void lockDoor() {
  doorServo.write(LOCKED_ANGLE);
  Serial.println("[ESP] Door LOCKED");
  updateLCD("Secure & Locked", "Awaiting Visitor");
}