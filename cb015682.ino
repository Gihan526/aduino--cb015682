#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP32Servo.h>

// =====================================================
// PIN CONNECTIONS
// =====================================================

// OLED - 7 pin SPI
#define OLED_SDA    23
#define OLED_SCL    18
#define OLED_DC     17
#define OLED_CS     5
#define OLED_RESET  16

// Components
#define SERVO_PIN   13
#define BUZZER_PIN  14
#define PIR_PIN     25
#define ECHO_PIN    26
#define TRIG_PIN    27
#define POT_PIN     34


// =====================================================
// SETTINGS
// =====================================================

const int CLOSED_ANGLE = 0;
const int OPEN_ANGLE = 90;

// Safety threshold
const float SAFETY_DISTANCE = 20;

// PIR warm-up
const unsigned long PIR_WARMUP_TIME = 30000;

// Potentiometer controls 2 - 10 seconds
const unsigned long MIN_HOLD_TIME = 2000;
const unsigned long MAX_HOLD_TIME = 10000;

// Obstruction must stay clear for 2 seconds
const unsigned long CLEAR_TIME = 2000;


// =====================================================
// OLED
// =====================================================

Adafruit_SSD1306 display(
  128,
  64,
  &SPI,
  OLED_DC,
  OLED_RESET,
  OLED_CS
);


// =====================================================
// SERVO
// =====================================================

Servo gateServo;

int servoAngle = CLOSED_ANGLE;


// =====================================================
// SYSTEM MODES
// =====================================================

enum SystemMode {
  AUTO_MODE,
  MANUAL_MODE,
  SAFETY_MODE
};

SystemMode mode = AUTO_MODE;


// =====================================================
// GATE STATES
// =====================================================

enum GateState {
  GATE_CLOSED,
  GATE_OPENING,
  GATE_OPEN,
  GATE_CLOSING,
  GATE_STOPPED
};

GateState gateState = GATE_CLOSED;


// =====================================================
// VARIABLES
// =====================================================

bool pirMotion = false;

float distance = -1;

int potValue = 0;

unsigned long holdTime = 5000;

unsigned long gateOpenTime = 0;

unsigned long lastServoMove = 0;

unsigned long lastDistanceRead = 0;

unsigned long lastOLEDUpdate = 0;

unsigned long safetyClearStart = 0;

String command = "";


// =====================================================
// SETUP
// =====================================================

void setup() {

  // Serial Monitor
  Serial.begin(9600);


  // -----------------------------
  // COMPONENT PINS
  // -----------------------------

  pinMode(PIR_PIN, INPUT);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  pinMode(BUZZER_PIN, OUTPUT);

  digitalWrite(BUZZER_PIN, LOW);


  // -----------------------------
  // POTENTIOMETER
  // -----------------------------

  analogReadResolution(12);


  // -----------------------------
  // SERVO
  // -----------------------------

  gateServo.setPeriodHertz(50);

  gateServo.attach(
    SERVO_PIN,
    500,
    2400
  );

  gateServo.write(CLOSED_ANGLE);

  servoAngle = CLOSED_ANGLE;


  // -----------------------------
  // OLED
  // -----------------------------

  SPI.begin(
    OLED_SCL,
    -1,
    OLED_SDA,
    OLED_CS
  );

  if (!display.begin(SSD1306_SWITCHCAPVCC)) {

    Serial.println("OLED ERROR");

    while (true) {
    }
  }

  display.clearDisplay();

  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);

  display.setCursor(0, 10);

  display.println("INDUSTRIAL");
  display.println("SAFETY GATE");
  display.println();
  display.println("Starting...");

  display.display();


  // -----------------------------
  // SERIAL INFO
  // -----------------------------

  Serial.println();
  Serial.println("=======================");
  Serial.println(" INDUSTRIAL SAFETY GATE");
  Serial.println("=======================");
  Serial.println();

  Serial.println("AUTO MODE");
  Serial.println("PIR warming up...");
  Serial.println();

  Serial.println("Commands:");
  Serial.println("AUTO");
  Serial.println("MANUAL");
  Serial.println("OPEN");
  Serial.println("CLOSE");
  Serial.println("STATUS");
  Serial.println("HELP");
}


