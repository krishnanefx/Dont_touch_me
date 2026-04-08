#include <Wire.h>
#include <Motoron.h>
#include <Adafruit_MMA8451.h>
#include <Adafruit_Sensor.h>

// === Tunable constants ===
const int   MOTOR_SPEED        = 600;
const int   PROXIMITY_THRESHOLD = 30;        // cm
const int   CALIBRATION_OBSTACLE = 20;       // cm - fwd/rev threshold
const float SHAKE_THRESHOLD     = 6.0;       // m/s^2 deviation from gravity
const int   SHAKE_COUNT_REQUIRED = 3;
const float GRAVITY             = 9.81;
const float SOUND_THRESHOLD_FACTOR = 1.50;   // 150% of background
const unsigned long CALIBRATION_DURATION = 5000;
const unsigned long STATE_TIMEOUT = 5000;
const unsigned long SCAN_PAUSE_MS = 300;     // pause at each 15deg
const unsigned long SOUND_LOOP_MS = 1000;    // retrigger audio in FLIGHT

// Physical params
const float WHEEL_RADIUS_CM       = 3.2;
const float WHEELBASE_WIDTH_CM    = 17.0;
const int   ENCODER_COUNTS_PER_REV = 144;

// encoder counts for 15deg robot spin:
//   ((15/360) * pi * W) / (2*pi*r) * CPR
//   = ((15/360)*pi*17) / (2*pi*3.2) * 144 ≈ 8
const int COUNTS_PER_15_DEGREES = 8;  

// ---- Pin assignments ----
MotoronI2C mc1(16);   // shield 1 - left
MotoronI2C mc2(17);   // shield 2 - right
Adafruit_MMA8451 mma = Adafruit_MMA8451();

const int trigPin = 11;
const int echoPin = 12;
const int micEnvelopePin = A0;
const int nextPin = 10;       // DFPlayer IO2
const int stopPin = 8;        // DFPlayer IO1
const int encoder1Pin = 2;    // left encoder A
const int encoder2Pin = 3;    // right encoder A
const int timerEnablePin = 13;  // 555 bistable
const int killSwitchPin = 4;    // button to GND, internal pullup

// i2c pins for bus recovery
const int SDA_PIN = A4;
const int SCL_PIN = A5;

// ----- State machine -----
enum State { CALIBRATION, ANXIETY, FEAR, FLIGHT, KILLED };
State currentState = CALIBRATION;

bool wasKilled = false;

int backgroundVolume = 0;
int soundThreshold = 0;
unsigned long stateStartTime = 0;

// encoder vars
volatile long encoderCount1 = 0;
volatile long encoderCount2 = 0;

bool accelAvailable = false;

// shake detection
int shakeConsecutiveCount = 0;

// fear scan sub-state
bool scanPaused = false;
unsigned long scanPauseStart = 0;

// audio loop
unsigned long lastSoundTrigger = 0;


// --- I2C bus recovery ---
// if a device holds SDA low we clock SCL manually to unstick it
void recoverI2C() {
  Serial.println("Attempting I2C bus recovery...");
  
  Wire.end();
  pinMode(SDA_PIN, INPUT_PULLUP);
  pinMode(SCL_PIN, OUTPUT);
  
  for (int i = 0; i < 9; i++) {
    digitalWrite(SCL_PIN, LOW);
    delayMicroseconds(5);
    digitalWrite(SCL_PIN, HIGH);
    delayMicroseconds(5);
    
    if (digitalRead(SDA_PIN) == HIGH) {
      Serial.print("  SDA released after ");
      Serial.print(i + 1);
      Serial.println(" clocks");
      break;
    }
  }
  
  // STOP condition: SDA low->high while SCL high
  pinMode(SDA_PIN, OUTPUT);
  digitalWrite(SDA_PIN, LOW);
  delayMicroseconds(5);
  digitalWrite(SCL_PIN, HIGH);
  delayMicroseconds(5);
  digitalWrite(SDA_PIN, HIGH);
  delayMicroseconds(5);
  
  Wire.begin();
  delay(100);
  Serial.println("I2C bus recovery complete.");
}


