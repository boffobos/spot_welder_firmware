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
#define VOLTAGE_1_BANK_PIN A6
#define VOLTAGE_1_BANK_PIN_DEVIDER (float)3.293
#define MOSFET_DRIVER_VOLTAGE_PIN A2
#define MOSFET_DRIVER_VOLTAGE_PIN_DEVIDER (float)3.576
#define CONTACT_PIN A3 // Новый пин для контроля замыкания электродов
#define CONTACT_PIN_DEVIDER (float)1.47
#define BACKLIGHT_PIN 9
#define SLIDING_AVERAGE_WINDOW 30
#define LONG_BUTTON_PRESS_TIME_MS 600
/*limits*/
#define PULSE_1_MIN 1
#define PULSE_1_MAX 50
#define PULSE_2_MIN 0
#define PULSE_2_MAX 50
#define PULSE_DELAY_MIN 10
#define PULSE_DELAY_MAX 99
#define AM_DELAY_MIN 0.3
#define AM_DELAY_MAX 2
#define BRIGHTNESS_MIN 0
#define BRIGHTNESS_MAX 5
#define MAX_SETTINGS_STRING_LEN 12
#define SETTING_OPTIONS 7
#define SETTINGS_SCREEN_SPAN 3
#define MAX_SETTINGS_SCREEN_NUMBER SETTING_OPTIONS - SETTINGS_SCREEN_SPAN

/*Settings screen view

   _____Screen 0_______     ______Screen 1______     ______Screen 2______
  |     SETTINGS       |   |     SETTINGS       |   |     SETTINGS       |
 0|* Pulse_1:   50ms   |   |  Pulse_2:   50ms   |   |  Delay:     50ms   |
 1|  Pulse_2:   50ms   |   |  Delay:     50ms   |   |  Mode:      AUTO   |
 2|  Delay:     50ms   |   |* Mode:      AUTO   |   |* Auto_delay:0.5sec |
   --------------------     --------------------     --------------------
 3/  Mode:      AUTO   /
 4/  Auto_delay:0.5sec /
 5/  Back_light:4      /
 6/  Beeper:    ON     /
 7/____________________/

*/

/*EEPROM structure:
      0         1         2         3         4           5          6          7
  [||||||||][||||||||][||||||||][00000000][00000000] [||||||||] [||||||||] [|||||||||]
  [pulse_1]  [delay]   [pulse_2]                    [auto_mode] [am_delay] [brightness]
  */

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

uint8_t screen_shown = 0; // 0 - main screen, 1 - settings screen

typedef enum
{
  PULSE_1,
  PULSE_DELAY,
  PULSE_2,
  AUTOMODE = 5,
  AM_DELAY,
  BRIGHTNESS

} Eeprom;

struct
{
  uint8_t pulse_1;               /*1-50 ms*/
  uint8_t pulse_2;               /*0-50 ms*/
  uint8_t inter_pulse_delay;     /*10-50 ms*/
  uint8_t mode;                  /*0 - manual, 1 - auto*/
  uint8_t auto_mode_delay;       /*.3-2 sec*/
  uint8_t back_light_saturation; /*0-5*/
  uint8_t beeper_mode;           /*0 - off, 1 - on*/
} settings;

void drawScreen();
void drawVoltage(float voltages[2], bool lowVoltage);
void generatePulse();
float readVoltage(int pin, float voltageMultiplier = 1.0);
float slidingAverageVoltage(float *window_arr, const int window_size, int pin, float devider);
void printMainScreen(float *voltages);
uint8_t loadSettings();
void printSettingsScreen(uint8_t cursor); // define
int digitalReadDebounce(int pin);

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
  float protectionVoltage = readVoltage(MOSFET_DRIVER_VOLTAGE_PIN, MOSFET_DRIVER_VOLTAGE_PIN_DEVIDER);
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
  pinMode(MOSFET_DRIVER_VOLTAGE_PIN, INPUT);
  pinMode(CONTACT_PIN, INPUT); // Новый пин как вход
  digitalWrite(OUTPUT_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);
  pinMode(BACKLIGHT_PIN, OUTPUT);

  lcd.init();
  lcd.backlight();
  loadSettings();
  values[0] = settings.pulse_1;
  values[1] = settings.inter_pulse_delay;
  values[2] = settings.pulse_2;
  autoMode = settings.mode;
  sValue = (float)settings.auto_mode_delay / 10.0;
  brightness = settings.back_light_saturation;
  // autoMode = EEPROM.read(5);
  // brightness = EEPROM.read(7);
  // if (brightness > 5)
  //   brightness = 5;
  if (brightness == 0)
    lcd.noBacklight();

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

  // selfTest();
  lcd.clear();
  // drawScreen();
  Serial.begin(9600);
}

