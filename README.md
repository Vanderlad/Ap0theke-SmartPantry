# Smart Pantry

A Wokwi/Arduino prototype for a home smart-pantry system. Each ingredient is
weighed with a load cell, and LEDs indicate which ingredients are needed for a
selected recipe and whether enough is available.

## Current progress

- Arduino Uno simulation in Wokwi
- Four simulated HX711 load-cell inputs: pasta, sauce, rice, and beans
- Four matching NeoPixel LEDs
- LCD recipe/status display and three-button control
- Six recipes with gram-based ingredient requirements
- Low-stock detection and Serial JSON status output
- First browser dashboard in `web/`, with recipe selection, inventory levels,
  automatic low-stock list, and optional Web Serial support for a USB Arduino

## How it works today

Use Previous/Next to choose a recipe and Select to turn recipe mode on or off.
In recipe mode, green LEDs mark ingredients with enough stock; red LEDs mark
ingredients that are required but insufficient. Outside recipe mode, red LEDs
identify ingredients that are low overall.

## To do

- [ ] Test and calibrate each HX711/load cell with real hardware
- [ ] Replace the Arduino Uno with an ESP32 for Wi-Fi connectivity
- [ ] Build a web dashboard to choose recipes and view inventory
- [ ] Add a database for ingredients, recipes, and quantities
- [ ] Sync recipe selection from the web app to the pantry LEDs
- [ ] Generate a shopping list from low-stock ingredients
- [ ] Add low-stock notifications

## Project files

- `sketch.ino` — pantry firmware and control logic
- `diagram.json` — Wokwi circuit diagram
- `platformio.ini` — PlatformIO configuration
- `wokwi.toml` — Wokwi firmware configuration
- `web/` — browser dashboard prototype (requires Node.js 22 or newer)
