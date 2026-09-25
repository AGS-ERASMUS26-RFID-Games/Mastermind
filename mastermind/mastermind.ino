/* =========================================================================
   MASTERMIND with 4 RFID readers (RC522), 16x16 LED matrix and button
   -------------------------------------------------------------------------
   Game idea:
     The Arduino picks a secret sequence of 4 colors
     (Red / Green / Blue / Yellow, colors may repeat).
     The player places a color chip on each of the 4 readers and confirms
     with the button. The feedback appears to the right:
       Green  = right color in the right position
       Yellow = right color, but wrong position
     The feedback is NOT position-based: first all green, then all yellow
     LEDs, the rest stay off.

   Flow:
     1. Start screen (scrolling text "MASTERMIND - PRESS BUTTON"). Press the
        button -> the game starts.
        The moment the button is pressed serves as the random seed.
     2. Attempt 1 is in row 1, attempt 2 in row 2, and so on.
        - No chip on a reader   -> LED white
        - Chip present          -> LED in the chip color
        - All 4 occupied        -> row blinks, button confirms
        If the input is identical to the last attempt, nothing blinks
        and the button is ignored (protection against double confirmation).
     3. All 4 correct -> win screen, after MAX_ATTEMPTS -> lose screen.
     4. Win/lose screen: scrolling text, then the solution as 4 color blocks
        (win: below it the number of attempts, lose: below it a red X).
        Scrolling texts: "YOU WIN!" resp. "YOU LOSE".
        Press the button -> new game.

   Matrix layout (row/position counted from 1):
     Row 1..MAX_ATTEMPTS, position 3-6   : inputs
     Row 1..MAX_ATTEMPTS, position 10-13 : feedback
     Row 16, position 10-13              : solution (only if DEBUG_SHOW_SOLUTION)

   Colors: index in colors[]
       0 = White (no / unknown chip)
       1 = Red,  2 = Green,  3 = Blue,  4 = Yellow

   WIRING (Arduino UNO)
     Shared across all 4 readers:
       SCK   -> Pin 13
       MOSI  -> Pin 11
       MISO  -> Pin 12
       RST   -> Pin 9
       3.3V  -> 3.3V     (NOT 5V!)
       GND   -> GND
     Individually per reader:
       SDA/SS reader 1 -> Pin 10
       SDA/SS reader 2 -> Pin 8
       SDA/SS reader 3 -> Pin 7
       SDA/SS reader 4 -> Pin 6
     LED matrix:
       DIN   -> Pin 5
     Button:
       one contact -> Pin 2, other contact -> GND
       (internal pullup, pressed = LOW)
   ========================================================================= */

#include <SPI.h>
#include <MFRC522.h>
#include <Adafruit_GFX.h>
#include <Adafruit_NeoMatrix.h>
#include <Adafruit_NeoPixel.h>
#include <avr/pgmspace.h>

// ============================== Settings =================================

// 1 = serial output on (read UIDs, show solution, feedback)
// 0 = off (saves memory, for the finished game)
#define DEBUG 1

// true = show the secret sequence in row 16 during the game (for testing)
const bool DEBUG_SHOW_SOLUTION = true;

// Maximum number of attempts, after which the game is lost
const byte MAX_ATTEMPTS = 1;
static_assert(MAX_ATTEMPTS >= 1 && MAX_ATTEMPTS <= 15,
              "MAX_ATTEMPTS must be between 1 and 15 (row 16 is reserved for the debug solution)");

// Matrix brightness (0..255)
const byte BRIGHTNESS = 4;

// Times in milliseconds
const unsigned int BLINK_INTERVAL     = 250;   // blinking of a complete input
const unsigned int SCROLL_INTERVAL    = 70;    // speed of the scrolling texts (smaller = faster)
const unsigned int END_IMAGE_DURATION = 4000;  // how long the solution image stays
const unsigned int RESULT_PAUSE       = 1500;  // show the last feedback before the end screen appears
const byte         DEBOUNCE_TIME      = 30;    // button debounce

// --------------------------------- Pins ----------------------------------

const byte MATRIX_PIN = 5;
const byte BUTTON_PIN = 2;   // Pin 2 = interrupt 0 on the UNO

