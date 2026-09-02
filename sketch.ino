#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_NeoPixel.h>
#include <HX711.h>
#include <stdlib.h>
#include <string.h>

// ==================================================
// SMART PANTRY - WOKWI HARDWARE
// ==================================================

const byte NUM_ITEMS = 4;

// Buttons use INPUT_PULLUP, so each button connects its pin to GND.
const byte PREV_BUTTON = 2;
const byte NEXT_BUTTON = 3;
const byte SELECT_BUTTON = 4;

// Every ingredient has its own HX711/load cell. This is the same layout a
// physical pantry would use: one scale underneath each ingredient container.
const byte HX711_DT_PINS[NUM_ITEMS] = {6, 8, 10, 12};
const byte HX711_SCK_PINS[NUM_ITEMS] = {5, 7, 11, 13};

const byte LED_PIN = 9;

// Full-container weights. Calibrate these values for your real containers.
const unsigned int fullWeightGrams[NUM_ITEMS] = {
  1000,               // Pasta
  700,                // Sauce
  1000,               // Rice
  800                  // Beans
};
const float WOKWI_HX711_SCALE_FACTOR = 420.0f;
const float WEIGHT_CHANGE_THRESHOLD_KG = 0.02f; // 20 grams
const unsigned long SENSOR_INTERVAL_MS = 200;
// Wokwi buttons have bounce disabled in diagram.json, so a short debounce keeps
// quick mouse clicks and keyboard presses from being missed. Use 20-30 ms with
// real mechanical buttons if their hardware bounce is noticeable.
const unsigned long DEBOUNCE_MS = 5;
const int LOW_STOCK_PERCENT = 25;

HX711 scales[NUM_ITEMS];
LiquidCrystal_I2C lcd(0x27, 16, 2);
Adafruit_NeoPixel pantryLEDs(NUM_ITEMS, LED_PIN, NEO_GRB + NEO_KHZ800);

// ==================================================
// PANTRY AND RECIPE DATA
// ==================================================

const char *const itemNames[NUM_ITEMS] = {
  "Pasta",
  "Sauce",
  "Rice",
  "Beans"
};

struct Recipe {
  const char *name;
  // Zero means this ingredient is not part of the recipe.
  unsigned int requiredGrams[NUM_ITEMS];
};

const Recipe recipes[] = {
  {"Tomato Pasta", {250, 300,   0,   0}},
  {"Rice & Beans", {  0,   0, 300, 250}},
  {"Pantry Bowl",  {  0, 150, 250, 150}},
  {"Pasta & Beans",{250, 200,   0, 200}},
  {"Saucy Rice",   {  0, 200, 350,   0}},
  {"Bean Pasta",   {200, 250,   0, 150}}
};

const byte NUM_RECIPES = sizeof(recipes) / sizeof(recipes[0]);

// ==================================================
// PROGRAM STATE
// ==================================================

byte selectedRecipe = 0;
bool recipeActive = false;

int stockPercent[NUM_ITEMS] = {0, 0, 0, 0};
unsigned int availableGrams[NUM_ITEMS] = {0, 0, 0, 0};
int lastReportedGrams[NUM_ITEMS] = {-1, -1, -1, -1};

unsigned long lastSensorReadMs = 0;

// Used by the Serial command handler, which appears before the output code.
void updateOutputs();

struct ButtonState {
  byte pin;
  bool stableState;
  bool lastReading;
  unsigned long changedAt;
};

ButtonState previousButton = {PREV_BUTTON, HIGH, HIGH, 0};
ButtonState nextButton = {NEXT_BUTTON, HIGH, HIGH, 0};
ButtonState selectButton = {SELECT_BUTTON, HIGH, HIGH, 0};

// ==================================================
// INVENTORY READING
// ==================================================

bool isLow(byte item) {
  return stockPercent[item] < LOW_STOCK_PERCENT;
}

bool recipeNeeds(byte item) {
  return recipes[selectedRecipe].requiredGrams[item] > 0;
}

bool hasEnoughForRecipe(byte item) {
  return availableGrams[item] >= recipes[selectedRecipe].requiredGrams[item];
}

