#include <Wire.h>
#include <string.h>
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
#define ENCODER_INCREMENT 2
/*limits*/
#define PULSE_1_MIN 1
#define PULSE_1_MAX 50
#define PULSE_2_MIN 0
#define PULSE_2_MAX 50
#define PULSE_DELAY_MIN 10
#define PULSE_DELAY_MAX 99
#define MODE_MIN 0
#define MODE_MAX 1
#define AM_DELAY_MIN 3
#define AM_DELAY_MAX 20
#define BRIGHTNESS_MIN 0
#define BRIGHTNESS_MAX 5
#define BEEPER_MIN 0
#define BEEPER_MAX 1
#define MAX_SETTINGS_STRING_LEN 12
#define SETTING_OPTIONS 7
#define SETTINGS_SCREEN_SPAN 3
#define SCREEN_ROW_LENGTH 20
#define SCREEN_ROWS_NUMBER 4
#define MAX_SETTINGS_SCREEN_NUMBER SETTING_OPTIONS - SETTINGS_SCREEN_SPAN

/*Settings screen view

   _____Screen 0_______     ______Screen 1______     ______Screen 2______    ______Screen 3______
  |     SETTINGS       |   |     SETTINGS       |   |     SETTINGS       |   |     SETTINGS       |
 0|* Pulse_1:   50ms   |   |  Pulse_2:   50ms   |   |  Delay:     50ms   |   |  Mode:      AUTO   |
 1|  Pulse_2:   50ms   |   |  Delay:     50ms   |   |  Mode:      AUTO   |   |  Auto_delay:0.5sec |
 2|  Delay:     50ms   |   |* Mode:      AUTO   |   |* Auto_delay:0.5sec |   |* Back_light:4      |
   --------------------     --------------------     --------------------    ---------------------
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
int8_t current_settings_option = -1;
uint8_t last_settings_cursor = 0;

typedef enum
{
  PULSE_1,
  PULSE_DELAY,
  PULSE_2,
  AUTOMODE = 5,
  AM_DELAY,
  BRIGHTNESS,
  BEEPER

} Eeprom;

struct
{
  uint8_t pulse_1;               /*1-50 ms*/
  uint8_t pulse_2;               /*0-50 ms*/
  uint8_t inter_pulse_delay;     /*10-99 ms*/
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
void saveSettings();
uint8_t printSettingsScreen(Encoder *encoder); // define
int digitalReadDebounce(int pin);
void changeSettingsOption(uint8_t option, int8_t increment);

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
  sValue = (float)settings.auto_mode_delay / 10.0;
  brightness = settings.back_light_saturation;
  // settings.mode = EEPROM.read(5);
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

    // uint8_t cursor_max = (SETTING_OPTIONS - 1) * ENCODER_INCREMENT; // custor starts from 0
    // uint8_t cursor_min = 0 * ENCODER_INCREMENT;
    // int8_t cursor = enc.read();
    // // need to move this check into function printSettingsScreen()
    // if (cursor > cursor_max)
    // {
    //   enc.write(cursor_max);
    //   cursor = enc.read();
    // }
    // else if (cursor < cursor_min)
    // {
    //   enc.write(cursor_min);
    //   cursor = enc.read();
    // }
    ///////////////////////////////

    printSettingsScreen(&enc);
    if (current_settings_option >= 0 && current_settings_option < SETTING_OPTIONS)
    {
      int8_t increment = enc.read() / ENCODER_INCREMENT - current_settings_option;
      changeSettingsOption(current_settings_option, increment);
      enc.write(current_settings_option * ENCODER_INCREMENT);
    }
    // enc.write(actual_cursor);
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
          if (screen_shown == 1 && current_settings_option == -1)
          {
            current_settings_option = enc.read() / ENCODER_INCREMENT;
          }
          else if (screen_shown == 1 && current_settings_option >= 0 && current_settings_option < SETTING_OPTIONS)
          {
            // Change current_settings_option = -1
            enc.write(current_settings_option * ENCODER_INCREMENT);
            current_settings_option = -1;
            saveSettings();
            // Save settings to EEPROM
          }
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
      beep();
      if (screen_shown == 1)
      {
        last_settings_cursor = enc.read();
        screen_shown = 0;
      }
      else if (screen_shown == 0)
      {
        screen_shown = 1;
        enc.write(last_settings_cursor);
      }
      press_time = 0;
    }
  }
  prev_b_state = b_state;
  //////////////////

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

  if (settings.mode && !lowVoltageProtectionActive)
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
  //       selected = (selected + 1) % (settings.mode ? 6 : 5); // В MAN пропускаем S
  //     }
  //     else
  //     {
  //       selected = (selected + (settings.mode ? 5 : 4)) % (settings.mode ? 6 : 5);
  //     }
  //   }
  //   else
  //   {
  //     if (selected < 3)
  //     {
  //       if (selected == 2)
  //       { // P2 с поддержкой OFF
  //         if (newPos > lastPos && settings.pulse_2 < 50)
  //           settings.pulse_2++;
  //         if (newPos < lastPos && settings.pulse_2 > 0)
  //           settings.pulse_2--;
  //         EEPROM.write(2, settings.pulse_2);
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
  //       settings.mode = !settings.mode;
  //       EEPROM.write(5, settings.mode);
  //       if (!settings.mode && selected == 5)
  //       {
  //         selected = 0;
  //       }
  //     }
  //     else if (selected == 5 && settings.mode)
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

  if (!settings.mode && digitalRead(START_PIN) == LOW && !startButtonState && !lowVoltageProtectionActive)
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
        if (i == 2 && settings.pulse_2 == 0)
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
        lcd.print(settings.mode ? "AUTO " : "MAN  ");
      }
    }
  }

  // Если режим AUTO — рисуем S, иначе стираем строку
  lcd.setCursor(10, 2);
  if (settings.mode)
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
  delay(settings.pulse_1);
  digitalWrite(OUTPUT_PIN, LOW);

  if (settings.pulse_2 > 0)
  {
    delay(10);
    delay(settings.inter_pulse_delay);
    digitalWrite(OUTPUT_PIN, HIGH);
    delay(settings.pulse_2);
    digitalWrite(OUTPUT_PIN, LOW);
  }
}