const byte READER_COUNT = 4;   // = length of the color sequence

const byte RST_PIN  = 9;   // shared by all readers
const byte SS_PIN_1 = 10;
const byte SS_PIN_2 = 8;
const byte SS_PIN_3 = 7;
const byte SS_PIN_4 = 6;

// ----------------------------- RFID timing -------------------------------

// Length of the UID in bytes. Most MIFARE Classic chips have 4 bytes.
const byte UID_LENGTH = 4;

// This many failed reads in a row before "chip removed" is assumed.
const byte MAX_FAILED_READS = 3;

// Pause between two complete polling rounds (ms)
const unsigned int POLL_INTERVAL = 30;

// Wait time after switching on an antenna (ms).
// If chips are detected unreliably: increase this first (e.g. to 10).
const byte ANTENNA_WAIT = 5;

// ------------------------------ Layout -----------------------------------

// Columns (position from 1) of the input - readers 1..4
const byte READER_LED_POSITION[READER_COUNT] = { 3, 4, 5, 6 };

// Columns (position from 1) of the feedback
const byte FEEDBACK_POSITION[READER_COUNT] = { 10, 11, 12, 13 };

// Row for the debug display of the solution
const byte SOLUTION_DEBUG_ROW = 16;

// ------------------------------ Colors -----------------------------------

const byte COLOR_COUNT = 5;

const byte COLOR_WHITE  = 0;
const byte COLOR_RED    = 1;
const byte COLOR_GREEN  = 2;
const byte COLOR_BLUE   = 3;
const byte COLOR_YELLOW = 4;

byte colors[COLOR_COUNT][3] =
{
  {255, 255, 220},  // 0 White
  {255,   0,   0},  // 1 Red
  {  0, 255,   0},  // 2 Green
  {  0,   0, 255},  // 3 Blue
  {255, 255,   0}   // 4 Yellow
};

const byte OFF[3] = { 0, 0, 0 };

// ------------------------------ Texts ------------------------------------
// Texts on the matrix. Stored in flash (PROGMEM), not in the scarce RAM.

const char TEXT_START[] PROGMEM = "MASTERMIND - PRESS BUTTON";
const char TEXT_WIN[]   PROGMEM = "YOU WIN!";
const char TEXT_LOSE[]  PROGMEM = "YOU LOSE";

// Default font: 5x7 pixels, 6 pixels wide including spacing
const byte CHAR_WIDTH = 6;
const byte TEXT_Y = 4;          // scrolling text vertically centered (rows 5-11)

// ------------------------- Chip-color table ------------------------------
// Enter the UIDs of your 16 chips here, followed by the color index (1..4).
// The table is stored in flash (PROGMEM) and thereby saves 80 bytes of RAM.

struct Chip {
  byte uid[UID_LENGTH];
  byte color;             // index in colors[]
};

const Chip CHIPS[] PROGMEM = {
  // ---- Red (1) ----
  { {0x04, 0x34, 0x13, 0xBB}, 1 },
  { {0x04, 0x6A, 0x21, 0xBB}, 1 },
  { {0x04, 0x84, 0xFD, 0xBA}, 1 },
  { {0x04, 0x36, 0x13, 0xBB}, 1 },
  // ---- Green (2) ----
  { {0x04, 0x99, 0x32, 0xBB}, 2 },
  { {0x04, 0x42, 0x26, 0xBB}, 2 },
  { {0x04, 0x71, 0x21, 0xBB}, 2 },
  { {0x04, 0x53, 0x3C, 0xBB}, 2 },
  // ---- Blue (3) ----
  { {0x04, 0xD4, 0x36, 0xBB}, 3 },
  { {0x04, 0x9A, 0x3D, 0xBB}, 3 },
  { {0x04, 0x7F, 0x31, 0xBB}, 3 },
  { {0x04, 0xB1, 0x1B, 0xBB}, 3 },
  // ---- Yellow (4) ----
  { {0x04, 0x5B, 0x12, 0xBB}, 4 },
  { {0x04, 0xE6, 0x17, 0xBB}, 4 },
  { {0x04, 0x4F, 0x26, 0xBB}, 4 },
  { {0x04, 0xA6, 0x1C, 0xBB}, 4 }
};