void scanI2C() {
  Serial.println("Scanning I2C bus...");
  int deviceCount = 0;
  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    byte error = Wire.endTransmission();
    if (error == 0) {
      deviceCount++;
      Serial.print("  I2C device at 0x");
      if (addr < 16) Serial.print("0");
      Serial.print(addr, HEX);
      if (addr == 0x1D) Serial.print(" (MMA8451)");
      if (addr == 16)   Serial.print(" (Motoron Shield 1)");
      if (addr == 17)   Serial.print(" (Motoron Shield 2)");
      Serial.println();
    }
  }
  if (deviceCount == 0) {
    Serial.println("  WARNING: No I2C devices found!");
  } else {
    Serial.print("  Found ");
    Serial.print(deviceCount);
    Serial.println(" device(s)");
  }
}


void setup() {
  Serial.begin(9600);
  delay(500);
  Serial.println("\n=== DON'T TOUCH ME BOT STARTING ===\n");

  Wire.begin();

  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(nextPin, OUTPUT);
  digitalWrite(nextPin, HIGH);
  pinMode(stopPin, OUTPUT);
  digitalWrite(stopPin, HIGH);

  pinMode(timerEnablePin, OUTPUT);
  digitalWrite(timerEnablePin, LOW);

  pinMode(killSwitchPin, INPUT_PULLUP);

  // encoder interrupts
  pinMode(encoder1Pin, INPUT_PULLUP);
  pinMode(encoder2Pin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(encoder1Pin), countEncoder1, RISING);
  attachInterrupt(digitalPinToInterrupt(encoder2Pin), countEncoder2, RISING);

  // --- I2C init (order matters!) ---

  scanI2C();

  // accel first since it's most likely to hang the bus
  Serial.println("Initializing accelerometer...");
  accelAvailable = mma.begin();
  if (!accelAvailable) {
    Serial.println("MMA8451 not found! Recovering I2C bus...");
    recoverI2C();
    
    accelAvailable = mma.begin();  // retry
    if (!accelAvailable) {
      Serial.println("MMA8451 still not found. Continuing without it.");
    } else {
      Serial.println("MMA8451 found on retry!");
      mma.setRange(MMA8451_RANGE_4_G);
    }
  } else {
    Serial.println("MMA8451 found!");
    mma.setRange(MMA8451_RANGE_4_G);
  }

  scanI2C();  // check bus is still healthy

  Serial.println("Initializing motors...");
  initMotors();
  Serial.println("Motors initialized.");

  scanI2C();  // final check

  currentState = CALIBRATION;
}


// ========== Main loop ==========
void loop() {
  // kill switch — highest priority
  if (digitalRead(killSwitchPin) == LOW) {
    if (currentState != KILLED) {
      Serial.println("\n*** KILL SWITCH PRESSED — ALL STOP ***");
      stopMotors();
      stopSound();
      delay(150);
      stopSound();          // double tap to be sure
      digitalWrite(timerEnablePin, LOW);
      currentState = KILLED;
      wasKilled = true;
    }
  }

  // shake detection (only if accel working and in an active state)
  if (accelAvailable && (currentState == ANXIETY || currentState == FEAR || currentState == FLIGHT)) {
    if (checkAccelerometer()) {
      shakeConsecutiveCount++;
    } else {
      shakeConsecutiveCount = 0;
    }

    if (shakeConsecutiveCount >= SHAKE_COUNT_REQUIRED && currentState != FLIGHT) {
      Serial.println("\n*** SHAKEN! Entering FLIGHT ***");
      playSound();
      lastSoundTrigger = millis();
      digitalWrite(timerEnablePin, HIGH);
      scanPaused = false;
      shakeConsecutiveCount = 0;
      currentState = FLIGHT;
      stateStartTime = millis();
    }
  }

  switch (currentState) {
    case CALIBRATION: runCalibration(); break;
    case ANXIETY:     runAnxietyState(); break;
    case FEAR:        runFearState(); break;
    case FLIGHT:      runFlightState(); break;
    case KILLED:      runKilledState(); break;
  }

  printDiagnostics();
}