// =====================================================
// MAIN LOOP
// =====================================================

void loop() {

  unsigned long now = millis();

  readSensors(now);

  readSerial();

  // Run current mode
  if (mode == AUTO_MODE) {

    runAutoMode(now);
  }

  else if (mode == MANUAL_MODE) {

    runManualMode(now);
  }

  else if (mode == SAFETY_MODE) {

    runSafetyMode(now);
  }

  moveServo(now);

  updateBuzzer(now);

  updateOLED(now);
}


// =====================================================
// READ SENSORS
// =====================================================

void readSensors(unsigned long now) {

  // -----------------------------
  // PIR
  // -----------------------------

  pirMotion = digitalRead(PIR_PIN);


  // -----------------------------
  // POTENTIOMETER
  // -----------------------------

  potValue = analogRead(POT_PIN);

  holdTime = map(
    potValue,
    0,
    4095,
    MIN_HOLD_TIME,
    MAX_HOLD_TIME
  );


  // -----------------------------
  // ULTRASONIC
  // -----------------------------

  // Read every 100ms

  if (now - lastDistanceRead >= 100) {

    lastDistanceRead = now;

    distance = getDistance();
  }
}


// =====================================================
// ULTRASONIC DISTANCE
// =====================================================

float getDistance() {

  digitalWrite(TRIG_PIN, LOW);

  delayMicroseconds(2);

  digitalWrite(TRIG_PIN, HIGH);

  delayMicroseconds(10);

  digitalWrite(TRIG_PIN, LOW);


  long duration =
    pulseIn(ECHO_PIN, HIGH, 25000);


  // No echo
  if (duration == 0) {

    return -1;
  }


  float cm =
    duration * 0.0343 / 2.0;

  return cm;
}


// =====================================================
// AUTO MODE
// =====================================================

void runAutoMode(unsigned long now) {

  // Wait for PIR to stabilise after startup
  if (now < PIR_WARMUP_TIME) {

    return;
  }


  // ===================================================
  // GATE CLOSED
  // ===================================================

  if (gateState == GATE_CLOSED) {

    // Motion detected -> open gate

    if (pirMotion == HIGH) {

      Serial.println("MOTION DETECTED");

      openGate();
    }
  }


  // ===================================================
  // GATE OPEN
  // ===================================================

  else if (gateState == GATE_OPEN) {

    // Still detecting movement?
    // Keep gate open.

    if (pirMotion == HIGH) {

      gateOpenTime = now;
    }

    // No motion
    else {

      // Wait according to potentiometer

      if (now - gateOpenTime >= holdTime) {

        Serial.println("Hold time finished");

        closeGate();
      }
    }
  }


  // ===================================================
  // GATE CLOSING
  // ===================================================

  else if (gateState == GATE_CLOSING) {

    // FIRST: check ultrasonic safety

    if (
      distance > 0 &&
      distance < SAFETY_DISTANCE
    ) {

      activateSafety();

      return;
    }


    // Motion appears while gate is closing

    if (pirMotion == HIGH) {

      Serial.println("Motion while closing");

      Serial.println("Reopening gate");

      openGate();
    }
  }
}


// =====================================================
// MANUAL MODE
// =====================================================

void runManualMode(unsigned long now) {

  // Safety still works in manual mode

  if (gateState == GATE_CLOSING) {

    if (
      distance > 0 &&
      distance < SAFETY_DISTANCE
    ) {

      activateSafety();
    }
  }
}


// =====================================================
// SAFETY MODE
// =====================================================

void runSafetyMode(unsigned long now) {

  // Object has moved away

  if (distance >= SAFETY_DISTANCE) {

    // Start clear timer

    if (safetyClearStart == 0) {

      safetyClearStart = now;
    }


    // Clear for 2 seconds

    if (
      now - safetyClearStart
      >= CLEAR_TIME
    ) {

      Serial.println("PATH CLEAR");
      Serial.println("REOPENING GATE");

      safetyClearStart = 0;

      mode = AUTO_MODE;

      openGate();
    }
  }

  else {

    // Still blocked

    safetyClearStart = 0;
  }
}