const byte CHIP_COUNT = sizeof(CHIPS) / sizeof(CHIPS[0]);

// ------------------------------ Objects ----------------------------------

// Pixel 0 sits top RIGHT, the first row runs to the left (zigzag).
// This makes position 1 the left one and text is displayed the right way up.
Adafruit_NeoMatrix matrix = Adafruit_NeoMatrix(
  16, 16, MATRIX_PIN,
  NEO_MATRIX_TOP + NEO_MATRIX_RIGHT +
  NEO_MATRIX_ROWS + NEO_MATRIX_ZIGZAG,
  NEO_GRB + NEO_KHZ800
);

MFRC522 reader[READER_COUNT] = {
  MFRC522(SS_PIN_1, RST_PIN),
  MFRC522(SS_PIN_2, RST_PIN),
  MFRC522(SS_PIN_3, RST_PIN),
  MFRC522(SS_PIN_4, RST_PIN)
};

// ------------------------------- State -----------------------------------

// Game states
const byte S_START = 0;   // start screen
const byte S_GAME  = 1;   // running game
const byte S_WON   = 2;   // win screen
const byte S_LOST  = 3;   // lose screen

byte state = S_START;

// Game
byte secretCode[READER_COUNT];   // color indices 1..4
byte lastAttempt[READER_COUNT];  // last confirmed input (0 = none yet)
byte attemptCount = 0;           // number of attempts already confirmed

// Blinking
bool blinkOn = true;
unsigned long lastBlinkToggle = 0;

// Scrolling text / end screen
int16_t scrollX = 16;
unsigned long lastScrollStep = 0;
bool endImageActive = false;
unsigned long endImageSince = 0;

// Reader
byte readerColor[READER_COUNT];             // color index per reader
byte currentUid[READER_COUNT][UID_LENGTH];  // UID of the chip currently present
bool chipPresent[READER_COUNT];             // is a chip currently present?
byte failedReads[READER_COUNT];             // counter for dropouts

// Button (written inside the interrupt -> volatile)
volatile bool buttonPressed = false;
volatile unsigned long lastButtonEdge = 0;
volatile unsigned long buttonTimeIsr = 0;   // micros() at press
unsigned long buttonTime = 0;               // copy for the main program

// ============================== Button ===================================

// Interrupt on every level change at pin 2.
// A press only counts if the pin is LOW and the last edge is longer ago
// than DEBOUNCE_TIME. The bouncing on press AND on release produces edges
// in quick succession and is thereby ignored.
void buttonISR() {
  unsigned long now = millis();
  if (digitalRead(BUTTON_PIN) == LOW && (now - lastButtonEdge) >= DEBOUNCE_TIME) {
    buttonPressed = true;
    buttonTimeIsr = micros();
  }
  lastButtonEdge = now;
}

// Returns true if a press occurred since the last call, and resets the
// flag. Can also be used to "discard" old presses.
bool fetchButton() {
  noInterrupts();
  bool pressed = buttonPressed;
  buttonPressed = false;
  buttonTime = buttonTimeIsr;
  interrupts();
  return pressed;
}

// ============================ LED matrix =================================

// Set one LED (row/position from 1)
void setLED(byte row, byte position, const byte color[3]) {
  matrix.drawPixel(
    position - 1,   // column
    row - 1,        // row
    matrix.Color(color[0], color[1], color[2])
  );
}

uint16_t colorValue(const byte color[3]) {
  return matrix.Color(color[0], color[1], color[2]);
}

// Draw one scrolling-text step (non-blocking).
// color == NULL -> letters cycling through Red/Green/Blue/Yellow.
// Returns true when the text has completely scrolled through.
bool scrollTextStep(const char* text, const byte* color) {
  if (millis() - lastScrollStep < SCROLL_INTERVAL) return false;
  lastScrollStep = millis();

  matrix.fillScreen(0);

  byte length = strlen_P(text);
  byte rainbowIndex = 0;

  for (byte i = 0; i < length; i++) {
    char character = pgm_read_byte(text + i);
    int16_t x = scrollX + i * CHAR_WIDTH;

    const byte* f = color;
    if (f == NULL) {
      f = colors[COLOR_RED + (rainbowIndex % 4)];
      if (character != ' ') rainbowIndex++;
    }

    if (x > -CHAR_WIDTH && x < 16) {
      uint16_t c = colorValue(f);
      matrix.drawChar(x, TEXT_Y, character, c, c, 1);   // bg == color -> transparent
    }
  }
  matrix.show();

  scrollX--;
  if (scrollX < -(int16_t)(length * CHAR_WIDTH)) {
    scrollX = 16;
    return true;
  }
  return false;
}