void readStock() {
  for (byte i = 0; i < NUM_ITEMS; i++) {
    // Do not call get_units() until this load cell has a sample ready. A bad
    // sensor then leaves its previous reading intact instead of freezing code.
    if (!scales[i].is_ready()) {
      continue;
    }

    float weightKg = scales[i].get_units(1); // Will add nonblocking avg of 3 readings later to have stable weight reading, keeping at 1 so theres no block for now
    if (weightKg < 0.0f) {
      weightKg = 0.0f;
    }

    unsigned int measuredGrams = (unsigned int)(weightKg * 1000.0f + 0.5f);
    availableGrams[i] = min(measuredGrams, fullWeightGrams[i]);
    stockPercent[i] = (unsigned long)availableGrams[i] * 100 / fullWeightGrams[i];
  }
}

int findMissingIngredient() {
  for (byte i = 0; i < NUM_ITEMS; i++) {
    if (recipeNeeds(i) && !hasEnoughForRecipe(i)) {
      return i;
    }
  }

  return -1;
}

// ==================================================
// SERIAL REPORTING
// ==================================================

void printInventory() {
  Serial.println();
  Serial.println(F("------ PANTRY ------"));

  for (byte i = 0; i < NUM_ITEMS; i++) {
    Serial.print(itemNames[i]);
    Serial.print(F(": "));
    Serial.print(stockPercent[i]);
    if (isLow(i)) {
      Serial.println(F("% (BUY MORE)"));
    } else {
      Serial.println('%');
    }

    Serial.print(F("  Measured: "));
    Serial.print(availableGrams[i]);
    Serial.println(F(" g"));
  }

  Serial.println(F("--------------------"));
}

void printRecipeStatus() {
  const Recipe &recipe = recipes[selectedRecipe];
  bool missingAnything = false;

  Serial.println();
  Serial.print(F("Recipe: "));
  Serial.println(recipe.name);

  for (byte i = 0; i < NUM_ITEMS; i++) {
    if (!recipeNeeds(i)) {
      continue;
    }

    Serial.print(F(" - "));
    Serial.print(itemNames[i]);
    Serial.print(F(": "));

    Serial.print(availableGrams[i]);
    Serial.print(F(" g available, need "));
    Serial.print(recipe.requiredGrams[i]);
    Serial.print(F(" g: "));

    if (!hasEnoughForRecipe(i)) {
      Serial.println(F("NOT ENOUGH"));
      missingAnything = true;
    } else {
      Serial.println(F("AVAILABLE"));
    }
  }

  if (!missingAnything) {
    Serial.println(F("All ingredients available!"));
  }
}

void reportWeightChanges() {
  const unsigned int changeThresholdGrams =
      (unsigned int)(WEIGHT_CHANGE_THRESHOLD_KG * 1000.0f);

  for (byte i = 0; i < NUM_ITEMS; i++) {
    if (lastReportedGrams[i] < 0) {
      lastReportedGrams[i] = availableGrams[i];
      continue;
    }

    if (abs((int)availableGrams[i] - lastReportedGrams[i]) < changeThresholdGrams) {
      continue;
    }

    lastReportedGrams[i] = availableGrams[i];
    Serial.println();
    Serial.print(itemNames[i]);
    Serial.print(F(" weight changed: "));
    Serial.print(availableGrams[i]);
    Serial.println(F(" g"));

    if (isLow(i)) {
      Serial.print(itemNames[i]);
      Serial.println(F(" needs restocking."));
    }

    if (recipeActive && recipeNeeds(i)) {
      printRecipeStatus();
    }
  }
}

// ==================================================
// SERIAL API: FUTURE WEB-APP CONNECTION
// ==================================================

// This uses one complete line per command, which makes it easy to test in the
// Wokwi Serial Monitor and later replace with Wi-Fi on an ESP32.

// Will eventually move to using ArduinoJson C++ library
char serialCommand[24]; //each char placed in this buffer
byte serialCommandLength = 0;

