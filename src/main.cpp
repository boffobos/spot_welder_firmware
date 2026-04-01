#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Encoder.h>
#include <EEPROM.h>

#if defined(__AVR__)
float maxVoltage = 5.0; // Most AVR (Uno, Mega, Nano) are 5V
#elif defined(ARDUINO_ARCH_SAMD) || defined(ARDUINO_ARCH_SAM)
float maxVoltage = 3.3; // Due, Zero, MKR series are 3.3V
#elif defined(ESP8266) || defined(ESP32)
float maxVoltage = 3.3; // ESP boards are 3.3V
#else
float maxVoltage = 5.0; // Default fallback
#endif

#if defined(__AVR__)
const int adcMax = 1023; // 10-bit
#elif defined(ARDUINO_ARCH_SAMD) || defined(ARDUINO_ARCH_SAM) || defined(ARDUINO_ARCH_RENESAS)
// These boards default to 10-bit but support higher
const int adcMax = 1023;
#elif defined(ESP32)
const int adcMax = 4095; // 12-bit
#else
const int adcMax = 1023;
#endif

LiquidCrystal_I2C lcd(0x27, 20, 4);
Encoder enc(2, 3);

#define BUTTON_PIN 4
#define START_PIN 5
#define OUTPUT_PIN A0
#define BUZZER_PIN 6
#define BATTERY_VOLTAGE_TRESHOLD (float)4.5
#define VOLTAGE_BATTERY_PIN A1
#define VOLTAGE_BATTERY_PIN_DEVIDER (float)2.7943
#define VOLTAGE_1_BANK A6
#define VOLTAGE_1_BANK_DEVIDER (float)3.293
#define PROTECTION_PIN A2
#define PROTECTION_PIN_DEVIDER (float)3.576
#define CONTACT_PIN A3 // Новый пин для контроля замыкания электродов
#define CONTACT_PIN_DEVIDER (float)1.47
#define BACKLIGHT_PIN 9
#define SLIDING_AVERAGE_WINDOW 30

// Для режима AUTO
bool autoTriggered = false;
unsigned long contactStartTime = 0;

int selected = 0;
int values[3] = {0, 0, 0};
float sValue = 0.1;
bool lightEnabled = true;
byte brightness = 5;
bool autoMode = true;
long lastPos = 0;
bool editMode = false;
bool buttonHeld = false;
unsigned long buttonPressTime = 0;
bool blinkState = true;
bool startButtonState = false;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 5;
const int8_t voltageMultiplier = 3;
bool dangerDisplayed = false;
unsigned long lastBeepTime = 0;
unsigned long lowVoltageStartTime = 0;
bool lowVoltageProtectionActive = false;
unsigned long protectionVoltageStartTime = 0;
bool protectionActive = false;
unsigned long startVoltTime = 0;

void drawScreen();
void drawVoltage(float voltages[2], bool lowVoltage);
void generatePulse();
float readVoltage(int pin, float voltageMultiplier = 1.0);
float *slidingAverageVoltage(int pin1, float devider1, int pin2, float devider2);

void beep()
{
  digitalWrite(BUZZER_PIN, HIGH);
  delay(50);
  digitalWrite(BUZZER_PIN, LOW);
}

void testDisplay()
{
  lcd.clear();
  lcd.setCursor(1, 0);
  lcd.print("Testing Display...");
  delay(1000);

  lcd.clear();
  for (int row = 0; row < 4; row++)
  {
    lcd.setCursor(0, row);
    lcd.print("ABCDEFGHIJKLMNOPQRST");
  }
  delay(1000);

  lcd.clear();
  for (int row = 0; row < 4; row++)
  {
    lcd.setCursor(0, row);
    lcd.print("01234567890123456789");
  }
  delay(1000);

  lcd.clear();
  for (int row = 0; row < 4; row++)
  {
    lcd.setCursor(0, row);
    lcd.print("####################");
  }
  delay(1000);
  lcd.clear();
}