// ============================== RFID =====================================

// Polls one reader. Returns true if a chip is in the field,
// and writes its UID into uidTarget.
// Only the antenna of the reader being polled is on at any time.
bool readChip(MFRC522 &r, byte* uidTarget) {
  bool found = false;

  r.PCD_AntennaOn();
  delay(ANTENNA_WAIT);

  byte atqa[2];
  byte atqaSize = sizeof(atqa);

  // WUPA wakes chips from both IDLE and HALT
  MFRC522::StatusCode status = r.PICC_WakeupA(atqa, &atqaSize);

  if (status == MFRC522::STATUS_OK || status == MFRC522::STATUS_COLLISION) {
    if (r.PICC_ReadCardSerial()) {
      memcpy(uidTarget, r.uid.uidByte, UID_LENGTH);
      found = true;
    }
    r.PICC_HaltA();
  }

  r.PCD_AntennaOff();
  return found;
}

// Looks up the color index for a UID. Unknown chip -> 0 (White)
byte findColor(const byte* uid) {
  for (byte i = 0; i < CHIP_COUNT; i++) {
    if (memcmp_P(uid, CHIPS[i].uid, UID_LENGTH) == 0) {
      return pgm_read_byte(&CHIPS[i].color);
    }
  }
  return COLOR_WHITE;
}

// Is a table entry still an unfilled placeholder (all 0)?
bool isPlaceholder(byte index) {
  for (byte j = 0; j < UID_LENGTH; j++) {
    if (pgm_read_byte(&CHIPS[index].uid[j]) != 0x00) return false;
  }
  return true;
}

#if DEBUG
const char* const COLOR_NAMES[COLOR_COUNT] = {
  "White", "Red", "Green", "Blue", "Yellow"
};

// Serial output of a reader. uid == NULL -> no chip
void report(byte index, const byte* uid) {
  Serial.print(F("Reader "));
  Serial.print(index + 1);
  Serial.print(F(" -> "));

  if (uid != NULL && readerColor[index] == COLOR_WHITE) {
    Serial.print(F("Unknown chip"));
  } else {
    Serial.print(COLOR_NAMES[readerColor[index]]);
  }

  if (uid != NULL) {
    Serial.print(F("   [dec: "));
    for (byte i = 0; i < UID_LENGTH; i++) {
      Serial.print(uid[i], DEC);
      if (i < UID_LENGTH - 1) Serial.print(' ');
    }
    Serial.print(F(" | hex: "));
    for (byte i = 0; i < UID_LENGTH; i++) {
      if (uid[i] < 0x10) Serial.print('0');
      Serial.print(uid[i], HEX);
      if (i < UID_LENGTH - 1) Serial.print(' ');
    }
    Serial.print(']');
  } else {
    Serial.print(F("   [no chip]"));
  }
  Serial.println();
}

// Prints a color sequence to serial, e.g. "Red Yellow Yellow Blue"
void reportSequence(const byte* sequence) {
  for (byte i = 0; i < READER_COUNT; i++) {
    Serial.print(COLOR_NAMES[sequence[i]]);
    Serial.print(' ');
  }
}

