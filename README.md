# ESP32 safety gate

Arduino sketch for a servo gate with PIR motion detection, ultrasonic obstruction sensing, a buzzer, and a 128×64 SPI OLED.

## Hardware

| Part | ESP32 pin |
| --- | ---: |
| OLED MOSI / SCK / DC / CS / RESET | 23 / 18 / 17 / 5 / 16 |
| Servo | 13 |
| Buzzer | 14 |
| PIR | 25 |
| Ultrasonic ECHO / TRIG | 26 / 27 |
| Potentiometer | 34 |

## Setup

Install the ESP32 board package and the **Adafruit GFX**, **Adafruit SSD1306**, and **ESP32Servo** libraries in Arduino IDE. Open `cb015682.ino`, select your ESP32 board and port, then upload. Open Serial Monitor at **9600 baud**.

## Use

In auto mode, motion opens the gate. The potentiometer sets how long it stays open after motion stops (2–10 seconds). The PIR sensor warms up for 30 seconds after startup.

If an object is detected within 20 cm while the gate closes, the gate stops and the buzzer sounds. Once the path has been clear for 2 seconds, the gate reopens.

Serial commands: `AUTO`, `MANUAL`, `OPEN`, `CLOSE`, `STATUS`, `HELP`. Use `MANUAL` before `OPEN` or `CLOSE`.

Name: Gihan Ariyasena
Student ID: CB015682