float readVoltage(int pin, float voltageMultiplier)
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
  EEPROM.get(PULSE_2, settings.pulse_2);
  EEPROM.get(AUTOMODE, settings.mode);
  EEPROM.get(AM_DELAY, settings.auto_mode_delay);
  EEPROM.get(BRIGHTNESS, settings.back_light_saturation);

  if (settings.pulse_1 > PULSE_1_MAX || settings.pulse_1 < PULSE_1_MIN)
    settings.pulse_1 = PULSE_1_MIN;

  if (settings.inter_pulse_delay > PULSE_DELAY_MAX || settings.inter_pulse_delay < PULSE_DELAY_MIN)
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

void saveSettings()
{
  EEPROM.put(PULSE_1, settings.pulse_1);
  EEPROM.put(PULSE_DELAY, settings.inter_pulse_delay);
  EEPROM.put(PULSE_2, settings.pulse_2);
  EEPROM.put(AUTOMODE, settings.mode);
  EEPROM.put(AM_DELAY, settings.auto_mode_delay);
  EEPROM.put(BRIGHTNESS, settings.back_light_saturation);
  EEPROM.put(BEEPER, settings.beeper_mode);
}

uint8_t printSettingsScreen(Encoder *encoder)
{
  const char static_content[SETTING_OPTIONS][MAX_SETTINGS_STRING_LEN] = {"Pulse_1:", "Pulse_2:", "Delay:", "Mode:", "Auto_delay:", "Back_light:", "Beeper:"};
  const char header[] = "SETTINGS";
  const char cursor_sign[2][2] = {"*", ">"};

  const uint8_t content_offset_top = 1; // For top row information line
  const uint8_t cursor_area = 2;
  const uint8_t parameter_name_area = 11;
  const uint8_t value_area = 7;

  static uint8_t prev_screen = 0;
  static uint8_t prev_option_number = 0;
  uint8_t screen = prev_screen;
  uint8_t option_number = prev_option_number;

  int8_t encP = encoder->read();
  if (current_settings_option == -1)
  {
    if (encP > (SETTING_OPTIONS - 1) * ENCODER_INCREMENT)
      encP = (SETTING_OPTIONS - 1) * ENCODER_INCREMENT;
    else if (encP < 0)
      encP = 0;

    encoder->write(encP);
    option_number = encP / ENCODER_INCREMENT;

    while (option_number - screen >= SETTINGS_SCREEN_SPAN)
      screen++;
    if (screen > MAX_SETTINGS_SCREEN_NUMBER)
      screen = MAX_SETTINGS_SCREEN_NUMBER;
    while (option_number - screen < 0)
      screen--;
    if (screen < 0)
      screen = 0;
    if (prev_screen != screen)
      lcd.clear();
  }

  for (int i = 0; i < SCREEN_ROWS_NUMBER; i++)
  {
    char row[SCREEN_ROW_LENGTH + 1];
    row[0] = '\0';
    // header formatting
    if (i == 0)
    {
      char full_header[SCREEN_ROW_LENGTH + 1];

      strcpy(full_header, header);
      if (current_settings_option >= 0 && current_settings_option < SETTING_OPTIONS)
      {
        strcat(full_header, ">");
        const uint8_t optionLen = strlen(static_content[current_settings_option]);
        strncat(full_header, static_content[current_settings_option], optionLen - 1); // delete column from option name
      }
      uint8_t whitespaces = SCREEN_ROW_LENGTH - strlen(full_header);
      while (strlen(row) < whitespaces / 2)
        strcat(row, " ");

      strcat(row, full_header);

      while (strlen(row) < SCREEN_ROW_LENGTH)
        strcat(row, " ");
    }
    else
    {
      // variable content formatting
      uint8_t cursor_position = option_number - screen + content_offset_top;
      uint8_t option = screen + i - content_offset_top;
      char value[4];

      if (i == cursor_position)
      {
        if (current_settings_option >= 0 && current_settings_option < SETTING_OPTIONS)
          strcpy(row, cursor_sign[1]);
        else
          strcpy(row, cursor_sign[0]);
      }
      while (strlen(row) < cursor_area)
        strcat(row, " ");

      strcat(row, static_content[screen + i - content_offset_top]);

      while (strlen(row) < cursor_area + parameter_name_area)
        strcat(row, " ");

      switch (option)
      {
      case 0:
        sprintf(value, "%d", settings.pulse_1);
        strcat(row, value);
        strcat(row, "ms");
        break;
      case 1:
        sprintf(value, "%d", settings.pulse_2);
        strcat(row, value);
        strcat(row, "ms");
        break;
      case 2:
        sprintf(value, "%d", settings.inter_pulse_delay);
        strcat(row, value);
        strcat(row, "ms");
        break;
      case 3:
        strcat(row, settings.mode == 1 ? "AUTO" : "MANUAL");
        break;
      case 4:
        sprintf(value, "%d.%01d", settings.auto_mode_delay / 10, settings.auto_mode_delay % 10);
        Serial.print("Auto_delay:");
        Serial.println(value);
        strcat(row, value);
        strcat(row, "sec");
        break;
      case 5:
        sprintf(value, "%d", settings.back_light_saturation);
        strcat(row, value);
        break;
      case 6:
        strcat(row, settings.beeper_mode == 1 ? "ON" : "OFF");
        break;
      }
      while (strlen(row) < cursor_area + parameter_name_area + value_area)
        strcat(row, " ");

      if (current_settings_option >= 0 && current_settings_option < SETTING_OPTIONS && i != cursor_position)
      {
        row[0] = '\0';
        while (strlen(row) < SCREEN_ROW_LENGTH)
          strcat(row, " ");
      }
    }
    lcd.setCursor(0, i);
    lcd.print(row);
  }

  prev_screen = screen;
  prev_option_number = option_number;
  return 1;
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

void changeSettingsOption(uint8_t option, int8_t increment)
{
  uint8_t s = 0;
  switch (option)
  {
  case 0:
    s = settings.pulse_1 + increment;
    settings.pulse_1 = s > PULSE_1_MIN ? (s < PULSE_1_MAX ? s : PULSE_1_MAX) : PULSE_1_MIN;
    break;
  case 1:
    s = settings.pulse_2 + increment;
    settings.pulse_2 = s > PULSE_2_MIN ? (s < PULSE_2_MAX ? s : PULSE_2_MAX) : PULSE_2_MIN;
    break;
  case 2:
    s = settings.inter_pulse_delay + increment;
    settings.inter_pulse_delay = s > PULSE_DELAY_MIN ? (s < PULSE_DELAY_MAX ? s : PULSE_DELAY_MAX) : PULSE_DELAY_MIN;
    break;
  case 3:
    s = settings.mode + increment;
    settings.mode = s > MODE_MIN ? (s < MODE_MAX ? s : MODE_MAX) : MODE_MIN;
    break;
  case 4:
    s = settings.auto_mode_delay + increment;
    settings.auto_mode_delay = s > AM_DELAY_MIN ? (s < AM_DELAY_MAX ? s : AM_DELAY_MAX) : AM_DELAY_MIN;
    break;
  case 5:
    s = settings.back_light_saturation + increment;
    settings.back_light_saturation = s > BRIGHTNESS_MIN ? (s < BRIGHTNESS_MAX ? s : BRIGHTNESS_MAX) : BRIGHTNESS_MIN;
    break;
  case 6:
    s = settings.beeper_mode + increment;
    settings.beeper_mode = s > BEEPER_MIN ? (s < BEEPER_MAX ? s : BEEPER_MAX) : BEEPER_MIN;
    break;
  }
}