// Checks the table at startup for typical entry mistakes
void checkTable() {
  Serial.println(F("--- Table check ---"));

  byte empty = 0;
  for (byte i = 0; i < CHIP_COUNT; i++) {
    if (isPlaceholder(i)) empty++;
  }
  if (empty > 0) {
    Serial.print(F("NOTE: "));
    Serial.print(empty);
    Serial.print(F(" of "));
    Serial.print(CHIP_COUNT);
    Serial.println(F(" entries still empty (UID 00 00 00 00)"));
  }

  byte uidA[UID_LENGTH];
  for (byte i = 0; i < CHIP_COUNT; i++) {
    if (isPlaceholder(i)) continue;
    memcpy_P(uidA, CHIPS[i].uid, UID_LENGTH);
    for (byte j = i + 1; j < CHIP_COUNT; j++) {
      if (isPlaceholder(j)) continue;
      if (memcmp_P(uidA, CHIPS[j].uid, UID_LENGTH) == 0) {
        Serial.print(F("ERROR: line "));
        Serial.print(i + 1);
        Serial.print(F(" and line "));
        Serial.print(j + 1);
        Serial.println(F(" have the same UID!"));
      }
    }
  }

  for (byte f = 1; f < COLOR_COUNT; f++) {
    byte count = 0;
    for (byte i = 0; i < CHIP_COUNT; i++) {
      if (pgm_read_byte(&CHIPS[i].color) == f) count++;
    }
    Serial.print(COLOR_NAMES[f]);
    Serial.print(F(": "));
    Serial.print(count);
    Serial.print(F(" chips"));
    if (count != 4) Serial.print(F("   <-- 4 were expected!"));
    Serial.println();
  }

  for (byte i = 0; i < CHIP_COUNT; i++) {
    byte f = pgm_read_byte(&CHIPS[i].color);
    if (f < 1 || f >= COLOR_COUNT) {
      Serial.print(F("ERROR: line "));
      Serial.print(i + 1);
      Serial.println(F(" has an invalid color index (allowed: 1..4)!"));
    }
  }

  Serial.println(F("------------------------"));
}
#endif

// Polls reader i and updates readerColor[i].
// Returns true if the displayed color has changed.
bool checkReader(byte i) {
  byte uid[UID_LENGTH];

  if (readChip(reader[i], uid)) {
    // --- A chip is present ---
    failedReads[i] = 0;

    bool isNew     = !chipPresent[i];
    bool hasSwapped = chipPresent[i] &&
                      memcmp(uid, currentUid[i], UID_LENGTH) != 0;

    if (isNew || hasSwapped) {
      memcpy(currentUid[i], uid, UID_LENGTH);
      chipPresent[i] = true;
      byte newColor  = findColor(uid);
      bool changed   = (newColor != readerColor[i]);
      readerColor[i] = newColor;
#if DEBUG
      report(i, uid);
#endif
      return changed;
    }

  } else if (chipPresent[i]) {
    // --- No response, although a chip was present before ---
    failedReads[i]++;

    if (failedReads[i] >= MAX_FAILED_READS) {
      chipPresent[i] = false;
      failedReads[i] = 0;
      bool changed   = (readerColor[i] != COLOR_WHITE);
      readerColor[i] = COLOR_WHITE;
#if DEBUG
      report(i, NULL);
#endif
      return changed;
    }
  }
  return false;
}

// ============================ Game logic =================================

void startEndScreen(bool won);   // defined further below

// Row (from 1) in which input currently happens
byte currentRow() {
  return attemptCount + 1;
}

// Is a known chip present on all readers?
bool inputComplete() {
  for (byte i = 0; i < READER_COUNT; i++) {
    if (readerColor[i] == COLOR_WHITE) return false;
  }
  return true;
}

// Complete AND different from the last attempt -> may be confirmed
bool inputConfirmable() {
  return inputComplete() &&
         memcmp(readerColor, lastAttempt, READER_COUNT) != 0;
}

// Draws the current input row (takes the blinking into account)
void drawInputRow() {
  bool visible = !inputConfirmable() || blinkOn;

  for (byte i = 0; i < READER_COUNT; i++) {
    setLED(currentRow(), READER_LED_POSITION[i],
           visible ? colors[readerColor[i]] : OFF);
  }
  matrix.show();
}

// Advance the blinking when the time is up
void blinkTick() {
  if (!inputConfirmable()) return;

  if (millis() - lastBlinkToggle >= BLINK_INTERVAL) {
    lastBlinkToggle = millis();
    blinkOn = !blinkOn;
    drawInputRow();
  }
}

