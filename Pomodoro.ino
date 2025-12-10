#include <Keypad.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <avr/sleep.h>
#include <avr/interrupt.h>

#define BUZZER 11
#define LED_GREEN A0
#define LED_RED A1
#define WAKE_PIN 2  // External push button between 5V and D2, pulldown 10kΩ to GND

LiquidCrystal_I2C lcd(0x27, 16, 2);

// ---- Keypad Setup ----
const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[ROWS] = {10, 9, 8, 7};
byte colPins[COLS] = {6, 5, 4, 3};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// ---- Timer Variables ----
unsigned long previousMillis = 0;
unsigned long interval = 1000;
int workMinutes = 0;
int restMinutes = 0;
int remainingSeconds = 0;
int totalWorkedSeconds = 0;
bool inWork = true;
bool running = false;
bool paused = false;

// ---- LCD + Power Control ----
unsigned long lastInteraction = 0;
bool lcdOn = true;
volatile bool wokeUp = false;

// ---- Helper Functions ----
void printLine(int row, String text) {
  lcd.setCursor(0, row);
  lcd.print(text);
  for (int i = text.length(); i < 16; i++) lcd.print(" ");
}

void setLEDs(bool greenOn, bool redOn) {
  digitalWrite(LED_GREEN, greenOn ? HIGH : LOW);
  digitalWrite(LED_RED, redOn ? HIGH : LOW);
}

// ---- Beeper Sounds ----
void beepKey() { tone(BUZZER, 1200, 50); }
void beepConfirm() { tone(BUZZER, 800, 150); }

void beepStartRest() {
  int melody[] = {988, 880, 784, 659};
  for (int i = 0; i < 4; i++) {
    tone(BUZZER, melody[i], 180);
    delay(220);
  }
  noTone(BUZZER);
  Serial.println("Melody: Start Rest");
}

void beepEndRest() {
  int melody[] = {659, 784, 880, 988};
  for (int i = 0; i < 4; i++) {
    tone(BUZZER, melody[i], 180);
    delay(220);
  }
  noTone(BUZZER);
  Serial.println("Melody: End Rest");
}

// ---- Interrupt Handler ----
void wakeISR() {
  wokeUp = true;
}

// ---- Sleep + Wake Logic ----
void goToSleep() {
  Serial.println("💤 Going to sleep...");

  lcd.noDisplay();
  lcd.noBacklight();
  setLEDs(false, false);
  noTone(BUZZER);

  delay(100);
  wokeUp = false;
  attachInterrupt(digitalPinToInterrupt(WAKE_PIN), wakeISR, RISING);

  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_enable();
  sleep_cpu();

  // MCU sleeps until interrupt triggers
  sleep_disable();
  detachInterrupt(digitalPinToInterrupt(WAKE_PIN));

  delay(200);
  Serial.println("🌞 Woke up!");
  lcd.display();
  lcd.backlight();
  lcdOn = true;
  lastInteraction = millis();

  // Resume LEDs to match state
  if (running && !paused) setLEDs(true, false);
  else setLEDs(false, true);

  lcd.clear();
  printLine(0, "Resuming...");
  delay(1000);
  displayTime();
}