// ------ CALIBRATION ------
void runCalibration() {
  Serial.println("Calibrating mic with motors running...");

  unsigned long startTime = millis();
  long sampleSum = 0;
  long sampleCount = 0;
  bool movingForward = true;

  setLeftMotors(MOTOR_SPEED);
  setRightMotors(MOTOR_SPEED);

  while (millis() - startTime < CALIBRATION_DURATION) {
    long dist = measureDistance();

    if (movingForward && dist > 0 && dist < CALIBRATION_OBSTACLE) {
      setLeftMotors(-MOTOR_SPEED);
      setRightMotors(-MOTOR_SPEED);
      movingForward = false;
    } else if (!movingForward && (dist >= CALIBRATION_OBSTACLE || dist <= 0)) {
      setLeftMotors(MOTOR_SPEED);
      setRightMotors(MOTOR_SPEED);
      movingForward = true;
    }

    sampleSum += analogRead(micEnvelopePin);
    sampleCount++;
    delay(10);
  }

  stopMotors();

  backgroundVolume = sampleSum / sampleCount;
  soundThreshold = (int)(backgroundVolume * SOUND_THRESHOLD_FACTOR);

  Serial.println("\n--- CALIBRATION COMPLETE ---");
  Serial.print("Background (with motors): "); Serial.println(backgroundVolume);
  Serial.print("Threshold (150%): "); Serial.println(soundThreshold);
  Serial.println("----------------------------\n");

  currentState = ANXIETY;
}


// ------ ANXIETY ------
void runAnxietyState() {
  stopMotors();

  int currentSound = analogRead(micEnvelopePin);

  if (currentSound > soundThreshold) {
    Serial.println("\n*** LOUD NOISE! Entering FEAR ***");
    encoderCount1 = 0;
    encoderCount2 = 0;
    scanPaused = false;
    shakeConsecutiveCount = 0;
    currentState = FEAR;
    stateStartTime = millis();
  }
}


// ------ FEAR ------
// spins in place, pausing every 15deg to check ultrasonic
void runFearState() {
  // if we're in a scan pause, wait then take a reading
  if (scanPaused) {
    setLeftMotors(0);
    setRightMotors(0);

    if (millis() - scanPauseStart >= SCAN_PAUSE_MS) {
      long distance = measureDistance();

      if (distance > 0 && distance < PROXIMITY_THRESHOLD) {
        Serial.println("\n*** OBJECT DETECTED! Entering FLIGHT ***");

        playSound();
        lastSoundTrigger = millis();
        digitalWrite(timerEnablePin, HIGH);

        scanPaused = false;
        shakeConsecutiveCount = 0;
        currentState = FLIGHT;
        stateStartTime = millis();
        return;
      }

      scanPaused = false;  // nothing found, keep spinning
    }
    return;
  }

  // spin in place (left fwd, right back)
  setLeftMotors(MOTOR_SPEED);
  setRightMotors(-MOTOR_SPEED);

  long avgCount = (encoderCount1 + encoderCount2) / 2;

  if (avgCount >= COUNTS_PER_15_DEGREES) {
    encoderCount1 = 0;
    encoderCount2 = 0;
    scanPaused = true;
    scanPauseStart = millis();
    return;
  }

  // timeout -> go back to listening
  if (millis() - stateStartTime > STATE_TIMEOUT) {
    Serial.println("\n*** Timeout. Returning to ANXIETY ***");
    stopMotors();
    scanPaused = false;
    currentState = ANXIETY;
  }
}


// ------ FLIGHT ------
// drive backwards and scream
void runFlightState() {
  setLeftMotors(-MOTOR_SPEED);
  setRightMotors(-MOTOR_SPEED);

  // keep retriggering audio so it loops
  if (millis() - lastSoundTrigger >= SOUND_LOOP_MS) {
    playSound();
    lastSoundTrigger = millis();
  }

  if (millis() - stateStartTime > STATE_TIMEOUT) {
    Serial.println("\n*** Flight timeout. Returning to FEAR (rescan) ***");

    stopMotors();
    stopSound();
    digitalWrite(timerEnablePin, LOW);

    encoderCount1 = 0;
    encoderCount2 = 0;
    scanPaused = false;
    shakeConsecutiveCount = 0;
    currentState = FEAR;
    stateStartTime = millis();
  }
}


// ------ KILLED (e-stop) ------
void runKilledState() {
  stopMotors();
  digitalWrite(timerEnablePin, LOW);

  // wait for button release before allowing re-press to restart
  if (wasKilled && digitalRead(killSwitchPin) == HIGH) {
    wasKilled = false;
  }

  if (!wasKilled && digitalRead(killSwitchPin) == LOW) {
    delay(50);  // debounce
    if (digitalRead(killSwitchPin) == LOW) {
      Serial.println("\n*** RESTARTING — Entering CALIBRATION ***\n");
      currentState = CALIBRATION;
      wasKilled = false;
    }
  }
}


