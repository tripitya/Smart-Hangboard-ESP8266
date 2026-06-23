# Smart Hangboard Controller 🧗‍♂️

A custom mechatronic hangboard system powered by an ESP8266 (NodeMCU V3), dual HX711 load cells, and WS2812B addressable LEDs. Designed for precision climbing training.

![Hangboard Image](Images/your_photo_name.jpg) ## Features
* **Captive Portal Web Interface:** Connects offline via QR Code (No internet required).
* **High-Precision Load Cells:** Reads real-time pulling weight (kg) to track performance.
* **Targeted LED Coach:** Select specific holds (e.g., 20mm outer edges) and the board highlights them dynamically.
* **EEPROM Leaderboard:** Saves Max Hang times permanently directly on the microcontroller.
* **Auto-Fail Mechanism:** Aborts the workout instantly if the hanging weight drops below 1kg.

## Hardware Required
* NodeMCU V3 (ESP8266)
* 2x 50kg Half-Bridge Load Cells
* 2x HX711 Amplifiers
* 1x 16x2 I2C LCD Screen
* 16x WS2812B LEDs
* 1x Passive Buzzer

## Getting Started
1. Install [FastLED] and [HX711_ADC] libraries in the Arduino IDE.
2. Upload the `HangboardController.ino` sketch to your NodeMCU.
3. Run the calibration wizard to tune your specific load cell setup.
4. Scan the QR code to join the board's Hotspot and begin training!
