#include <WiFi.h>
#include <WebServer.h>
#include <Keypad.h>
#include <ESP32Servo.h>

// ── WiFi Credentials ──────────────────────────────────────────
const char* ssid     = "Ridoy";
const char* password = "ridoysaha";

// ── Constants ─────────────────────────────────────────────────
const int DEG_PER_ML = 18;      // 1 mL = 18 degree
const int MAX_ML     = 10;      // Maximum 10 mL
const int SWEEP_MS   = 15;      // Servo speed

// ── Keypad ────────────────────────────────────────────────────
const byte ROWS = 4;
const byte COLS = 4;

char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};

byte rowPins[ROWS] = {13, 12, 14, 27};
byte colPins[COLS] = {26, 25, 33, 32};

Keypad keypad = Keypad(
  makeKeymap(keys),
  rowPins,
  colPins,
  ROWS,
  COLS
);

// ── Servo ─────────────────────────────────────────────────────
const int SERVO_PIN = 18;
Servo servoMotor;

// ── Web Server ────────────────────────────────────────────────
WebServer server(80);

// ── Variables ────────────────────────────────────────────────
char input[6] = "";
int inputIndex = 0;

int anesthesiaAmount = 0;
int lastAmount = 0;
int currentAngle = 0;

bool isInjecting = false;
String injectorStatus = "Idle";

// ── Function Declaration ─────────────────────────────────────
void connectWiFi();
void processInput(char key);
void sweepServo(int fromDeg, int toDeg);
void injectAnesthesia(int amount);
void reverseInjection();
void clearInput();

// Web handlers
void handleRoot();
void handleData();
void handleInject();
void handleReverse();
void handleStatus();
void handleStop();

// ─────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);

  // ── WiFi Connect ───────────────────────────────────────────
  connectWiFi();

  // ── Servo Setup ────────────────────────────────────────────
  ESP32PWM::allocateTimer(0);
  servoMotor.setPeriodHertz(50);
  servoMotor.attach(SERVO_PIN, 500, 2400);
  servoMotor.write(0);

  // ── Web Server Routes ──────────────────────────────────────
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/status", handleStatus);
  server.on("/inject", handleInject);     // /inject?amount=5
  server.on("/reverse", handleReverse);
  server.on("/stop", handleStop);
  server.begin();

  Serial.println("================================");
  Serial.println("  Anesthesia Injector Ready");
  Serial.println("================================");

  Serial.println("Manual keypad control:");
  Serial.println("3 key replaced by A");
  Serial.println("6 key replaced by C");
  Serial.println("9 key replaced by D");
  Serial.println("# = Inject");
  Serial.println("B = Reverse");
  Serial.println("* = Clear");

  Serial.println("Web control:");
  Serial.println("/data");
  Serial.println("/status");
  Serial.println("/inject?amount=5");
  Serial.println("/reverse");
  Serial.println("/stop");
}

// ─────────────────────────────────────────────────────────────
void loop() {
  server.handleClient();

  char key = keypad.getKey();

  if (key) {
    // Replace faulty keys
    if (key == 'A') {
      key = '3';
    } else if (key == 'C') {
      key = '6';
    } else if (key == 'D') {
      key = '9';
    }

    Serial.printf("Key Pressed: %c\n", key);
    processInput(key);
  }
}

// ─────────────────────────────────────────────────────────────
// WiFi Connection Function
void connectWiFi() {
  Serial.println();
  Serial.println("Connecting to WiFi...");

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi Connected Successfully!");
  Serial.print("ESP32 IP Address: ");
  Serial.println(WiFi.localIP());
}

// ─────────────────────────────────────────────────────────────
void processInput(char key) {
  // Inject
  if (key == '#') {
    if (anesthesiaAmount > 0) {
      injectAnesthesia(anesthesiaAmount);
      clearInput();
    } else {
      Serial.println(">> Enter amount first");
    }
  }

  // Reverse
  else if (key == 'B') {
    reverseInjection();
  }

  // Clear
  else if (key == '*') {
    clearInput();
    Serial.println(">> Input Cleared");
  }

  // Numeric Input
  else if (key >= '0' && key <= '9') {
    if (inputIndex < 5) {
      input[inputIndex++] = key;
      input[inputIndex] = '\0';

      anesthesiaAmount = atoi(input);

      if (anesthesiaAmount > MAX_ML) {
        Serial.printf(">> WARNING: Max %d mL allowed\n", MAX_ML);
      } else {
        Serial.printf(">> Amount Entered: %d mL\n", anesthesiaAmount);
      }
    } else {
      Serial.println(">> Maximum 5 digits allowed");
    }
  }

  delay(150);
}