// ===== Encoder ISRs =====
void countEncoder1() { encoderCount1++; }
void countEncoder2() { encoderCount2++; }


// ===== Ultrasonic =====
long measureDistance() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  long duration = pulseIn(echoPin, HIGH, 30000);
  if (duration == 0) return 999;
  return duration / 58;
}


// ===== Accelerometer =====
bool checkAccelerometer() {
  sensors_event_t event;
  mma.getEvent(&event);

  float totalAccel = sqrt(
    pow(event.acceleration.x, 2) +
    pow(event.acceleration.y, 2) +
    pow(event.acceleration.z, 2)
  );
  float deviation = abs(totalAccel - GRAVITY);
  return (deviation > SHAKE_THRESHOLD);
}


// ===== DFPlayer audio =====
void playSound() {
  digitalWrite(nextPin, LOW);
  delay(100);
  digitalWrite(nextPin, HIGH);
}

void stopSound() {
  digitalWrite(stopPin, LOW);
  delay(100);
  digitalWrite(stopPin, HIGH);
}


// ===== Motor helpers =====
void initMotors() {
  mc1.reinitialize();
  mc1.disableCrc();
  mc1.clearResetFlag();
  mc1.disableCommandTimeout();
  mc1.setMaxAcceleration(2, 200); mc1.setMaxDeceleration(2, 300);
  mc1.setMaxAcceleration(3, 200); mc1.setMaxDeceleration(3, 300);

  mc2.reinitialize();
  mc2.disableCrc();
  mc2.clearResetFlag();
  mc2.disableCommandTimeout();
  mc2.setMaxAcceleration(2, 200); mc2.setMaxDeceleration(2, 300);
  mc2.setMaxAcceleration(3, 200); mc2.setMaxDeceleration(3, 300);
}

void setLeftMotors(int speed) {
  mc1.setSpeed(2, speed);
  mc1.setSpeed(3, speed);
}

// right side is wired backwards so we negate
void setRightMotors(int speed) {
  mc2.setSpeed(2, -speed);
  mc2.setSpeed(3, -speed);
}

void stopMotors() {
  setLeftMotors(0);
  setRightMotors(0);
}


// ===== Diagnostics =====
void printDiagnostics() {
  static unsigned long lastPrintTime = 0;

  if (millis() - lastPrintTime > 250) {
    lastPrintTime = millis();

    int currentSound = analogRead(micEnvelopePin);

    long dist = -1;
    if (currentState != FEAR) {
      dist = measureDistance();
    }

    Serial.print("[");
    if      (currentState == CALIBRATION) Serial.print("CALIBRATING");
    else if (currentState == ANXIETY)     Serial.print("ANXIETY    ");
    else if (currentState == FEAR)        Serial.print("FEAR       ");
    else if (currentState == FLIGHT)      Serial.print("FLIGHT     ");
    else if (currentState == KILLED)      Serial.print("KILLED     ");
    Serial.print("] ");

    Serial.print("MIC: "); Serial.print(currentSound);
    Serial.print("/"); Serial.print(soundThreshold);

    Serial.print(" | ENC L:"); Serial.print(encoderCount1);
    Serial.print(" R:"); Serial.print(encoderCount2);
    Serial.print("/"); Serial.print(COUNTS_PER_15_DEGREES);

    if (dist >= 0) {
      Serial.print(" | DIST: "); Serial.print(dist); Serial.print("cm");
    }

    if (accelAvailable) {
      sensors_event_t event;
      mma.getEvent(&event);
      float totalAccel = sqrt(
        pow(event.acceleration.x, 2) +
        pow(event.acceleration.y, 2) +
        pow(event.acceleration.z, 2)
      );
      Serial.print(" | ACCEL: ");
      Serial.print(totalAccel); Serial.print(" (dev: ");
      Serial.print(abs(totalAccel - GRAVITY)); Serial.print(" shk: ");
      Serial.print(shakeConsecutiveCount); Serial.print("/");
      Serial.print(SHAKE_COUNT_REQUIRED); Serial.print(")");
    } else {
      Serial.print(" | ACCEL: N/A");
    }

    Serial.println();
  }
}