// Compares an attempt with the secret sequence.
// green = right color + position, yellow = right color, wrong position
void evaluate(const byte* attempt, byte &green, byte &yellow) {
  byte remainingSecret[COLOR_COUNT]  = { 0 };
  byte remainingAttempt[COLOR_COUNT] = { 0 };

  green  = 0;
  yellow = 0;

  for (byte i = 0; i < READER_COUNT; i++) {
    if (attempt[i] == secretCode[i]) {
      green++;
    } else {
      // Only count the positions that were not hit for "yellow"
      remainingSecret[secretCode[i]]++;
      remainingAttempt[attempt[i]]++;
    }
  }

  for (byte f = 1; f < COLOR_COUNT; f++) {
    yellow += min(remainingSecret[f], remainingAttempt[f]);
  }
}

// Show the feedback: first green, then yellow, rest off
void drawFeedback(byte row, byte green, byte yellow) {
  for (byte i = 0; i < READER_COUNT; i++) {
    const byte* f = OFF;
    if (i < green)              f = colors[COLOR_GREEN];
    else if (i < green + yellow) f = colors[COLOR_YELLOW];
    setLED(row, FEEDBACK_POSITION[i], f);
  }
}

// Button was pressed during the game
void confirmAttempt() {
  if (!inputConfirmable()) {
#if DEBUG
    Serial.println(F("Button ignored (input incomplete or same as last time)"));
#endif
    return;
  }

  byte row = currentRow();
  byte green, yellow;
  evaluate(readerColor, green, yellow);

  // Leave the input fixed (not blinking) + feedback next to it
  for (byte i = 0; i < READER_COUNT; i++) {
    setLED(row, READER_LED_POSITION[i], colors[readerColor[i]]);
  }
  drawFeedback(row, green, yellow);
  matrix.show();

  memcpy(lastAttempt, readerColor, READER_COUNT);
  attemptCount++;

#if DEBUG
  Serial.print(F("Attempt "));
  Serial.print(attemptCount);
  Serial.print(F(": "));
  reportSequence(lastAttempt);
  Serial.print(F("-> green "));
  Serial.print(green);
  Serial.print(F(", yellow "));
  Serial.println(yellow);
#endif

  if (green == READER_COUNT) {
    delay(RESULT_PAUSE);
    startEndScreen(true);
  } else if (attemptCount >= MAX_ATTEMPTS) {
    delay(RESULT_PAUSE);
    startEndScreen(false);
  } else {
    // Next row: immediately shows the chips present (not blinking,
    // since identical to the attempt just confirmed)
    blinkOn = true;
    lastBlinkToggle = millis();
    drawInputRow();
  }
}

// Generate the secret sequence and start a new game
void newGame() {
  randomSeed(buttonTime);   // moment of the button press = true randomness

  for (byte i = 0; i < READER_COUNT; i++) {
    secretCode[i]  = random(1, COLOR_COUNT);   // 1..4
    lastAttempt[i] = COLOR_WHITE;              // no attempt yet
  }
  attemptCount = 0;

  matrix.fillScreen(0);

  if (DEBUG_SHOW_SOLUTION) {
    for (byte i = 0; i < READER_COUNT; i++) {
      setLED(SOLUTION_DEBUG_ROW, FEEDBACK_POSITION[i], colors[secretCode[i]]);
    }
  }

#if DEBUG
  Serial.print(F("=== New game === Solution: "));
  reportSequence(secretCode);
  Serial.println();
#endif

  blinkOn = true;
  lastBlinkToggle = millis();
  drawInputRow();

  fetchButton();   // discard old button presses
  state = S_GAME;
}

// ============================== Screens ==================================

void startStartScreen() {
  state = S_START;
  scrollX = 16;
  fetchButton();
}

void startEndScreen(bool won) {
  state = won ? S_WON : S_LOST;
  scrollX = 16;
  endImageActive = false;
  fetchButton();   // discard presses during the result pause

#if DEBUG
  Serial.println(won ? F("*** WON ***") : F("*** LOST ***"));
#endif
}