// ─────────────────────────────────────────────────────────────
// Smooth Servo Movement
void sweepServo(int fromDeg, int toDeg) {
  if (fromDeg < toDeg) {
    for (int angle = fromDeg; angle <= toDeg; angle++) {
      servoMotor.write(angle);
      delay(SWEEP_MS);
    }
  } else {
    for (int angle = fromDeg; angle >= toDeg; angle--) {
      servoMotor.write(angle);
      delay(SWEEP_MS);
    }
  }

  currentAngle = toDeg;
}

// ─────────────────────────────────────────────────────────────
// Injection Function
void injectAnesthesia(int amount) {
  if (amount > MAX_ML) {
    amount = MAX_ML;
  }

  int targetDeg = amount * DEG_PER_ML;

  isInjecting = true;
  injectorStatus = "Injecting";

  Serial.printf(">> Injecting %d mL\n", amount);
  Serial.printf(">> Rotating Servo: %d° → %d°\n", currentAngle, targetDeg);

  // Move forward only
  sweepServo(currentAngle, targetDeg);

  Serial.printf(">> Servo stopped at %d°\n", targetDeg);
  Serial.println(">> Press B to reverse");

  lastAmount = amount;

  isInjecting = false;
  injectorStatus = "Idle";
}

// ─────────────────────────────────────────────────────────────
// Reverse Function
void reverseInjection() {
  if (lastAmount == 0) {
    Serial.println(">> Nothing to reverse");
    return;
  }

  isInjecting = true;
  injectorStatus = "Reversing";

  int targetDeg = lastAmount * DEG_PER_ML;

  Serial.printf(">> Reversing %d mL\n", lastAmount);
  Serial.printf(">> Rotating Servo: %d° → 0°\n", targetDeg);

  // Reverse back to 0 degree
  sweepServo(targetDeg, 0);

  Serial.println(">> Returned to Home Position");

  lastAmount = 0;

  isInjecting = false;
  injectorStatus = "Idle";
}

// ─────────────────────────────────────────────────────────────
void clearInput() {
  anesthesiaAmount = 0;
  inputIndex = 0;
  input[0] = '\0';
}

// ─────────────────────────────────────────────────────────────
// Web Handlers
void handleRoot() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "text/plain", "ESP32 Anesthesia Injector Server is running");
}

void handleData() {
  String json = "{";
  json += "\"status\":\"" + injectorStatus + "\",";
  json += "\"injecting\":" + String(isInjecting ? "true" : "false") + ",";
  json += "\"lastAmount\":" + String(lastAmount) + ",";
  json += "\"currentAngle\":" + String(currentAngle) + ",";
  json += "\"selectedAmount\":" + String(anesthesiaAmount) + ",";
  json += "\"wifi\":\"" + String(WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected") + "\",";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\"";
  json += "}";

  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);
}

void handleStatus() {
  handleData();
}

void handleInject() {
  if (!server.hasArg("amount")) {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(400, "text/plain", "Missing amount parameter");
    return;
  }

  int amount = server.arg("amount").toInt();

  if (amount <= 0 || amount > MAX_ML) {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(400, "text/plain", "Invalid amount");
    return;
  }

  Serial.printf(">> Web inject request: %d mL\n", amount);

  injectAnesthesia(amount);

  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "text/plain", "Injection complete");
}

void handleReverse() {
  Serial.println(">> Web reverse request");
  reverseInjection();

  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "text/plain", "Reversed");
}

void handleStop() {
  Serial.println(">> Web stop request");

  injectorStatus = "Stopped";
  isInjecting = false;

  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "text/plain", "Stopped");
}