void selfTest()
{
  // testDisplay();

  lcd.setCursor(1, 0);
  lcd.print("SYSTEM TESTING...");
  delay(500);

  lcd.setCursor(2, 1);
  lcd.print("EEPROM Check...");
  for (int i = 0; i < 3; i++)
  {
    values[i] = EEPROM.read(i);
    if (values[i] > 50)
      values[i] = 0;
  }
  sValue = EEPROM.read(6) / 10.0;
  if (sValue < 0.3 || sValue > 2.0)
    sValue = 0.3;
  delay(500);

  lcd.setCursor(2, 2);
  lcd.print("Backlight Test...");
  lcd.backlight();
  delay(500);
  lcd.noBacklight();
  delay(500);
  lcd.backlight();
  delay(500);

  lcd.setCursor(4, 3);
  lcd.print("Beep Test...");
  beep();
  delay(500);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Voltage Test...");
  float protectionVoltage = readVoltage(PROTECTION_PIN, PROTECTION_PIN_DEVIDER);
  lcd.setCursor(0, 1);
  lcd.print("Voltage: ");
  lcd.print(protectionVoltage, 2);
  lcd.print("V");
  delay(500);

  if (protectionVoltage < 10.0 || protectionVoltage > 18.0)
  {
    lcd.setCursor(0, 2);
    lcd.print("ERROR: OUT OF RANGE");
    lcd.setCursor(0, 3);
    lcd.print("REQ: 10-18V");
    for (int i = 0; i < 3; i++)
    {
      beep();
      delay(500);
    }
    delay(1000);
  }
  else
  {
    lcd.setCursor(0, 2);
    lcd.print("Voltage OK");
  }

  delay(500);
  lcd.clear();
  lcd.setCursor(2, 1);
  lcd.print("TEST COMPLETE!");
  delay(1000);
}