// ---- Setup ----
void setup() {
  pinMode(BUZZER, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  pinMode(WAKE_PIN, INPUT);

  Serial.begin(9600);
  setLEDs(false, true);

  lcd.init();
  lcd.backlight();
  lcd.display();
  lcdOn = true;
  lastInteraction = millis();

  Serial.println("Pomodoro Timer Starting...");
  lcd.clear();
  printLine(0, "Pomodoro Timer");
  delay(2000);

  lcd.clear();
  printLine(0, "Work Time(min):");
  Serial.println("Enter Work Time (minutes):");
  workMinutes = getNumberInput();

  lcd.clear();
  printLine(0, "Rest Time(min):");
  Serial.println("Enter Rest Time (minutes):");
  restMinutes = getNumberInput();

  lcd.clear();
  printLine(0, "Press A to Start");
  Serial.println("Setup Complete. Press A to Start.");
}

// ---- Main Loop ----
void loop() {
  char key = keypad.getKey();

  if (key) {
    // Manual LCD toggle
    if (key == '*') {
      if (lcdOn) {
        lcd.noDisplay();
        lcd.noBacklight();
        lcdOn = false;
        Serial.println("LCD manually turned OFF by '*'");
      } else {
        lcd.display();
        lcd.backlight();
        lcdOn = true;
        Serial.println("LCD manually turned ON by '*'");
      }
      lastInteraction = millis();
      return;
    }

    // Wake LCD if off
    if (!lcdOn) {
      lcd.display();
      lcd.backlight();
      lcdOn = true;
      Serial.println("LCD turned ON by key press");
    }

    lastInteraction = millis();
    if (key == '#') beepConfirm(); else beepKey();

    Serial.print("Key pressed: ");
    Serial.println(key);

    if (key == 'A') {
      if (!running) {
        running = true;
        paused = false;
        if (remainingSeconds == 0) {
          remainingSeconds = workMinutes * 60;
          inWork = true;
        }
        Serial.println("Timer Started.");
        setLEDs(true, false);
      } else if (paused) {
        paused = false;
        Serial.println("Timer Resumed.");
        setLEDs(true, false);
      }
    } 
    else if (key == 'B') {
      if (running) {
        paused = true;
        Serial.println("Timer Paused.");
        setLEDs(false, true);
      }
    } 
    else if (key == 'C') {
      running = false;
      paused = false;
      remainingSeconds = 0;
      totalWorkedSeconds = 0;
      Serial.println("Timer Reset.");

      setLEDs(false, true);

      lcd.clear();
      printLine(0, "Timer Reset");
      delay(1500);

      lcd.clear();
      printLine(0, "Work Time(min):");
      Serial.println("Enter Work Time (minutes):");
      workMinutes = getNumberInput();

      lcd.clear();
      printLine(0, "Rest Time(min):");
      Serial.println("Enter Rest Time (minutes):");
      restMinutes = getNumberInput();

      lcd.clear();
      printLine(0, "Press A to Start");
      Serial.println("Setup Complete. Press A to Start.");

      setLEDs(false, true);
    }
  }

  // ---- Auto Sleep ----
  if ((!running || paused) && millis() - lastInteraction > 60000) {
    if (lcdOn) {
      lcd.noDisplay();
      lcd.noBacklight();
      lcdOn = false;
      Serial.println("LCD turned OFF due to inactivity");
    }
    goToSleep();
  }

  // ---- Main Timer ----
  if (running && !paused) {
    unsigned long currentMillis = millis();
    if (currentMillis - previousMillis >= interval) {
      previousMillis = currentMillis;

      if (remainingSeconds > 0) {
        remainingSeconds--;
        if (inWork) totalWorkedSeconds++;
      } else {
        if (inWork) {
          beepStartRest();
          inWork = false;
          remainingSeconds = restMinutes * 60;
          Serial.println("Switching to Rest.");
        } else {
          beepEndRest();
          inWork = true;
          remainingSeconds = workMinutes * 60;
          Serial.println("Switching to Work.");
        }
      }

      displayTime();
    }
  }
}

// ---- Display ----
void displayTime() {
  String line1 = (inWork ? "Work: " : "Rest: ");
  int minutes = remainingSeconds / 60;
  int seconds = remainingSeconds % 60;
  if (minutes < 10) line1 += "0";
  line1 += String(minutes) + ":";
  if (seconds < 10) line1 += "0";
  line1 += String(seconds);

  printLine(0, line1);

  String line2 = "Total: ";
  int totalMin = totalWorkedSeconds / 60;
  int totalSec = totalWorkedSeconds % 60;
  if (totalMin < 10) line2 += "0";
  line2 += String(totalMin) + ":";
  if (totalSec < 10) line2 += "0";
  line2 += String(totalSec);

  printLine(1, line2);

  Serial.print(inWork ? "Work " : "Rest ");
  Serial.print(minutes);
  Serial.print(":");
  if (seconds < 10) Serial.print("0");
  Serial.print(seconds);
  Serial.print(" | Total Work: ");
  Serial.print(totalMin);
  Serial.print(":");
  if (totalSec < 10) Serial.print("0");
  Serial.println(totalSec);
}

// ---- Number Input ----
int getNumberInput() {
  String input = "";
  char key;
  while (true) {
    key = keypad.getKey();

    // Sleep during input if idle
    if (lcdOn && (millis() - lastInteraction > 60000)) {
      lcd.noDisplay();
      lcd.noBacklight();
      lcdOn = false;
      Serial.println("LCD turned OFF due to inactivity (input mode)");
      goToSleep();
    }

    if (key) {
      if (key == '*') {
        if (lcdOn) {
          lcd.noDisplay();
          lcd.noBacklight();
          lcdOn = false;
          Serial.println("LCD manually turned OFF by '*'");
        } else {
          lcd.display();
          lcd.backlight();
          lcdOn = true;
          Serial.println("LCD manually turned ON by '*'");
        }
        lastInteraction = millis();
        continue;
      }

      if (!lcdOn) {
        lcd.display();
        lcd.backlight();
        lcdOn = true;
        Serial.println("LCD turned ON by key press");
      }

      lastInteraction = millis();

      if (key == '#') {
        beepConfirm();
        Serial.print("Final Value: ");
        Serial.println(input);
        return input.toInt();
      } else {
        beepKey();
        if (key >= '0' && key <= '9') {
          input += key;
          printLine(1, input);
          Serial.print("Entered: ");
          Serial.println(input);
        }
      }
    }
  }
}
