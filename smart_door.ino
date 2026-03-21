#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ESP32Servo.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>

// ── Wi-Fi Credentials ─────────────────────────────────────────────────────
const char* ssid     = "samosa";
const char* password = "nitin@123";

// ── MQTT Broker (HiveMQ Public) ───────────────────────────────────────────
const char* mqtt_server = "broker.hivemq.com";
const int mqtt_port = 1883;

const char* TOPIC_STATE = "nitindubey/door/state";
const char* TOPIC_COMMAND = "nitindubey/door/command";

// ── API.OTP.DEV Credentials ───────────────────────────────────────────────
const String OTP_API_KEY = "95a549d2e50908342cf9bdc3b769a142";
const String OTP_PHONE = "918982207277";

// ── Servo Configuration ───────────────────────────────────────────────────
Servo doorServo;
const int SERVO_PIN      = 13;
const int LOCKED_ANGLE   = 0;
const int UNLOCKED_ANGLE = 90;

// ── Internal State ────────────────────────────────────────────────────────
enum SystemState { IDLE, WAITING_FOR_OTP };
SystemState currentState = IDLE;

// ── Objects ───────────────────────────────────────────────────────────────
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// ── Function Declarations ─────────────────────────────────────────────────
void connectToWiFi();
void connectToMQTT();
void requestOTP();
void verifyOTP(String otp);
void unlockDoor();
void lockDoor();
void publishState(const char* stateMsg);

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  
  Serial.print("[MQTT] Received command: ");
  Serial.println(message);

  if (message == "GENERATE_OTP") {
    requestOTP();
  } 
  else if (message == "LOCK") {
    lockDoor();
  }
  else if (message == "SIMULATE_BELL") {
    Serial.println("[ESP] Web commanded doorbell simulation.");
    publishState("SOMEONE_AT_DOOR");
  }
  else if (message.startsWith("KEYPAD:")) {
    String otpInput = message.substring(7); // "KEYPAD:1234" -> "1234"
    verifyOTP(otpInput);
  }
}

// ─────────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1000);

  doorServo.attach(SERVO_PIN);
  
  connectToWiFi();
  
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(mqttCallback);

  lockDoor();

  Serial.println("==========================================");
  Serial.println("Smart Door MQTT System Initialized");
  Serial.println("Type 'hi' and press Enter to trigger doorbell manually");
  Serial.println("==========================================");
}

// ─────────────────────────────────────────────────────────────────────────
void loop() {
  if (!mqttClient.connected()) {
    connectToMQTT();
  }
  mqttClient.loop();

  // Read Serial Monitor for manual ESP32 interaction
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim();

    if (input.length() > 0) {
      if (currentState == IDLE) {
        if (input.equalsIgnoreCase("hi")) {
          Serial.println("[ESP] Doorbell triggered locally! Publishing SOMEONE_AT_DOOR...");
          publishState("SOMEONE_AT_DOOR"); // Alerts the web dashboard
        } else {
          Serial.println("[ESP] Unknown command. Type 'hi' to ring doorbell.");
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

void connectToMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("Connecting to HiveMQ MQTT Broker...");
    // Create a random client ID
    String clientId = "ESP32Door_" + String(random(0xffff), HEX);
    
    // Attempt to connect (HiveMQ public broker needs no auth)
    if (mqttClient.connect(clientId.c_str())) {
      Serial.println(" connected!");
      mqttClient.subscribe(TOPIC_COMMAND); 
      publishState("LOCKED"); // Ensure dashboard knows we are online
    } else {
      Serial.print(" failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void publishState(const char* stateMsg) {
  if (mqttClient.connected()) {
    mqttClient.publish(TOPIC_STATE, stateMsg);
  }
}

// ── Request OTP from server ───────────────────────────────────────────────
void requestOTP() {
  if (WiFi.status() != WL_CONNECTED) return;

  Serial.println("[ESP] Calling api.otp.dev to send SMS...");
  publishState("OTP_GENERATED"); // Tell web to show "Sent" UI immediately

  WiFiClientSecure secureClient;
  secureClient.setInsecure();
  HTTPClient http;
  
  http.begin(secureClient, "https://api.otp.dev/v1/verifications");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("accept", "application/json");
  http.addHeader("X-OTP-Key", OTP_API_KEY.c_str());

  // Manually construct JSON to avoid dynamic memory limits
  String payload = "{\"data\":{\"channel\":\"sms\",\"sender\":\"896ee6b2-2a0b-4922-bbd9-39cafc99140b\",\"phone\":\"" + OTP_PHONE + "\",\"template\":\"a7fe47ae-ccc8-4ffb-adaa-15d557704036\",\"code_length\":4}}";
  
  int code = http.POST(payload);

  if (code > 0) {
    if (code == 200 || code == 201) {
      Serial.println("[ESP] SMS Sent successfully via external API!");
      currentState = WAITING_FOR_OTP;
    } else {
      Serial.print("[ESP] API error: HTTP ");
      Serial.println(code);
      Serial.println(http.getString());
      publishState("LOCKED"); // Revert UI
    }
  } else {
    Serial.print("[ESP] Request failed: ");
    Serial.println(http.errorToString(code));
    publishState("LOCKED"); // Revert UI
  }

  http.end();
}

// ── Verify OTP entered on keypad ──────────────────────────────────────────
void verifyOTP(String otp) {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure secureClient;
  secureClient.setInsecure();
  HTTPClient http;
  
  String url = "https://api.otp.dev/v1/verifications?code=" + otp + "&phone=" + OTP_PHONE;
  http.begin(secureClient, url);
  http.addHeader("accept", "application/json");
  http.addHeader("X-OTP-Key", OTP_API_KEY.c_str());

  int code = http.GET();

  if (code > 0) {
    String response = http.getString();
    
    // Check if the JSON returned actual verification items
    if (code == 200 && response.indexOf("message_id") > 0) {
      Serial.println("[ESP] OTP correct! Unlocking door...");
      unlockDoor();
    } else {
      Serial.println("[ESP] Wrong OTP! Response: " + response);
      publishState("SOMEONE_AT_DOOR"); // Reset dashboard
    }
  } else {
    Serial.println("[ESP] Verification request failed");
  }

  currentState = IDLE;
  http.end();
}

// ── Door Control ──────────────────────────────────────────────────────────
void unlockDoor() {
  doorServo.write(UNLOCKED_ANGLE);
  Serial.println("[ESP] Door UNLOCKED");
  publishState("UNLOCKED");
  
  delay(5000);
  
  Serial.println("[ESP] Auto-locking door...");
  lockDoor();
}

void lockDoor() {
  doorServo.write(LOCKED_ANGLE);
  Serial.println("[ESP] Door LOCKED");
  publishState("LOCKED");
}