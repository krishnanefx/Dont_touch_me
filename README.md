# Don't Touch Me Bot 🤖

An autonomous Arduino-based robot that *really* doesn't want to be touched. It listens, looks around, and runs away screaming when it detects you getting too close.

Built as part of COMP0209 coursework at UCL, in a team of three. I was the lead programmer.

[![Watch the robot run away (video)](https://img.youtube.com/vi/9Sp8UCt-aAA/hqdefault.jpg)](https://youtu.be/9Sp8UCt-aAA)

*The video we submitted for the coursework.*

## What it does

The robot runs a five-state behaviour machine:

```
CALIBRATION ──► ANXIETY ──(loud noise)──► FEAR ──(object < 30 cm)──► FLIGHT
                   ▲                       │  ▲                        │
                   └────────(timeout)──────┘  └────────(timeout)───────┘

ANXIETY / FEAR ──(shake)──► FLIGHT
any state ──(kill switch)──► KILLED ──(release, press again)──► CALIBRATION
```

1. **CALIBRATION** — Drives forward for 5 seconds (reversing whenever the ultrasonic sensor sees something closer than 20 cm), sampling the microphone every ~10 ms to learn the background noise level with the motors running. The sound threshold is set to 150% of that average.

2. **ANXIETY** — Sits still, listening. If it hears a loud noise above the calibrated threshold, it gets scared and enters FEAR.

3. **FEAR** — Spins in place in 15° increments, pausing at each step to take an ultrasonic distance reading. If it spots something within 30 cm, it panics and enters FLIGHT. If nothing is found within 5 seconds, it calms back down to ANXIETY.

4. **FLIGHT** — Drives backwards at `MOTOR_SPEED` while triggering audio on a DFPlayer Mini (re-triggered every second so it loops) and setting the 555 enable pin high. After 5 seconds it stops the motors and audio and goes back to FEAR to scan again.

5. **KILLED** — Emergency stop via a physical kill switch, checked at the top of every loop. Motors, audio and the 555 enable signal are stopped. Release the button and press it again to restart from CALIBRATION.

During ANXIETY or FEAR, picking up or shaking the robot (3 consecutive accelerometer readings deviating more than 6 m/s² from gravity) triggers an immediate jump to FLIGHT.

## Hardware

| Component | Role |
|---|---|
| Arduino Uno | Main controller |
| 2× Motoron I2C Motor Shield (addr 16, 17) | Dual motor control — left and right side |
| 4× DG01D-E DC motors with encoders | Differential drive (2 per side); one encoder channel A per side is read |
| HC-SR04 | Ultrasonic distance sensor |
| MMA8451 (I2C 0x1D, ±4 g range) | 3-axis accelerometer (shake/pick-up detection) |
| DFPlayer Mini | Audio playback (plays screaming sounds) |
| Electret mic + envelope circuit | Sound level detection |
| 555 timer (bistable) | External enable signal |
| Momentary push button | Kill switch (pin 4 to GND) |

### Pin mapping

| Pin | Connection |
|---|---|
| 2 | Left encoder A (interrupt) |
| 3 | Right encoder A (interrupt) |
| 4 | Kill switch (INPUT_PULLUP) |
| 8 | DFPlayer IO1 — stop |
| 10 | DFPlayer IO2 — next/play |
| 11 | HC-SR04 trigger |
| 12 | HC-SR04 echo |
| 13 | 555 timer enable |
| A0 | Mic envelope |
| A4 | SDA (I2C) |
| A5 | SCL (I2C) |

### Motor wiring

Left motors are on Motoron shield 1 (address 16), channels 2 and 3. Right motors are on Motoron shield 2 (address 17), channels 2 and 3. The right side is physically wired in reverse, so the firmware negates the speed value for right motors.

## Physical parameters

- Wheel radius: 3.2 cm
- Wheelbase (track width): 17.0 cm
- Encoder: 144 counts per revolution
- 15° spin ≈ 8 encoder counts (`COUNTS_PER_15_DEGREES`, calculated from the above; encoders count rising edges of channel A only)

## Repo structure

```
├── dont_touch_me_bot.ino    # Main firmware — full state machine
├── Accelerometer.ino        # Test sketch for the MMA8451 (Adafruit example)
├── Motor_shield.ino         # Test sketch for the Motoron shields
├── ultrasonic_sensor.ino    # Test sketch for the HC-SR04 (pins 11/12)
├── microphone.ino           # Test sketch for the sound detector (SparkFun demo)
└── README.md
```

The test sketches are useful for verifying each sensor/actuator independently before running the full system. Note that they don't all match the main firmware's wiring: `Motor_shield.ino` drives shield 1 channels 1–3 and shield 2 channel 3, and `microphone.ino` uses pin 2 for the sound detector's gate output (pin 2 is the left encoder in the main sketch) and the pin 13 LED.

## Dependencies

Install via the Arduino Library Manager:

- **Motoron** — Pololu Motoron I2C motor shield library
- **Adafruit MMA8451** — accelerometer driver
- **Adafruit Unified Sensor** — required by the accelerometer library
- **Wire** — built-in I2C (included with Arduino IDE)

## Setup & usage

1. Wire everything according to the pin mapping above.
2. Install the dependencies in the Arduino IDE.
3. All sketches sit in the repo root, and each defines its own `setup()`/`loop()`, so they can't be compiled together as one sketch. Copy the one you want into a folder of the same name (e.g. `dont_touch_me_bot/dont_touch_me_bot.ino`); the Arduino IDE will offer to do this when you open the file.
4. Upload `dont_touch_me_bot.ino` to the Arduino Uno.
5. Open the Serial Monitor at 9600 baud — you'll see an I2C bus scan, sensor init status, and then live telemetry.
6. The robot will start in CALIBRATION, driving while sampling mic levels. After 5 seconds it enters ANXIETY and starts listening.

### Telemetry output

The serial monitor prints diagnostics every 250 ms:

```
[ANXIETY    ] MIC: 142/210 | ENC L:0 R:0/8 | DIST: 67cm | ACCEL: 9.84 (dev: 0.03 shk: 0/3)
```

This shows the current state, mic reading vs threshold, encoder counts vs the 15° target, distance (omitted in FEAR, where the scan takes its own readings), and accelerometer data including shake detection progress.

### I2C bus recovery

The MMA8451 accelerometer can occasionally hang the I2C bus on startup. The firmware handles this automatically — it attempts recovery by clocking SCL manually, then retries. If the accelerometer still isn't found, the robot continues without shake detection.

## Tuning

Key constants at the top of the main sketch:

| Constant | Default | What it controls |
|---|---|---|
| `MOTOR_SPEED` | 600 | Motoron speed for all movement (max 800) |
| `PROXIMITY_THRESHOLD` | 30 cm | Distance that triggers FLIGHT |
| `CALIBRATION_OBSTACLE` | 20 cm | Distance at which calibration driving reverses |
| `SHAKE_THRESHOLD` | 6.0 m/s² | Acceleration deviation to detect shake |
| `SHAKE_COUNT_REQUIRED` | 3 | Consecutive readings needed to confirm shake |
| `SOUND_THRESHOLD_FACTOR` | 1.50 | Multiplier on background noise for sound trigger |
| `CALIBRATION_DURATION` | 5000 ms | How long calibration runs |
| `STATE_TIMEOUT` | 5000 ms | Timeout for FEAR and FLIGHT states |
| `SCAN_PAUSE_MS` | 300 ms | Pause at each 15° to take a reading |
| `SOUND_LOOP_MS` | 1000 ms | Interval for re-triggering audio during FLIGHT |

## Contributors
- [@krishnanefx](https://github.com/krishnanefx)
- [@wippy06](https://github.com/wippy06)
- [@yaariek](https://github.com/yaariek)