// =====================================================
// OPEN GATE
// =====================================================

void openGate() {

  // Turn alarm off

  digitalWrite(
    BUZZER_PIN,
    LOW
  );

  gateState = GATE_OPENING;

  Serial.println("Gate opening...");
}


// =====================================================
// CLOSE GATE
// =====================================================

void closeGate() {

  gateState = GATE_CLOSING;

  Serial.println("Gate closing...");
}


// =====================================================
// SAFETY
// =====================================================

void activateSafety() {

  mode = SAFETY_MODE;

  // Stops servo exactly where it is
  gateState = GATE_STOPPED;

  safetyClearStart = 0;

  Serial.println();
  Serial.println("!!! SAFETY ALERT !!!");

  Serial.println("OBSTRUCTION DETECTED");

  Serial.print("Distance: ");
  Serial.print(distance);
  Serial.println(" cm");

  Serial.println("GATE STOPPED");
}


// =====================================================
// SERVO MOVEMENT
// =====================================================

void moveServo(unsigned long now) {

  // Servo moves every 20ms

  if (now - lastServoMove < 20) {

    return;
  }

  lastServoMove = now;


  // ===================================================
  // OPENING
  // ===================================================

  if (gateState == GATE_OPENING) {

    if (servoAngle < OPEN_ANGLE) {

      servoAngle++;

      gateServo.write(
        servoAngle
      );
    }

    else {

      gateState = GATE_OPEN;

      gateOpenTime = now;

      Serial.println("Gate OPEN");
    }
  }


  // ===================================================
  // CLOSING
  // ===================================================

  else if (gateState == GATE_CLOSING) {

    if (servoAngle > CLOSED_ANGLE) {

      servoAngle--;

      gateServo.write(
        servoAngle
      );
    }

    else {

      gateState = GATE_CLOSED;

      Serial.println("Gate CLOSED");
    }
  }


  // If GATE_STOPPED:
  // servo doesn't move
}


// =====================================================
// BUZZER
// =====================================================

void updateBuzzer(unsigned long now) {

  // Buzzer only works in SAFETY mode

  if (mode != SAFETY_MODE) {

    digitalWrite(
      BUZZER_PIN,
      LOW
    );

    return;
  }


  // Chirp like a warning alarm

  // 150ms ON every 500ms

  if ((now % 500) < 150) {

    digitalWrite(
      BUZZER_PIN,
      HIGH
    );
  }

  else {

    digitalWrite(
      BUZZER_PIN,
      LOW
    );
  }
}


// =====================================================
// OLED
// =====================================================

void updateOLED(unsigned long now) {

  // Update every 200ms

  if (now - lastOLEDUpdate < 200) {

    return;
  }

  lastOLEDUpdate = now;

  display.clearDisplay();

  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);


  // ===================================================
  // PIR WARMUP SCREEN
  // ===================================================

  if (
    mode == AUTO_MODE &&
    now < PIR_WARMUP_TIME
  ) {

    display.setTextSize(1);

    display.println("PIR WARMING UP");
    display.println();

    display.print("Wait: ");

    display.print(
      (PIR_WARMUP_TIME - now) / 1000
    );

    display.println(" sec");

    display.println();
    display.println("Gate locked");

    display.display();

    return;
  }


  // ===================================================
  // SAFETY SCREEN
  // ===================================================

  if (mode == SAFETY_MODE) {

    display.setTextSize(2);

    display.println("SAFETY!");

    display.setTextSize(1);

    display.println("OBSTRUCTION");

    display.print("Distance: ");

    display.print(
      distance,
      1
    );

    display.println("cm");

    display.println("GATE STOPPED");

    display.println("ALARM ACTIVE");

    display.display();

    return;
  }


  // ===================================================
  // NORMAL SCREEN
  // ===================================================

  display.setTextSize(1);


  // MODE

  display.print("MODE: ");

  if (mode == AUTO_MODE) {

    display.println("AUTO");
  }

  else {

    display.println("MANUAL");
  }


  // GATE

  display.print("GATE: ");

  if (gateState == GATE_CLOSED) {

    display.println("CLOSED");
  }

  else if (gateState == GATE_OPENING) {

    display.println("OPENING");
  }

  else if (gateState == GATE_OPEN) {

    display.println("OPEN");
  }

  else if (gateState == GATE_CLOSING) {

    display.println("CLOSING");
  }

  else {

    display.println("STOPPED");
  }


  // DISTANCE

  display.print("DIST: ");

  if (distance < 0) {

    display.println("--");
  }

  else {

    display.print(
      distance,
      1
    );

    display.println("cm");
  }


  // HOLD TIME

  display.print("HOLD: ");

  display.print(
    holdTime / 1000.0,
    1
  );

  display.println("s");


  // PIR

  display.print("PIR: ");

  if (pirMotion == HIGH) {

    display.println("MOTION");
  }

  else {

    display.println("CLEAR");
  }


  display.display();
}