void setup()
{
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(START_PIN, INPUT_PULLUP);
  pinMode(OUTPUT_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(VOLTAGE_BATTERY_PIN, INPUT);
  pinMode(PROTECTION_PIN, INPUT);
  pinMode(CONTACT_PIN, INPUT); // Новый пин как вход
  digitalWrite(OUTPUT_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);
  pinMode(BACKLIGHT_PIN, OUTPUT);

  lcd.init();
  lcd.backlight();
  autoMode = EEPROM.read(5);
  brightness = EEPROM.read(7);
  if (brightness > 5)
    brightness = 5;
  analogWrite(BACKLIGHT_PIN, map(brightness, 0, 5, 0, 255));

  // lcd.setCursor(3, 0);
  // lcd.print("BY AKA KASYAN");
  // lcd.setCursor(2, 1);
  // lcd.print(">>MyWeld V2.0<<");
  // lcd.setCursor(5, 2);
  // lcd.print("SOFT V2.1");
  // lcd.setCursor(1, 3);
  // lcd.print("Aka Kasyan YouTube");
  // delay(3000);
  // beep();

  selfTest();
  lcd.clear();
  drawScreen();
}
void loop()
{
  float protectionVoltage = readVoltage(PROTECTION_PIN, PROTECTION_PIN_DEVIDER);
  bool protectionVoltageOutOfRange = (protectionVoltage > 18.0 || protectionVoltage < 10.0);

  if (protectionVoltageOutOfRange)
  {
    if (protectionVoltageStartTime == 0)
    {
      protectionVoltageStartTime = millis();
    }
    else if (millis() - protectionVoltageStartTime >= 1500)
    {
      protectionActive = true;
    }
  }
  else
  {
    protectionVoltageStartTime = 0;
    protectionActive = false;
    dangerDisplayed = false;
  }

  if (protectionActive)
  {
    if (!dangerDisplayed)
    {
      lcd.clear();
      lcd.setCursor(2, 0);
      lcd.print("DC 15V PROBLEM!");
      lcd.setCursor(6, 1);
      lcd.print("DANGER!");
      lcd.setCursor(2, 2);
      lcd.print("CONTROL VOLTAGE");
      lcd.setCursor(2, 3);
      lcd.print("ERROR1 <10V >18V");
      dangerDisplayed = true;
    }
    if (millis() - lastBeepTime >= 1000)
    {
      beep();
      lastBeepTime = millis();
    }
    return;
  }

  // === Режим AUTO: контроль замыкания электродов ===
  float contactVoltage = readVoltage(CONTACT_PIN, CONTACT_PIN_DEVIDER);
  bool contactDetected = contactVoltage > 3.0;

  if (autoMode && !lowVoltageProtectionActive)
  {
    if (contactDetected)
    {
      if (contactStartTime == 0)
      {
        contactStartTime = millis();
        beep(); // Сигнал о замыкании
      }
      else if (!autoTriggered && millis() - contactStartTime >= (unsigned long)(sValue * 1000))
      {
        generatePulse();
        autoTriggered = true;
      }
    }
    else
    {
      contactStartTime = 0;
      autoTriggered = false;
    }
  }

  if (digitalRead(BUTTON_PIN) == LOW)
  {
    if (!buttonHeld)
    {
      buttonPressTime = millis();
      buttonHeld = true;
    }
    else if (millis() - buttonPressTime > 2000)
    {
      editMode = !editMode;
      beep();
      buttonHeld = false;
      drawScreen(); // <--- ВОТ ЭТА СТРОКА решает проблему
    }
  }
  else
  {
    buttonHeld = false;
  }

  static unsigned long lastBlinkTime = 0;
  if (editMode && millis() - lastBlinkTime > 500)
  {
    blinkState = !blinkState;
    lastBlinkTime = millis();
    drawScreen();
  }

  long newPos = enc.read() / 4;
  if (newPos != lastPos)
  {
    beep();

    if (!editMode)
    {
      if (newPos > lastPos)
      {
        selected = (selected + 1) % (autoMode ? 6 : 5); // В MAN пропускаем S
      }
      else
      {
        selected = (selected + (autoMode ? 5 : 4)) % (autoMode ? 6 : 5);
      }
    }
    else
    {
      if (selected < 3)
      {
        if (selected == 2)
        { // P2 с поддержкой OFF
          if (newPos > lastPos && values[2] < 50)
            values[2]++;
          if (newPos < lastPos && values[2] > 0)
            values[2]--;
          EEPROM.write(2, values[2]);
        }
        else
        {
          if (newPos > lastPos && values[selected] < 50)
            values[selected]++;
          if (newPos < lastPos && values[selected] > 0)
            values[selected]--;
          EEPROM.write(selected, values[selected]);
        }
      }
      else if (selected == 3)
      {
        if (newPos > lastPos && brightness < 5)
          brightness++;
        if (newPos < lastPos && brightness > 0)
          brightness--;
        analogWrite(BACKLIGHT_PIN, map(brightness, 0, 5, 0, 255));
        EEPROM.write(7, brightness);
      }
      else if (selected == 4)
      {
        autoMode = !autoMode;
        EEPROM.write(5, autoMode);
        if (!autoMode && selected == 5)
        {
          selected = 0;
        }
      }
      else if (selected == 5 && autoMode)
      {
        if (newPos > lastPos && sValue < 2.0)
          sValue += 0.1;
        if (newPos < lastPos && sValue > 0.3)
          sValue -= 0.1;
        EEPROM.write(6, (int)(sValue * 10));
      }
    }

    lastPos = newPos;
    drawScreen();
  }

  // float voltage = readVoltage(VOLTAGE_BATTERY_PIN, VOLTAGE_BATTERY_PIN_DEVIDER);
  // float voltageB1 = readVoltage(VOLTAGE_1_BANK, VOLTAGE_1_BANK_DEVIDER);
  float *batteryVoltages = slidingAverageVoltage(VOLTAGE_1_BANK, VOLTAGE_1_BANK_DEVIDER, VOLTAGE_BATTERY_PIN, VOLTAGE_BATTERY_PIN_DEVIDER);
  bool lowVoltage = (batteryVoltages[1] < BATTERY_VOLTAGE_TRESHOLD);

  if (lowVoltage)
  {
    if (lowVoltageStartTime == 0)
    {
      lowVoltageStartTime = millis();
    }
    else if (millis() - lowVoltageStartTime >= 1500)
    {
      lowVoltageProtectionActive = true;
    }
  }
  else
  {
    lowVoltageStartTime = 0;
    lowVoltageProtectionActive = false;
  }

  drawVoltage(batteryVoltages, lowVoltageProtectionActive);
  free(batteryVoltages);

  if (!autoMode && digitalRead(START_PIN) == LOW && !startButtonState && !lowVoltageProtectionActive)
  {
    startButtonState = true;
    beep();
    generatePulse();
  }

  if (digitalRead(START_PIN) == HIGH)
  {
    startButtonState = false;
  }
} // <-- Закрытие loop()

void drawScreen()
{
  for (int i = 0; i < 5; i++)
  {
    lcd.setCursor((i % 2) * 10, i / 2);

    // Стрелка перед выбранным параметром
    if (i == selected)
    {
      lcd.print("->");
    }
    else
    {
      lcd.print("  ");
    }

    // Мигание выбранного параметра
    if (i == selected && editMode && blinkState)
    {
      lcd.print("      "); // Очищаем строку при мигании
    }
    else
    {
      if (i < 3)
      {
        lcd.print(i == 0 ? "P1=" : i == 1 ? "T="
                                          : "P2=");
        if (i == 2 && values[2] == 0)
        {
          lcd.print("OFF   ");
        }
        else
        {
          lcd.print(values[i]);
          lcd.print("ms ");
        }
      }
      else if (i == 3)
      {
        lcd.print("Light:");
        lcd.print(brightness);
        lcd.print("   "); // Затираем остатки текста
      }
      else if (i == 4)
      {
        lcd.print("Mode:");
        lcd.print(autoMode ? "AUTO " : "MAN  ");
      }
    }
  }

  // Если режим AUTO — рисуем S, иначе стираем строку
  lcd.setCursor(10, 2);
  if (autoMode)
  {
    if (selected == 5)
    {
      lcd.print("->");
    }
    else
    {
      lcd.print("  ");
    }

    if (selected == 5 && editMode && blinkState)
    {
      lcd.print("      "); // Мигание S
    }
    else
    {
      lcd.print("S=");
      lcd.print(sValue, 1);
      lcd.print("s  ");
    }
  }
  else
  {
    lcd.print("          "); // Очищаем строку, если режим MAN
  }
}

void drawVoltage(float voltages[2], bool lowVoltage)
{
  lcd.setCursor(0, 3);

  if (lowVoltage)
  {
    lcd.print("U: ");
    lcd.print(voltages[1]);
    lcd.print(" ");
    lcd.print("LOW ");
  }
  else
  {
    lcd.print("U1+U2:");
    lcd.print(voltages[0], 2);
    lcd.print("+");
    lcd.print(voltages[1] - voltages[0]);
    lcd.print("=");
    lcd.print(voltages[1]);
    // lcd.print(" "); // ← 5 пробелов, чтобы гарантированно затереть "LOW"
  }
}

void generatePulse()
{
  digitalWrite(OUTPUT_PIN, HIGH);
  delay(values[0]);
  digitalWrite(OUTPUT_PIN, LOW);

  if (values[2] > 0)
  {
    delay(10);
    delay(values[1]);
    digitalWrite(OUTPUT_PIN, HIGH);
    delay(values[2]);
    digitalWrite(OUTPUT_PIN, LOW);
  }
}

float readVoltage(int pin, float voltageMultiplier = 1.0)
{
  int analogVoltage = analogRead(pin);

  float voltage = analogVoltage * maxVoltage * voltageMultiplier / (float)adcMax;

  return voltage;
}

float *slidingAverageVoltage(int pin1, float devider1, int pin2, float devider2)
{
  const int window = SLIDING_AVERAGE_WINDOW;
  static float u1[window]{};
  static float u2[window]{};

  for (int i = 1; i < window; i++)
  {
    u1[i - 1] = u1[i];
    u2[i - 1] = u2[i];
  }
  u1[window - 1] = readVoltage(pin1, devider1);
  u2[window - 1] = readVoltage(pin2, devider2);

  float u1Accumulator = 0;
  int u1Count = 0;
  float u2Accumulator = 0;
  int u2Count = 0;

  for (int j = 0; j < window; j++)
  {
    if (u1[j] > 0)
    {
      u1Accumulator += u1[j];
      u1Count++;
    }

    if (u2[j] > 0)
    {
      u2Accumulator += u2[j];
      u2Count++;
    }
  }

  if (!(u1Count && u2Count))
    return NULL;

  float *voltages = (float *)malloc(sizeof(float) * 2);
  voltages[0] = u1Accumulator / u1Count;
  voltages[1] = u2Accumulator / u2Count;

  return voltages;
}