void printStatusJson() {
  Serial.print(F("{\"type\":\"status\",\"selectedRecipe\":"));
  Serial.print(selectedRecipe);
  Serial.print(F(",\"recipe\":\""));
  Serial.print(recipes[selectedRecipe].name);
  Serial.print(F("\",\"active\":"));
  Serial.print(recipeActive ? F("true") : F("false"));
  Serial.print(F(",\"items\":["));

  for (byte i = 0; i < NUM_ITEMS; i++) {
    if (i > 0) {
      Serial.print(',');
    }

    Serial.print(F("{\"name\":\""));
    Serial.print(itemNames[i]);
    Serial.print(F("\",\"availableGrams\":"));
    Serial.print(availableGrams[i]);
    Serial.print(F(",\"requiredGrams\":"));
    Serial.print(recipes[selectedRecipe].requiredGrams[i]);
    Serial.print(F(",\"low\":"));
    Serial.print(isLow(i) ? F("true") : F("false"));
    Serial.print(F(",\"enough\":"));
    Serial.print(hasEnoughForRecipe(i) ? F("true") : F("false"));
    Serial.print('}');
  }

  Serial.println(F("]}"));
}

void processSerialCommand(const char *command) {
  if (strcmp(command, "STATUS") == 0) {
    printStatusJson();
    return;
  }

  if (strcmp(command, "CLEAR") == 0) {
    recipeActive = false;
    updateOutputs();
    Serial.println(F("{\"type\":\"ok\",\"message\":\"recipe cleared\"}"));
    return;
  }

  const char *recipePrefix = "RECIPE:";
  if (strncmp(command, recipePrefix, strlen(recipePrefix)) == 0) { // if n = prefix length, then if n num of chars == RECIPE:, then check suffix
    char *endOfNumber;
    long recipeNumber = strtol(command + strlen(recipePrefix), &endOfNumber, 10); // shift pointer to look at number ( base 10 )
                                                                                  // converted string to long int
    if (*endOfNumber == '\0' && recipeNumber >= 0 &&
        recipeNumber < NUM_RECIPES) {
      selectedRecipe = (byte)recipeNumber;
      recipeActive = true;
      updateOutputs();
      printStatusJson();
      return;
    }
  }

  Serial.println(F("{\"type\":\"error\",\"message\":\"Use STATUS, CLEAR, or RECIPE:0-5\"}"));
}

void handleSerialCommands() {
  while (Serial.available() > 0) {
    char incoming = (char)Serial.read();

    if (incoming == '\n' || incoming == '\r') { //
      if (serialCommandLength > 0) {
        serialCommand[serialCommandLength] = '\0'; // add null terminator to command
        processSerialCommand(serialCommand);
        serialCommandLength = 0;
      }
    } else if (serialCommandLength < sizeof(serialCommand) - 1) {
      serialCommand[serialCommandLength++] = incoming;
    } else {
      serialCommandLength = 0;
      Serial.println(F("{\"type\":\"error\",\"message\":\"command too long\"}"));
    }
  }
}

// ==================================================
// NON-BLOCKING BUTTON HANDLING
// ==================================================

bool buttonPressed(ButtonState &button) {
  bool reading = digitalRead(button.pin);
  unsigned long now = millis();

  if (reading != button.lastReading) {
    button.lastReading = reading;
    button.changedAt = now;
  }

  if ((now - button.changedAt) >= DEBOUNCE_MS && reading != button.stableState) {
    button.stableState = reading;

    // INPUT_PULLUP buttons are pressed when their state becomes LOW.
    return button.stableState == LOW;
  }

  return false;
}

void handleButtons() {
  bool previousPressed = buttonPressed(previousButton);
  bool nextPressed = buttonPressed(nextButton);
  bool selectPressed = buttonPressed(selectButton);

  if (nextPressed) {
    selectedRecipe = (selectedRecipe + 1) % NUM_RECIPES;
    Serial.println();
    Serial.print(F("Selected recipe: "));
    Serial.println(recipes[selectedRecipe].name);

    if (recipeActive) {
      printRecipeStatus();
    } else {
      printInventory();
    }
    updateOutputs();
  }

  if (previousPressed) {
    selectedRecipe = selectedRecipe == 0 ? NUM_RECIPES - 1 : selectedRecipe - 1;
    Serial.println();
    Serial.print(F("Selected recipe: "));
    Serial.println(recipes[selectedRecipe].name);

    if (recipeActive) {
      printRecipeStatus();
    } else {
      printInventory();
    }
    updateOutputs();
  }

  if (selectPressed) {
    recipeActive = !recipeActive;
    Serial.println();

    if (recipeActive) {
      Serial.print(F("Recipe activated: "));
      Serial.println(recipes[selectedRecipe].name);
      printRecipeStatus();
    } else {
      Serial.println(F("Returned to recipe selection."));
      printInventory();
    }
    updateOutputs();
  }
}