// =====================================================
// READ UART
// =====================================================

void readSerial() {

  while (Serial.available()) {

    char c =
      Serial.read();


    if (
      c == '\n' ||
      c == '\r'
    ) {

      if (command.length() > 0) {

        processCommand();

        command = "";
      }
    }

    else {

      command += c;
    }
  }
}


// =====================================================
// UART COMMANDS
// =====================================================

void processCommand() {

  command.trim();

  command.toUpperCase();


  // AUTO

  if (command == "AUTO") {

    if (mode == SAFETY_MODE) {

      Serial.println(
        "Cannot leave SAFETY yet"
      );

      return;
    }

    mode = AUTO_MODE;

    Serial.println("AUTO MODE");
  }


  // MANUAL

  else if (command == "MANUAL") {

    if (mode == SAFETY_MODE) {

      Serial.println(
        "Cannot leave SAFETY yet"
      );

      return;
    }

    mode = MANUAL_MODE;

    Serial.println("MANUAL MODE");
  }


  // OPEN

  else if (command == "OPEN") {

    if (mode != MANUAL_MODE) {

      Serial.println(
        "Use MANUAL first"
      );

      return;
    }

    openGate();
  }


  // CLOSE

  else if (command == "CLOSE") {

    if (mode != MANUAL_MODE) {

      Serial.println(
        "Use MANUAL first"
      );

      return;
    }


    // Check obstacle before closing

    if (
      distance > 0 &&
      distance < SAFETY_DISTANCE
    ) {

      Serial.println(
        "Cannot close"
      );

      Serial.println(
        "Obstacle detected"
      );

      activateSafety();

      return;
    }


    closeGate();
  }


  // STATUS

  else if (command == "STATUS") {

    printStatus();
  }


  // HELP

  else if (command == "HELP") {

    Serial.println();
    Serial.println("COMMANDS:");

    Serial.println("AUTO");
    Serial.println("MANUAL");
    Serial.println("OPEN");
    Serial.println("CLOSE");
    Serial.println("STATUS");
    Serial.println("HELP");
  }


  else {

    Serial.println(
      "Unknown command"
    );
  }
}


// =====================================================
// STATUS
// =====================================================

void printStatus() {

  Serial.println();
  Serial.println("----- STATUS -----");


  Serial.print("Mode: ");

  if (mode == AUTO_MODE) {

    Serial.println("AUTO");
  }

  else if (mode == MANUAL_MODE) {

    Serial.println("MANUAL");
  }

  else {

    Serial.println("SAFETY");
  }


  Serial.print("PIR: ");

  if (pirMotion) {

    Serial.println("MOTION");
  }

  else {

    Serial.println("CLEAR");
  }


  Serial.print("Distance: ");

  Serial.print(distance);

  Serial.println(" cm");


  Serial.print("Pot value: ");

  Serial.println(potValue);


  Serial.print("Hold time: ");

  Serial.print(
    holdTime / 1000.0
  );

  Serial.println(" sec");


  Serial.print("Servo angle: ");

  Serial.println(
    servoAngle
  );


  Serial.println("------------------");
}