// Still image: solution as 4 color blocks (3x3), below it attempts resp. red X
void drawEndImage() {
  matrix.fillScreen(0);

  // Solution, blocks at x = 1, 5, 9, 13 and y = 1..3
  for (byte i = 0; i < READER_COUNT; i++) {
    matrix.fillRect(1 + i * 4, 2, 2, 2, colorValue(colors[secretCode[i]]));
  }

  if (state == S_WON) {
        // Number of attempts, bold (2 pixels wide) and centered
    char numStr[4];
    itoa(attemptCount, numStr, 10);
    byte length = strlen(numStr);
    const byte BOLD_WIDTH = 6;   // 5-pixel character + 1-pixel thickening
    const byte SPACING    = 2;   // gap between two digits
    byte total = length * BOLD_WIDTH + (length - 1) * SPACING;
    int16_t x = (16 - total) / 2;
    uint16_t c = colorValue(colors[COLOR_GREEN]);
    for (byte i = 0; i < length; i++) {
      int16_t xc = x + i * (BOLD_WIDTH + SPACING);
      matrix.drawChar(xc,     7, numStr[i], c, c, 1);
      matrix.drawChar(xc + 1, 7, numStr[i], c, c, 1);   // offset by 1 pixel = bold
    }

  } else {
      // Red X, 2 pixels wide (columns 4-11, rows 7-13) -> exactly centered
      uint16_t c = colorValue(colors[COLOR_RED]);
      matrix.drawLine(4, 7, 10, 13, c);   // "\" left half
      matrix.drawLine(5, 7, 11, 13, c);   // "\" right half
      matrix.drawLine(11, 7, 5, 13, c);   // "/" right half
      matrix.drawLine(10, 7, 4, 13, c);   // "/" left half
  }

  matrix.show();
}

// ============================== States ===================================

void loopStart() {
  if (fetchButton()) {
    newGame();
    return;
  }
  scrollTextStep(TEXT_START, NULL);   // colorful
}

void loopGame() {
  bool changed = false;

  for (byte i = 0; i < READER_COUNT; i++) {
    if (checkReader(i)) changed = true;
    blinkTick();   // between the readers so the blinking stays even
  }

  if (changed) {
    // Start visible after every change so the color is seen immediately
    blinkOn = true;
    lastBlinkToggle = millis();
    drawInputRow();
  }

  if (fetchButton()) {
    confirmAttempt();
    return;
  }

  delay(POLL_INTERVAL);
}

void loopEnd() {
  if (fetchButton()) {
    newGame();
    return;
  }

  if (!endImageActive) {
    bool won = (state == S_WON);
    bool done = scrollTextStep(
      won ? TEXT_WIN : TEXT_LOSE,
      won ? colors[COLOR_GREEN] : colors[COLOR_RED]
    );
    if (done) {
      endImageActive = true;
      endImageSince = millis();
      drawEndImage();
    }
  } else if (millis() - endImageSince >= END_IMAGE_DURATION) {
    endImageActive = false;   // back to scrolling text
    scrollX = 16;
  }
}

// ============================= Setup/Loop ================================

void setup() {
#if DEBUG
  Serial.begin(9600);
  Serial.println(F("=== Mastermind ==="));
#endif

  // LED matrix
  matrix.begin();
  matrix.setBrightness(BRIGHTNESS);
  matrix.setTextWrap(false);
  matrix.fillScreen(0);
  matrix.show();

  // Button
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), buttonISR, CHANGE);

  // RFID readers
  SPI.begin();
  for (byte i = 0; i < READER_COUNT; i++) {
    reader[i].PCD_Init();
    delay(10);

#if DEBUG
    // Expected "Firmware Version: 0x92" or similar - NOT 0x00 or 0xFF.
    Serial.print(F("Reader "));
    Serial.print(i + 1);
    Serial.print(F(": "));
    reader[i].PCD_DumpVersionToSerial();
#endif

    // If the range is too short, increase the sensitivity:
    // reader[i].PCD_SetAntennaGain(MFRC522::RxGain_max);

    reader[i].PCD_AntennaOff();   // only ever one antenna active at a time

    readerColor[i]  = COLOR_WHITE;
    chipPresent[i]  = false;
    failedReads[i]  = 0;
  }

#if DEBUG
  checkTable();
#endif

  startStartScreen();
}

void loop() {
  switch (state) {
    case S_START: loopStart(); break;
    case S_GAME:  loopGame();  break;
    case S_WON:
    case S_LOST:  loopEnd();   break;
  }
}
