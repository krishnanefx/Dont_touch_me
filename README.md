# Don't Touch Me Bot 🤖

An autonomous Arduino-based robot that *really* doesn't want to be touched. It listens, looks around, and runs away screaming when it detects you getting too close.

Built as part of COMP0209 coursework at UCL.

## What it does

The robot runs a five-state behaviour machine:

```
CALIBRATION → ANXIETY → FEAR → FLIGHT
                ↑         |       |
                ←─────────←───────┘
                          
              KILLED (kill switch, any time)
```

1. **CALIBRATION** — Drives back and forth for 5 seconds, sampling the microphone to learn the background noise level with motors running. Sets a 150% threshold for sound detection.

2. **ANXIETY** — Sits still, listening. If it hears a loud noise above the calibrated threshold, it gets scared and enters FEAR.

3. **FEAR** — Spins in place in 15° increments, pausing at each step to take an ultrasonic distance reading. If it spots something within 30 cm, it panics. If nothing is found after a timeout, it calms back down to ANXIETY.

4. **FLIGHT** — Drives backwards at full speed while playing audio through a DFPlayer Mini. After a timeout, it stops and goes back to FEAR to scan again.

5. **KILLED** — Emergency stop via a physical kill switch. All motors and audio halt immediately. Press the kill switch again to restart from CALIBRATION.

At any point during ANXIETY, FEAR, or FLIGHT, picking up or shaking the robot (detected via the accelerometer) triggers an immediate jump to FLIGHT.

## Hardware

| Component | Role |
|---|---|
| Arduino Uno | Main controller |
| 2× Motoron I2C Motor Shield (addr 16, 17) | Dual motor control — left and right side |
| 4× DG01D-E DC motors with encoders | Differential drive (2 per side) |
| HC-SR04 | Ultrasonic distance sensor |
| MMA8451 | 3-axis accelerometer (shake/pick-up detection) |
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
- 15° spin ≈ 8 encoder counts (calculated, pending physical confirmation)

## Repo structure

```
├── dont_touch_me_bot.ino    # Main firmware — full state machine
├── Accelerometer.ino        # Standalone test sketch for MMA8451
├── Motor_shield.ino         # Standalone test sketch for Motoron shields
├── ultrasonic_sensor.ino    # Standalone test sketch for HC-SR04
├── microphone.ino           # Standalone test sketch for Microphone
└── README.md
```

The test sketches are useful for verifying each sensor/actuator independently before running the full system.

## Dependencies

Install via the Arduino Library Manager:

- **Motoron** — Pololu Motoron I2C motor shield library
- **Adafruit MMA8451** — accelerometer driver
- **Adafruit Unified Sensor** — required by the accelerometer library
- **Wire** — built-in I2C (included with Arduino IDE)

## Setup & usage

1. Wire everything according to the pin mapping above.
2. Install the dependencies in the Arduino IDE.
3. Upload `dont_touch_me_bot.ino` to the Arduino Uno.
4. Open the Serial Monitor at 9600 baud — you'll see an I2C bus scan, sensor init status, and then live telemetry.
5. The robot will start in CALIBRATION, driving back and forth while sampling mic levels. After 5 seconds it enters ANXIETY and starts listening.

### Telemetry output

The serial monitor prints diagnostics every 250 ms:

```
[ANXIETY    ] MIC: 142/210 | ENC L:0 R:0/8 | DIST: 67cm | ACCEL: 9.84 (dev: 0.03 shk: 0/3)
```

This shows the current state, mic reading vs threshold, encoder counts vs target, distance, and accelerometer data including shake detection progress.

### I2C bus recovery

The MMA8451 accelerometer can occasionally hang the I2C bus on startup. The firmware handles this automatically — it attempts recovery by clocking SCL manually, then retries. If the accelerometer still isn't found, the robot continues without shake detection.

## Tuning

Key constants at the top of the main sketch:

| Constant | Default | What it controls |
|---|---|---|
| `MOTOR_SPEED` | 600 | PWM speed for all movement |
| `PROXIMITY_THRESHOLD` | 30 cm | Distance that triggers FLIGHT |
| `SHAKE_THRESHOLD` | 6.0 m/s² | Acceleration deviation to detect shake |
| `SHAKE_COUNT_REQUIRED` | 3 | Consecutive readings needed to confirm shake |
| `SOUND_THRESHOLD_FACTOR` | 1.50 | Multiplier on background noise for sound trigger |
| `CALIBRATION_DURATION` | 5000 ms | How long calibration runs |
| `STATE_TIMEOUT` | 5000 ms | Timeout for FEAR and FLIGHT states |
| `SCAN_PAUSE_MS` | 300 ms | Pause at each 15° to take a reading |

## License
This project was built for UCL COMP0209. Use it however you like.

## Contributors
- [@krishnanefx](https://github.com/krishnanefx)
- [@wippy06](https://github.com/wippy06)
- [@yaariek](https://github.com/yaariek)

## Video Submitted
[Video](https://youtu.be/9Sp8UCt-aAA)