void loop()
{
  const int window_size = SLIDING_AVERAGE_WINDOW;
  static float u1_arr[window_size]; // super capasitor fist bank voltage
  static float ub_arr[window_size]; // super capasitors battery voltage
  static float ug_arr[window_size]; // mosfet driver voltage

  float u1 = slidingAverageVoltage(u1_arr, window_size, VOLTAGE_1_BANK_PIN, VOLTAGE_1_BANK_PIN_DEVIDER);
  float ub = slidingAverageVoltage(ub_arr, window_size, VOLTAGE_BATTERY_PIN, VOLTAGE_BATTERY_PIN_DEVIDER);
  float ug = slidingAverageVoltage(ug_arr, window_size, MOSFET_DRIVER_VOLTAGE_PIN, MOSFET_DRIVER_VOLTAGE_PIN_DEVIDER);
  float voltages[] = {u1, ub, ug};

  if (screen_shown == 1)
  {
    /*Encoder control*/

    uint8_t cursor_max = SETTING_OPTIONS - 1; // custor starts from 0
    uint8_t cursor_min = 0;
    int8_t cursor = enc.read() / 2;
    if (cursor > cursor_max)
    {
      enc.write(cursor_max * 2);
      cursor = enc.read() / 2;
    }
    else if (cursor < cursor_min)
    {
      enc.write(cursor_min * 2);
      cursor = enc.read() / 2;
    }
    ///////////////////////////////

    printSettingsScreen(cursor);
  }
  else if (screen_shown == 0)
  {
    printMainScreen(voltages);
  }

  /*Handle button presses*/
  static uint32_t press_time = 0;
  uint8_t b_state = digitalReadDebounce(BUTTON_PIN);
  static uint8_t prev_b_state = b_state;
  if (prev_b_state != b_state)
  {
    if (b_state == LOW)
    {
      press_time = millis();
    }
    else
    {
      if (press_time != 0)
      {
        uint32_t current_time = millis();
        if (current_time - press_time < LONG_BUTTON_PRESS_TIME_MS)
        {
          // handle short press
          Serial.print("Short press: ");
          Serial.print(current_time - press_time);
          Serial.println("ms");
          press_time = 0;
        }
      }
    }
  }
  else
  {
    uint32_t current_time = millis();
    if (b_state == LOW && press_time != 0 && current_time - press_time > LONG_BUTTON_PRESS_TIME_MS)
    {
      // handle long press
      Serial.print("Long press: ");
      Serial.print(current_time - press_time);
      Serial.println("ms");
      lcd.clear();
      screen_shown = !screen_shown; // switching screens
      press_time = 0;
    }
  }
  prev_b_state = b_state;
  //////////////////

  /* Toggle screens*/

  ///////////////////
  // lcd.clear();
  // lcd.setCursor(0, 0);
  // lcd.print("Test");
  // lcd.setCursor(0, 1);
  // lcd.print("U1:");

  float protectionVoltage = ug;
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

  // if (digitalRead(BUTTON_PIN) == LOW)
  // {
  //   if (!buttonHeld)
  //   {
  //     buttonPressTime = millis();
  //     buttonHeld = true;
  //   }
  //   else if (millis() - buttonPressTime > 2000)
  //   {
  //     editMode = !editMode;
  //     beep();
  //     buttonHeld = false;
  //     // drawScreen(); // <--- ВОТ ЭТА СТРОКА решает проблему
  //     printMainScreen(voltages);
  //   }
  // }
  // else
  // {
  //   buttonHeld = false;
  // }

  static unsigned long lastBlinkTime = 0;
  if (editMode && millis() - lastBlinkTime > 500)
  {
    blinkState = !blinkState;
    lastBlinkTime = millis();
    // drawScreen();
    printMainScreen(voltages);
  }

  // long newPos = enc.read() / 4;
  // if (newPos != lastPos)
  // {
  //   beep();

  //   if (!editMode)
  //   {
  //     if (newPos > lastPos)
  //     {
  //       selected = (selected + 1) % (autoMode ? 6 : 5); // В MAN пропускаем S
  //     }
  //     else
  //     {
  //       selected = (selected + (autoMode ? 5 : 4)) % (autoMode ? 6 : 5);
  //     }
  //   }
  //   else
  //   {
  //     if (selected < 3)
  //     {
  //       if (selected == 2)
  //       { // P2 с поддержкой OFF
  //         if (newPos > lastPos && values[2] < 50)
  //           values[2]++;
  //         if (newPos < lastPos && values[2] > 0)
  //           values[2]--;
  //         EEPROM.write(2, values[2]);
  //       }
  //       else
  //       {
  //         if (newPos > lastPos && values[selected] < 50)
  //           values[selected]++;
  //         if (newPos < lastPos && values[selected] > 0)
  //           values[selected]--;
  //         EEPROM.write(selected, values[selected]);
  //       }
  //     }
  //     else if (selected == 3)
  //     {
  //       if (newPos > lastPos && brightness < 5)
  //         brightness++;
  //       if (newPos < lastPos && brightness > 0)
  //         brightness--;
  //       analogWrite(BACKLIGHT_PIN, map(brightness, 0, 5, 0, 255));
  //       EEPROM.write(7, brightness);
  //     }
  //     else if (selected == 4)
  //     {
  //       autoMode = !autoMode;
  //       EEPROM.write(5, autoMode);
  //       if (!autoMode && selected == 5)
  //       {
  //         selected = 0;
  //       }
  //     }
  //     else if (selected == 5 && autoMode)
  //     {
  //       if (newPos > lastPos && sValue < 2.0)
  //         sValue += 0.1;
  //       if (newPos < lastPos && sValue > 0.3)
  //         sValue -= 0.1;
  //       EEPROM.write(6, (int)(sValue * 10));
  //     }
  //   }

  //   lastPos = newPos;
  // drawScreen();
  // printMainScreen(voltages);
  // }

  /*this code moved to the begining of loop(). Need to be deleted
   const int window_size = SLIDING_AVERAGE_WINDOW;
   static float u1_arr[window_size];
   static float ub_arr[window_size];

   float u1 = slidingAverageVoltage(u1_arr, window_size, VOLTAGE_1_BANK_PIN, VOLTAGE_1_BANK_PIN_DEVIDER);
   float ub = slidingAverageVoltage(ub_arr, window_size, VOLTAGE_BATTERY_PIN, VOLTAGE_BATTERY_PIN_DEVIDER);
   float voltages[] = {u1, ub};
  */

  if (ub < BATTERY_VOLTAGE_TRESHOLD)
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

  // drawVoltage(voltages, lowVoltageProtectionActive);

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

float slidingAverageVoltage(float *window_arr, const int window_size, int pin, float devider)
{
  for (int i = 1; i < window_size; i++)
    window_arr[i - 1] = window_arr[i];

  window_arr[window_size - 1] = readVoltage(pin, devider);
  float accumulator = 0;
  int counter = 0;

  for (int i = 0; i < window_size; i++)
  {
    if (window_arr[i] > 0)
    {
      accumulator += window_arr[i];
      counter++;
    }
  }

  if (counter == 0)
    return 0;

  return (float)accumulator / (float)counter;
}

void printMainScreen(float *voltages)
{
  /* Main screen content:
  PDP:50 50 50 ms
  Ug:15.2  A0.5
  U1:2.53
  U2:2.49  Ub:5.02
  */

  /*voltages = [U1, Ub, Ug]*/

  // lcd.clear();
  // Printing all static content
  lcd.setCursor(0, 0);
  lcd.print("PDP:");
  lcd.setCursor(13, 0);
  lcd.print("ms");
  lcd.setCursor(0, 1);
  lcd.print("Ug:");
  lcd.setCursor(0, 2);
  lcd.print("U1:");
  lcd.setCursor(0, 3);
  lcd.print("U2:");
  lcd.setCursor(9, 3);
  lcd.print("Ub:");

  /* 0 row */

  lcd.setCursor(4, 0);
  lcd.print(settings.pulse_1);

  lcd.setCursor(7, 0);
  lcd.print(settings.inter_pulse_delay);

  lcd.setCursor(10, 0);
  lcd.print(settings.pulse_2);

  /* 1 row */

  lcd.setCursor(3, 1);
  lcd.print(voltages[2], 1);

  lcd.setCursor(9, 1);
  if (settings.mode == 1)
  {
    lcd.print("A");
    lcd.print(settings.auto_mode_delay / 10.0, 1);
  }
  else
  {
    lcd.print("MANUAL");
  }

  /* 2-3 row */

  lcd.setCursor(3, 2);
  lcd.print(voltages[0], 2);

  lcd.setCursor(3, 3);
  lcd.print(voltages[1] - voltages[0], 2);

  lcd.setCursor(12, 3);
  lcd.print(voltages[1], 2);
}

uint8_t loadSettings()
{
  EEPROM.get(PULSE_1, settings.pulse_1);
  EEPROM.get(PULSE_DELAY, settings.inter_pulse_delay);
  EEPROM.get(PULSE_1, settings.pulse_2);
  EEPROM.get(AUTOMODE, settings.mode);
  EEPROM.get(AM_DELAY, settings.auto_mode_delay);
  EEPROM.get(BRIGHTNESS, settings.back_light_saturation);

  if (settings.pulse_1 > PULSE_1_MAX || settings.pulse_1 < PULSE_1_MIN)
    settings.pulse_1 = PULSE_1_MIN;

  if (settings.inter_pulse_delay > PULSE_DELAY_MIN || settings.inter_pulse_delay < PULSE_DELAY_MIN)
    settings.inter_pulse_delay = PULSE_DELAY_MAX;

  if (settings.pulse_2 > PULSE_2_MAX || settings.pulse_2 < PULSE_2_MIN)
    settings.pulse_2 = PULSE_2_MIN;

  if (settings.mode != 0 && settings.mode != 1)
    settings.mode = 0;

  if (settings.auto_mode_delay > AM_DELAY_MAX || settings.auto_mode_delay < AM_DELAY_MIN)
    settings.auto_mode_delay = AM_DELAY_MAX;

  if (settings.back_light_saturation > BRIGHTNESS_MAX || settings.back_light_saturation < BRIGHTNESS_MIN)
    settings.back_light_saturation = BRIGHTNESS_MIN;

  return 1;
}

void printSettingsScreen(uint8_t cursor) // cursor 0-6, screen 0 - 4
{
  static uint8_t prev_screen = 0;
  static uint8_t prev_cursor = 0;
  const uint8_t settings_screen_parameters = SETTING_OPTIONS;
  const uint8_t screen_span = SETTINGS_SCREEN_SPAN;
  const uint8_t content_offseet_left = 2;
  const uint8_t content_offset_top = 1;
  uint8_t screen = prev_screen; // ??? test
  char static_content[settings_screen_parameters][MAX_SETTINGS_STRING_LEN] = {"Pulse_1:", "Pulse_2:", "Delay:", "Mode:", "Auto_delay:", "Back_light:", "Beeper:"};
  while (cursor - screen >= screen_span)
    screen++;
  if (screen > MAX_SETTINGS_SCREEN_NUMBER)
    screen = MAX_SETTINGS_SCREEN_NUMBER;
  while (cursor - screen < 0)
    screen--;
  if (screen < 0)
    screen = 0;
  if (prev_screen != screen)
    lcd.clear();

  /*row 0*/
  lcd.setCursor(6, 0);
  lcd.print("SETTINGS");
  for (int i = 0; i < screen_span; i++)
  {
    /*Print static conten of defined screen*/
    lcd.setCursor(content_offseet_left, i + content_offset_top);
    lcd.print(static_content[i + screen]);
    // lcd.setCursor(MAX_SETTINGS_STRING_LEN, i);

    /*Printing variable parameters from struct settings*/
    uint8_t p = i + screen;
    lcd.setCursor(MAX_SETTINGS_STRING_LEN + content_offseet_left, i + content_offset_top);

    switch (p)
    {
    case 0:
      lcd.print(settings.pulse_1);
      lcd.print("ms");
      break;
    case 1:
      lcd.print(settings.pulse_2);
      lcd.print("ms");
      break;
    case 2:
      lcd.print(settings.inter_pulse_delay);
      lcd.print("ms");
      break;
    case 3:
      lcd.print(settings.mode == 1 ? "AUTO" : "MANUAL");
      break;
    case 4:
      lcd.print(settings.auto_mode_delay / 10.0, 1);
      lcd.print("sec");
      break;
    case 5:
      lcd.print(settings.back_light_saturation);
      break;
    case 6:
      lcd.print(settings.beeper_mode == 1 ? "ON" : "OFF");
      break;
    case 7:
      lcd.print("                    ");
      break;
    default:
      lcd.print("                    ");
      break;
    }
  }
  /*Print cursor on defined position*/
  if (prev_cursor != cursor)
  {
    lcd.setCursor(0, prev_cursor - screen + content_offset_top);
    lcd.print(" ");
    beep();
  }
  lcd.setCursor(0, cursor - screen + content_offset_top);
  lcd.print("*");
  prev_screen = screen;
  prev_cursor = cursor;
}

int digitalReadDebounce(int pin)
{
  uint8_t bounce_time = 5;
  uint32_t start = millis();
  uint32_t current_time = start;
  int counter = 0;
  int pin_state_accumulator = digitalRead(pin); // pin is pulled up;
  while (current_time - start < bounce_time)
  {
    counter++;
    pin_state_accumulator += digitalRead(pin);
    current_time = millis();
  }

  return pin_state_accumulator / counter;
}