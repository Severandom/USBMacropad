/*
 * Pi Game — Arduino Nano
 * -------------------------------------------------------
 * Pinout:
 *   Encoder CLK (A)  → D2  (INT0)
 *   Encoder DT  (B)  → D3  (INT1)
 *   Encoder SW       → D4
 *   Encoder GND (C)  → GND
 *   Encoder SW (E)   → GND
 *
 *   LCD SDA          → A4
 *   LCD SCL          → A5
 *   LCD VCC          → 5V
 *   LCD GND          → GND
 *
 *   LED digit 1      → D5
 *   LED digit 2      → D6
 *   LED digit 3      → D7
 *   LED digit 4      → D8
 *   LED digit 5      → D9
 *   LED digit 6      → D10
 *   LED digit 7      → D11
 *   LED digit 8      → D12
 *   LED digit 9      → D13
 *   LED digit 0      → A0
 *
 *   Battery +        → VIN
 *   Battery −        → GND
 *
 * Libraries required:
 *   LiquidCrystal_I2C  (Frank de Brabander / johnrickman)
 *
 * Encoder zone map (0° = bottom, CCW increases):
 *   0°  – 36°  → digit 1
 *   36° – 72°  → digit 2
 *   72° – 108° → digit 3
 *   108°– 144° → digit 4
 *   144°– 180° → digit 5
 *   180°– 216° → digit 6
 *   216°– 252° → digit 7
 *   252°– 288° → digit 8
 *   288°– 324° → digit 9
 *   324°– 360° → digit 0
 * -------------------------------------------------------
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>

// ── LCD (change address to 0x3F if screen stays blank) ─
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ── Encoder pins ───────────────────────────────────────
#define ENC_CLK  2
#define ENC_DT   3
#define ENC_SW   4

// ── LED pins indexed by digit 0–9 ─────────────────────
const uint8_t LED_PINS[10] = {
  A0,  // digit 0
  5,   // digit 1
  6,   // digit 2
  7,   // digit 3
  8,   // digit 4
  9,   // digit 5
  10,  // digit 6
  11,  // digit 7
  12,  // digit 8
  13   // digit 9
};

// ── 1000 digits of Pi stored in program memory ────────
const char PI_DIGITS[] PROGMEM =
  "3141592653589793238462643383279502884197169399375105"
  "8209749445923078164062862089986280348253421170679821"
  "4808651328230664709384460955058223172535940812848111"
  "7450284102701938521105559644622948954930381964428810"
  "9756659334461284756482337867831652712019091456485669"
  "2346034861045432664821339360726024914127372458700660"
  "6315588174881520920962829254091715364367892590360011"
  "3305305488204665213841469519415116094330572703657595"
  "9195309218611738193261179310511854807446237996274956"
  "7351885752724891227938183011949129833673362440656643"
  "0860213949463952247371907021798609437027705392171762"
  "9317675238467481846766940513200056812714526356082778"
  "5771342757789609173637178721468440901224953430146549"
  "5853710507922796892589235420199561121290219608640344"
  "1815981362977477130996051870721134999999837297804995"
  "1059731732816096318595024459455346908302642522308253"
  "3446850352619311881710100031378387528865875332083814"
  "2061717766914730359825349042875546873115956286388235"
  "3787593751957781857780532171226806613001927876611195"
  "9092164201989";

#define MAX_DIGITS   1000
#define EEPROM_HI_LO 0    // high score stored as 2 bytes at address 0–1

// ── Game states ────────────────────────────────────────
enum State { BOOT, IDLE, INPUT };
State gameState = BOOT;

// ── Encoder ────────────────────────────────────────────
volatile int encTicks = 0;
int          encPrev  = 0;
int          encAngle = 0;   // 0–359°, CCW positive, 0 = bottom
int8_t       curZone  = -1;  // digit currently under the marker

// ── Button ─────────────────────────────────────────────
unsigned long btnTime      = 0;
bool          btnPrevState = HIGH;
#define DEBOUNCE_MS 40

// ── Game ───────────────────────────────────────────────
uint16_t highScore    = 0;
uint16_t shownCount   = 0;  // digits shown so far (increases by 1 each round)
uint16_t inputIndex   = 0;  // which digit user is currently confirming

// ── LED flash helper ───────────────────────────────────
#define FLASH_COUNT    4
#define FLASH_DELAY_MS 120

// ══════════════════════════════════════════════════════
void IRAM_ATTR encISR() {
  if (digitalRead(ENC_CLK) != digitalRead(ENC_DT)) encTicks++;
  else                                               encTicks--;
}

// ══════════════════════════════════════════════════════
uint8_t piDigit(uint16_t idx) {
  return (uint8_t)(pgm_read_byte(&PI_DIGITS[idx]) - '0');
}

int angleDeg(int ticks) {
  // EC11E15244G1: 15 pulses per revolution, each pulse = 2 ISR edges
  // → 30 ISR events per rev, each = 12°
  int deg = ((ticks * 12) % 360 + 360) % 360;
  return deg;
}

uint8_t zoneToDigit(int angle) {
  // zone 0 (0–36°) → digit 1 … zone 8 (288–324°) → digit 9 … zone 9 (324–360°) → digit 0
  uint8_t z = (uint8_t)(angle / 36) % 10;
  return (z == 9) ? 0 : z + 1;
}

void allOff() {
  for (uint8_t i = 0; i < 10; i++) digitalWrite(LED_PINS[i], LOW);
}

void lightDigit(uint8_t d) {
  allOff();
  digitalWrite(LED_PINS[d], HIGH);
}

void flashDigit(uint8_t d) {
  for (uint8_t i = 0; i < FLASH_COUNT; i++) {
    digitalWrite(LED_PINS[d], LOW);
    delay(FLASH_DELAY_MS);
    digitalWrite(LED_PINS[d], HIGH);
    delay(FLASH_DELAY_MS);
  }
  allOff();
}

uint16_t loadHigh() {
  uint16_t h = ((uint16_t)EEPROM.read(EEPROM_HI_LO) << 8)
                | EEPROM.read(EEPROM_HI_LO + 1);
  return (h > MAX_DIGITS) ? 0 : h;
}

void saveHigh(uint16_t s) {
  EEPROM.write(EEPROM_HI_LO,     (s >> 8) & 0xFF);
  EEPROM.write(EEPROM_HI_LO + 1, s & 0xFF);
}

void showIdle() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Press 0 to begin");
  lcd.setCursor(0, 1);
  lcd.print("Highscore: ");
  lcd.print(highScore);
}

void showCurrentDigitScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Current digit:#");
  lcd.print(shownCount);          // 1-based: shownCount is already incremented
  lcd.setCursor(0, 1);
  lcd.print("Enter #");
  lcd.print(inputIndex + 1);
  lcd.print(" of ");
  lcd.print(shownCount);
}

bool buttonPressed() {
  bool now     = digitalRead(ENC_SW);
  bool pressed = false;
  if (now == LOW && btnPrevState == HIGH &&
      (millis() - btnTime) > DEBOUNCE_MS) {
    pressed = true;
    btnTime = millis();
  }
  btnPrevState = now;
  return pressed;
}

// ══════════════════════════════════════════════════════
void setup() {
  for (uint8_t i = 0; i < 10; i++) {
    pinMode(LED_PINS[i], OUTPUT);
    digitalWrite(LED_PINS[i], LOW);
  }
  pinMode(ENC_CLK, INPUT_PULLUP);
  pinMode(ENC_DT,  INPUT_PULLUP);
  pinMode(ENC_SW,  INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ENC_CLK), encISR, CHANGE);

  lcd.init();
  lcd.backlight();
  highScore = loadHigh();

  // Boot screen
  lcd.clear();
  lcd.setCursor(4, 0);
  lcd.print("Pi Game");
  delay(2000);

  showIdle();
  gameState = IDLE;
}

// ══════════════════════════════════════════════════════
void loop() {
  // ── Update encoder ──────────────────────────────────
  noInterrupts();
  int ticksCopy = encTicks;
  interrupts();

  if (ticksCopy != encPrev) {
    encAngle = angleDeg(ticksCopy);
    encPrev  = ticksCopy;
    uint8_t newZone = zoneToDigit(encAngle);
    if ((int8_t)newZone != curZone) {
      curZone = (int8_t)newZone;
      // Always show zone LED so user can navigate
      lightDigit(curZone);
    }
  }

  bool btn = buttonPressed();

  // ════════════════════════════════════════════════════
  //  IDLE — waiting for user to select 0 and press
  // ════════════════════════════════════════════════════
  if (gameState == IDLE) {
    if (btn && curZone == 0) {
      // Start game
      shownCount  = 0;
      inputIndex  = 0;

      // Show first digit (3)
      shownCount = 1;
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Digit 1 is:  ");
      lcd.print(piDigit(0));
      lcd.setCursor(0, 1);
      lcd.print("Now enter it!");
      lightDigit(piDigit(0));
      delay(2000);

      inputIndex = 0;
      showCurrentDigitScreen();
      lightDigit(piDigit(inputIndex));
      gameState = INPUT;
    }
    return;
  }

  // ════════════════════════════════════════════════════
  //  INPUT — user entering the sequence
  // ════════════════════════════════════════════════════
  if (gameState == INPUT) {
    uint8_t expected = piDigit(inputIndex);

    // Keep pending digit lit
    if (curZone != expected) {
      lightDigit(curZone);   // show where encoder is
    } else {
      lightDigit(expected);  // light the correct one when aligned
    }

    if (btn) {
      if ((uint8_t)curZone == expected) {
        // ── Correct digit ────────────────────────────
        flashDigit(expected);

        inputIndex++;

        if (inputIndex >= shownCount) {
          // Completed this round — advance to next digit
          if (shownCount >= MAX_DIGITS) {
            // ── WIN ──────────────────────────────────
            allOff();
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Congratulations!");
            lcd.setCursor(0, 1);
            lcd.print("You got 1000!   ");
            if ((uint16_t)MAX_DIGITS > highScore) {
              highScore = MAX_DIGITS;
              saveHigh(highScore);
            }
            while (true) { /* freeze on win screen */ }
          }

          // Show next Pi digit
          shownCount++;
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("Digit ");
          lcd.print(shownCount);
          lcd.print(" is: ");
          lcd.print(piDigit(shownCount - 1));
          lcd.setCursor(0, 1);
          lcd.print("Now enter all!");
          lightDigit(piDigit(shownCount - 1));
          delay(2000);

          inputIndex = 0;
          showCurrentDigitScreen();
          lightDigit(piDigit(inputIndex));
        } else {
          // More in this round
          showCurrentDigitScreen();
          lightDigit(piDigit(inputIndex));
        }

      } else {
        // ── Wrong digit ──────────────────────────────
        allOff();

        // Score = number of fully completed rounds
        uint16_t score = shownCount - 1;
        if (score > highScore) {
          highScore = score;
          saveHigh(highScore);
        }

        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Oops! Correct:");
        lcd.print(expected);
        lcd.setCursor(0, 1);
        lcd.print("Highscore:");
        lcd.print(highScore);
        lcd.print(" digits");

        delay(3500);

        showIdle();
        curZone   = -1;
        gameState = IDLE;
      }
    }
    return;
  }
}