void initializeButton(ButtonState &button) {
  button.stableState = digitalRead(button.pin);
  button.lastReading = button.stableState;
  button.changedAt = millis();
}

void initializeButtons() {
  initializeButton(previousButton);
  initializeButton(nextButton);
  initializeButton(selectButton);
}

// ==================================================
// LED AND LCD OUTPUT
// ==================================================

void updateLEDs() {
  const uint32_t OFF = pantryLEDs.Color(0, 0, 0);
  const uint32_t RED = pantryLEDs.Color(150, 0, 0);
  const uint32_t GREEN = pantryLEDs.Color(0, 150, 0);

  for (byte i = 0; i < NUM_ITEMS; i++) {
    uint32_t color = OFF;

    if (recipeActive) {
      if (recipeNeeds(i)) {
        color = hasEnoughForRecipe(i) ? GREEN : RED;
      }
    } else if (isLow(i)) {
      color = RED;
    }

    pantryLEDs.setPixelColor(i, color);
  }

  pantryLEDs.show();
}

void writeLCDRow(byte row, const char *text) {
  lcd.setCursor(0, row);

  byte i = 0;
  while (text[i] != '\0' && i < 16) {
    lcd.print(text[i]);
    i++;
  }

  while (i < 16) {
    lcd.print(' ');
    i++;
  }
}

void updateDisplay() {
  char secondRow[17];

  if (!recipeActive) {
    byte lowCount = 0;
    for (byte i = 0; i < NUM_ITEMS; i++) {
      if (isLow(i)) {
        lowCount++;
      }
    }

    snprintf(secondRow, sizeof(secondRow), "Low:%u SEL=stock", lowCount);
  } else {
    int missing = findMissingIngredient();

    if (missing == -1) {
      snprintf(secondRow, sizeof(secondRow), "All in stock!");
    } else {
      snprintf(secondRow, sizeof(secondRow), "Missing:%s", itemNames[missing]);
    }
  }

  writeLCDRow(0, recipes[selectedRecipe].name);
  writeLCDRow(1, secondRow);
}

void updateOutputs() {
  updateLEDs();
  updateDisplay();
}

// ==================================================
// ARDUINO SETUP AND LOOP
// ==================================================

void setup() {
  Serial.begin(9600);

  pinMode(PREV_BUTTON, INPUT_PULLUP);
  pinMode(NEXT_BUTTON, INPUT_PULLUP);
  pinMode(SELECT_BUTTON, INPUT_PULLUP);
  initializeButtons();

  pantryLEDs.begin();
  pantryLEDs.clear();
  pantryLEDs.show();

  for (byte i = 0; i < NUM_ITEMS; i++) {
    scales[i].begin(HX711_DT_PINS[i], HX711_SCK_PINS[i]);
    scales[i].set_scale(WOKWI_HX711_SCALE_FACTOR);
  }

  lcd.init();
  lcd.backlight();
  writeLCDRow(0, "Smart Pantry");
  writeLCDRow(1, "Starting...");

  Serial.println(F("Smart Pantry started."));
  Serial.println(F("Serial API: STATUS, RECIPE:0 through RECIPE:5, CLEAR"));
  delay(750);

  readStock();
  for (byte i = 0; i < NUM_ITEMS; i++) {
    lastReportedGrams[i] = availableGrams[i];
  }
  printInventory();
  updateOutputs();
}

void loop() {
  unsigned long now = millis();

  handleButtons();
  handleSerialCommands();

  if (now - lastSensorReadMs >= SENSOR_INTERVAL_MS) {
    lastSensorReadMs = now;
    readStock();
    reportWeightChanges();
    updateOutputs();
  }
}
