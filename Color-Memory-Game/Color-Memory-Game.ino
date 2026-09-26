/* =========================================================================
   COLOR MEMORY GAME (Simon-Says Variant) with 4 RFID Readers (RC522),
   16x16 LED Matrix and Button
   -------------------------------------------------------------------------
   STANDALONE FILE - independent from the Mastermind sketch.
   Uses the same hardware base (readers, matrix, button,
   chip color table).

   Game idea:
     The Arduino generates a COMPLETELY NEW color sequence every round
     (the old sequence is not extended, but a new one is generated).
     Round 1 contains exactly 1 color, round 2 exactly 2 colors, etc.,
     up to MAX_MEMORY_LENGTH (20) colors.

     GRID DISPLAY:
     The memory sequence is shown on a 5x4 grid of LED positions,
     arranged on the 16x16 matrix: 4 positions per row (one
     per reader), maximum 5 rows (5 x 4 = 20 = MAX_MEMORY_LENGTH).
     Each position of the memory sequence (index 0..19) has a FIXED
     position in the grid: row = index / 4, column = index % 4.
     The first 4 colors are therefore in row 1, colors 5-8 in row 2,
     etc.

     During playback, ALL LEDs of the current sequence are shown
     simultaneously at their fixed grid positions.

     INPUT:
     The player can place up to 4 chips simultaneously on the 4 readers
     (distributed as desired). During input, the currently "active" row
     is updated live with the colors detected by the RFID readers.
     The button confirms the current block and each position is compared
     with the memory sequence:
       - correct -> the corresponding LED flashes green, then white,
                    1 point
       - wrong   -> the corresponding LED flashes red, game over

     If the complete sequence is entered correctly, the next round
     begins: a new, longer sequence is generated and displayed.

     Points = total number of correctly entered colors
     accumulated across all rounds.

   Game flow:
     1. Start screen (scrolling text). Button -> game starts
        (round 1, 1 color).
        The button press time is used as the random seed.
     2. Generate a new sequence and display it on the grid
        (blocking, but the button interrupt continues running
        in the background - old button presses are discarded afterwards).
     3. Input mode: place up to 4 chips on the readers, button
        confirms the current block.
     4. On an error -> Game Over screen with score, button -> new game.
        If the complete sequence is correct -> next round
        (new, longer sequence).

   Colors: Index in colors[]
       0 = White (no / unknown chip)
       1 = Red, 2 = Green, 3 = Blue, 4 = Yellow

   WIRING (Arduino UNO)
     All 4 RFID readers are connected and used in the game:
     Shared by all 4 readers:
       SCK   -> Pin 13
       MOSI  -> Pin 11
       MISO  -> Pin 12
       RST   -> Pin 9
       3.3V  -> 3.3V     (NOT 5V!)
       GND   -> GND
     Individually per reader:
       SDA/SS Reader 1 -> Pin 10
       SDA/SS Reader 2 -> Pin 8
       SDA/SS Reader 3 -> Pin 7
       SDA/SS Reader 4 -> Pin 6
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

// ============================ Settings ===================================

// 1 = serial output on (UID, memory sequence, score)
// 0 = off (saves memory, for the finished game)
#define DEBUG 1

// Maximum memory sequence length = 5 rows x 4 columns in the grid.
const byte MAX_MEMORY_LENGTH = 20;

// Matrix brightness (0..255)
const byte BRIGHTNESS = 4;

// Times in milliseconds
const unsigned int DISPLAY_DURATION    = 1100;  // how long the sequence is displayed
const unsigned int DISPLAY_PAUSE       = 300;   // pause after the sequence turns white
const unsigned int FEEDBACK_DURATION   = 1000;  // duration of green/red feedback
const unsigned int ROUND_PAUSE         = 600;   // pause before next round / game over screen
const unsigned int SCROLL_INTERVAL     = 70;    // scrolling text speed
const unsigned int POLL_INTERVAL       = 30;    // pause between reader checks
const byte DEBOUNCE_TIME               = 30;    // button debounce time

// --------------------------------- Pins ----------------------------------

const byte MATRIX_PIN = 5;
const byte BUTTON_PIN = 2;   // Pin 2 = interrupt 0 on UNO

const byte RESET_PIN = 9;   // shared by all 4 readers

const byte SS_PIN_1 = 10;
const byte SS_PIN_2 = 8;
const byte SS_PIN_3 = 7;
const byte SS_PIN_4 = 6;

// ----------------------------- RFID Timing -------------------------------

const byte UID_LENGTH = 4;             // UID length of chips in bytes
const byte MAX_FAILED_ATTEMPTS = 3;    // attempts before "chip removed"
const byte ANTENNA_WAIT_TIME = 5;      // wait time after antenna on (ms)

// ------------------------------ Colors -----------------------------------

const byte NUMBER_OF_COLORS = 5;

const byte COLOR_WHITE  = 0;
const byte COLOR_RED    = 1;
const byte COLOR_GREEN  = 2;
const byte COLOR_BLUE   = 3;
const byte COLOR_YELLOW = 4;

byte colors[NUMBER_OF_COLORS][3] =
{
  {255, 255, 220},  // 0 White
  {255,   0,   0},  // 1 Red
  {  0, 255,   0},  // 2 Green
  {  0,   0, 255},  // 3 Blue
  {255, 255,   0}   // 4 Yellow
};

const byte OFF[3] = { 0, 0, 0 };

// ------------------------------ Text -------------------------------------

const char START_TEXT[]    PROGMEM = "COLOR MEMORY GAME - PRESS BUTTON";
const char GAME_OVER_TEXT[] PROGMEM = "GAME OVER";

const byte CHARACTER_WIDTH = 6;   // standard font 5x7, with 6 pixel spacing
const byte TEXT_Y = 4;

// ------------------------- Chip Color Table ------------------------------
// Enter here which UID corresponds to which color.
// Stored in PROGMEM to save RAM.

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

const byte NUMBER_OF_CHIPS = sizeof(CHIPS) / sizeof(CHIPS[0]);

// ------------------------------ Objects ----------------------------------

Adafruit_NeoMatrix matrix = Adafruit_NeoMatrix(
  16, 16, MATRIX_PIN,
  NEO_MATRIX_TOP + NEO_MATRIX_RIGHT +
  NEO_MATRIX_ROWS + NEO_MATRIX_ZIGZAG,
  NEO_GRB + NEO_KHZ800
);

const byte NUMBER_OF_READERS = 4;   // 4 readers connected and active

MFRC522 readers[NUMBER_OF_READERS] = {
  MFRC522(SS_PIN_1, RESET_PIN),
  MFRC522(SS_PIN_2, RESET_PIN),
  MFRC522(SS_PIN_3, RESET_PIN),
  MFRC522(SS_PIN_4, RESET_PIN)
};

// ------------------------------ State ------------------------------------

const byte STATE_START    = 0;   // start screen
const byte STATE_DISPLAY  = 1;   // sequence is displayed
const byte STATE_INPUT    = 2;   // player enters colors
const byte STATE_GAME_OVER = 3;  // game over screen

byte gameState = STATE_START;

// Game
byte memorySequence[MAX_MEMORY_LENGTH];   // color indices 1..4
byte playerInput[MAX_MEMORY_LENGTH];

byte memoryLength = 0;          // current sequence length (= round number)
byte inputPosition = 0;         // number of colors already confirmed
long score = 0;                 // total correctly entered colors

// Scrolling text
int16_t scrollX = 16;
unsigned long lastScrollStep = 0;

// Readers 1-4 (index 0 = Reader 1, ... index 3 = Reader 4)
byte readerColor[NUMBER_OF_READERS];
byte currentUID[NUMBER_OF_READERS][UID_LENGTH];
bool chipOnReader[NUMBER_OF_READERS];
byte failedAttempts[NUMBER_OF_READERS];

// Button (written in interrupt -> volatile)
volatile bool buttonPressed = false;
volatile unsigned long lastButtonEdge = 0;
volatile unsigned long buttonTimestampISR = 0;
unsigned long buttonTimestamp = 0;

// ============================== Button ===================================

void buttonISR() {
  unsigned long now = millis();
  if (digitalRead(BUTTON_PIN) == LOW && (now - lastButtonEdge) >= DEBOUNCE_TIME) {
    buttonPressed = true;
    buttonTimestampISR = micros();
  }
  lastButtonEdge = now;
}

bool getButtonPress() {
  noInterrupts();
  bool pressed = buttonPressed;
  buttonPressed = false;
  buttonTimestamp = buttonTimestampISR;
  interrupts();
  return pressed;
}

// ============================ LED Matrix =================================

uint16_t getColorValue(const byte color[3]) {
  return matrix.Color(color[0], color[1], color[2]);
}

// --------------------------------- Grid Layout --------------------------------
// 4 columns (one per reader) x 5 rows (MAX_MEMORY_LENGTH / 4), as 5 clearly
// separated rows BELOW EACH OTHER. Each index of the memory sequence (0..19)
// has a fixed position: row = index / 4, column = index % 4.

const byte NUMBER_OF_ROWS =
  (MAX_MEMORY_LENGTH + NUMBER_OF_READERS - 1) / NUMBER_OF_READERS;  // = 5

const byte LED_X[NUMBER_OF_READERS] = { 1, 5, 10, 14 };  // 4 columns

// Input rows
const byte LED_INPUT_Y[NUMBER_OF_ROWS] = { 1, 4, 7, 10, 13 };

// Feedback rows directly below the corresponding input row
const byte LED_FEEDBACK_Y[NUMBER_OF_ROWS] = { 3, 6, 9, 12, 15 };

// Draws the LED for one memory sequence index (0-based)
// in the given color and displays it immediately.
void setField(byte index, byte colorIndex) {
  byte row = index / NUMBER_OF_READERS;
  byte column = index % NUMBER_OF_READERS;

  if (row >= NUMBER_OF_ROWS) return;

  matrix.drawPixel(
    LED_X[column],
    LED_INPUT_Y[row],
    getColorValue(colors[colorIndex])
  );

  matrix.show();
}

// Shows the result directly BELOW the input row.
// The original input remains visible.
void feedbackField(byte index, bool correct) {

  byte row = index / NUMBER_OF_READERS;
  byte column = index % NUMBER_OF_READERS;

  if (row >= NUMBER_OF_ROWS) {
    return;
  }

  // Feedback is directly below the input row.
  matrix.drawPixel(
    LED_X[column],
    LED_FEEDBACK_Y[row],
    getColorValue(colors[correct ? COLOR_GREEN : COLOR_RED])
  );
}

// Non-blocking scrolling text step. color == NULL -> multicolor.
// Returns true when the text has completely scrolled through.
bool scrollTextStep(const char* text, const byte* color) {
  if (millis() - lastScrollStep < SCROLL_INTERVAL) return false;
  lastScrollStep = millis();

  matrix.fillScreen(0);

  byte length = strlen_P(text);
  byte multicolorIndex = 0;

  for (byte i = 0; i < length; i++) {
    char character = pgm_read_byte(text + i);
    int16_t x = scrollX + i * CHARACTER_WIDTH;

    const byte* selectedColor = color;
    if (selectedColor == NULL) {
      selectedColor = colors[COLOR_RED + (multicolorIndex % 4)];
      if (character != ' ') multicolorIndex++;
    }

    if (x > -CHARACTER_WIDTH && x < 16) {
      uint16_t c = getColorValue(selectedColor);
      matrix.drawChar(x, TEXT_Y, character, c, c, 1);
    }
  }

  matrix.show();

  scrollX--;

  if (scrollX < -(int16_t)(length * CHARACTER_WIDTH)) {
    scrollX = 16;
    return true;
  }

  return false;
}

// Display score centered on the matrix
void displayScore(long value) {
  matrix.fillScreen(0);

  char number[8];
  ltoa(value, number, 10);

  byte length = strlen(number);
  byte totalWidth = length * CHARACTER_WIDTH - 1;

  int16_t x = (16 - totalWidth) / 2;
  if (x < 0) x = 0;

  uint16_t c = getColorValue(colors[COLOR_GREEN]);

  matrix.drawChar(x, TEXT_Y, number[0], c, c, 1);

  for (byte i = 1; i < length; i++) {
    matrix.drawChar(
      x + i * CHARACTER_WIDTH,
      TEXT_Y,
      number[i],
      c,
      c,
      1
    );
  }

  matrix.show();
}

// ============================== RFID =====================================

bool readChip(MFRC522 &reader, byte* targetUID) {
  bool found = false;

  reader.PCD_AntennaOn();
  delay(ANTENNA_WAIT_TIME);

  byte atqa[2];
  byte atqaSize = sizeof(atqa);

  MFRC522::StatusCode status =
    reader.PICC_WakeupA(atqa, &atqaSize);

  if (status == MFRC522::STATUS_OK ||
      status == MFRC522::STATUS_COLLISION) {

    if (reader.PICC_ReadCardSerial()) {
      memcpy(targetUID, reader.uid.uidByte, UID_LENGTH);
      found = true;
    }

    reader.PICC_HaltA();
  }

  reader.PCD_AntennaOff();

  return found;
}

byte findColor(const byte* uid) {
  for (byte i = 0; i < NUMBER_OF_CHIPS; i++) {
    if (memcmp_P(uid, CHIPS[i].uid, UID_LENGTH) == 0) {
      return pgm_read_byte(&CHIPS[i].color);
    }
  }

  return COLOR_WHITE;
}

#if DEBUG

const char* const COLOR_NAMES[NUMBER_OF_COLORS] = {
  "White", "Red", "Green", "Blue", "Yellow"
};

void reportColor(byte i) {
  Serial.print(F("Reader "));
  Serial.print(i + 1);
  Serial.print(F(" -> "));
  Serial.println(COLOR_NAMES[readerColor[i]]);
}

#endif

// Checks reader i and updates readerColor[i].
// Returns true if the displayed color changed.
bool checkReader(byte i) {

  byte uid[UID_LENGTH];

  if (readChip(readers[i], uid)) {

    bool isNew =
      !chipOnReader[i];

    bool isChanged =
      chipOnReader[i] &&
      memcmp(uid, currentUID[i], UID_LENGTH) != 0;

    if (isNew || isChanged) {

      memcpy(currentUID[i], uid, UID_LENGTH);

      chipOnReader[i] = true;
      failedAttempts[i] = 0;

      byte newColor = findColor(uid);

      bool changed =
        (newColor != readerColor[i]);

      readerColor[i] = newColor;

#if DEBUG
      reportColor(i);
#endif

      return changed;
    }

    failedAttempts[i] = 0;

  } else if (chipOnReader[i]) {

    failedAttempts[i]++;

    if (failedAttempts[i] >= MAX_FAILED_ATTEMPTS) {

      chipOnReader[i] = false;
      failedAttempts[i] = 0;

      bool changed =
        (readerColor[i] != COLOR_WHITE);

      readerColor[i] = COLOR_WHITE;

#if DEBUG
      reportColor(i);
#endif

      return changed;
    }
  }

  return false;
}

// ============================ Game Logic =================================

void startGameOver();   // defined below

// Display the complete memory sequence.
// All colors are shown simultaneously at their fixed grid positions.
void displaySequence() {

#if DEBUG

  Serial.print(F("Round "));
  Serial.print(memoryLength);
  Serial.print(F(" - Sequence: "));

  for (byte i = 0; i < memoryLength; i++) {
    Serial.print(COLOR_NAMES[memorySequence[i]]);
    Serial.print(' ');
  }

  Serial.println();

#endif

  matrix.fillScreen(0);
  matrix.show();

  delay(DISPLAY_PAUSE);

  // Display the complete current memory sequence simultaneously.
  // Each color is displayed at its fixed grid position.
  for (byte i = 0; i < memoryLength; i++) {

    byte row = i / NUMBER_OF_READERS;
    byte column = i % NUMBER_OF_READERS;

    matrix.drawPixel(
      LED_X[column],
      LED_INPUT_Y[row],
      getColorValue(colors[memorySequence[i]])
    );
  }

  // All LEDs of the current sequence are now visible simultaneously.
  matrix.show();

  // Keep the complete sequence visible.
  delay(DISPLAY_DURATION);

  // Turn all occupied positions white again.
  for (byte i = 0; i < memoryLength; i++) {

    byte row = i / NUMBER_OF_READERS;
    byte column = i % NUMBER_OF_READERS;

    matrix.drawPixel(
      LED_X[column],
      LED_INPUT_Y[row],
      getColorValue(colors[COLOR_WHITE])
    );
  }

  matrix.show();

  delay(DISPLAY_PAUSE);
}

// Displays the currently "active" row with the colors detected
// by the RFID readers.
void updateInputDisplay() {

  byte row = inputPosition / NUMBER_OF_READERS;

  if (row >= NUMBER_OF_ROWS) return;

  byte startIndex = row * NUMBER_OF_READERS;

  byte numberInRow =
    memoryLength - startIndex;

  if (numberInRow > NUMBER_OF_READERS)
    numberInRow = NUMBER_OF_READERS;

  for (byte i = 0; i < numberInRow; i++) {

    matrix.drawPixel(
      LED_X[i],
      LED_INPUT_Y[row],
      getColorValue(colors[readerColor[i]])
    );
  }

  matrix.show();
}

// Next round: generate a COMPLETELY NEW color sequence.
// The sequence becomes one color longer each round.
void nextRound() {

  if (memoryLength < MAX_MEMORY_LENGTH) {
    memoryLength++;
  }

  // If MAX_MEMORY_LENGTH is reached, the length remains the same,
  // but a completely new sequence is still generated.

  for (byte i = 0; i < memoryLength; i++) {
    memorySequence[i] =
      random(1, NUMBER_OF_COLORS);   // 1..4, completely new
  }

  // Reset reader state so that chips from the previous round
  // do not immediately affect the new grid.
  for (byte i = 0; i < NUMBER_OF_READERS; i++) {

    readerColor[i] = COLOR_WHITE;
    chipOnReader[i] = false;
    failedAttempts[i] = 0;
  }

  inputPosition = 0;

  displaySequence();

  gameState = STATE_INPUT;

  getButtonPress();   // discard button presses made during display
}

void newGame() {

  randomSeed(buttonTimestamp);   // button press time = random seed

  memoryLength = 0;
  inputPosition = 0;
  score = 0;

#if DEBUG
  Serial.println(F("=== New Game ==="));
#endif

  nextRound();
}

// Is at least one known chip placed on a reader?
bool inputAvailable() {

  for (byte i = 0; i < NUMBER_OF_READERS; i++) {

    if (readerColor[i] != COLOR_WHITE)
      return true;
  }

  return false;
}

// Builds a "block" from the currently occupied readers:
// all non-empty colors in reader order (1,2,3,4), gaps are skipped.
// Returns the number of colors in the block.
byte buildBlock(byte block[NUMBER_OF_READERS]) {

  byte count = 0;

  for (byte i = 0; i < NUMBER_OF_READERS; i++) {

    if (readerColor[i] != COLOR_WHITE) {
      block[count++] = readerColor[i];
    }
  }

  return count;
}

// Button was pressed during input: compare the current block
// (up to 4 simultaneously placed colors) with the memory sequence.
// The original input remains visible during the feedback.
// The feedback is displayed directly below the input.
// After the feedback time, both rows are cleared to BLACK.
void confirmBlock() {

  if (!inputAvailable()) {

#if DEBUG
    Serial.println(F("Button ignored (no chip placed)"));
#endif

    return;
  }

  byte block[NUMBER_OF_READERS];

  byte count =
    buildBlock(block);

  // ---------------------------------------------------------
  // Store the entered colors
  // DO NOT evaluate them yet!
  // ---------------------------------------------------------

  for (byte i = 0; i < count; i++) {

    if (inputPosition >= memoryLength) {
      break;
    }

    playerInput[inputPosition] = block[i];

#if DEBUG
    Serial.print(F("Saved position "));
    Serial.print(inputPosition + 1);
    Serial.print(F(": "));
    Serial.println(COLOR_NAMES[block[i]]);
#endif

    inputPosition++;
  }

  // ---------------------------------------------------------
  // Clear the entered input from the matrix
  // ---------------------------------------------------------

  byte startPosition =
    inputPosition - count;

  for (byte i = startPosition; i < inputPosition; i++) {

    byte row = i / NUMBER_OF_READERS;
    byte column = i % NUMBER_OF_READERS;

    if (row >= NUMBER_OF_ROWS) {
      continue;
    }

    matrix.drawPixel(
      LED_X[column],
      LED_INPUT_Y[row],
      matrix.Color(0, 0, 0)
    );
  }

  matrix.show();

  // ---------------------------------------------------------
  // Complete sequence entered?
  // ---------------------------------------------------------

  if (inputPosition >= memoryLength) {

#if DEBUG

    Serial.println(F(""));
    Serial.println(F("=== COMPLETE INPUT ==="));
    Serial.println(F("Evaluating complete sequence..."));

#endif

    bool error = false;

    // =======================================================
    // NOW evaluate the COMPLETE sequence
    // =======================================================

    for (byte i = 0; i < memoryLength; i++) {

      byte row = i / NUMBER_OF_READERS;
      byte column = i % NUMBER_OF_READERS;

      bool correct =
        (playerInput[i] == memorySequence[i]);

      if (correct) {

        // Correct -> GREEN
        feedbackField(i, true);

        score++;

#if DEBUG

        Serial.print(F("Position "));
        Serial.print(i + 1);
        Serial.println(F(": CORRECT +1"));

#endif

      } else {

        // Wrong -> RED
        feedbackField(i, false);

        error = true;

#if DEBUG

        Serial.print(F("Position "));
        Serial.print(i + 1);
        Serial.print(F(": WRONG - Expected "));
        Serial.print(COLOR_NAMES[memorySequence[i]]);
        Serial.print(F(", entered "));
        Serial.println(COLOR_NAMES[playerInput[i]]);

#endif
      }
    }

    // Show all feedback LEDs at once
    matrix.show();

    // Keep complete evaluation visible
    delay(FEEDBACK_DURATION);

    // Clear all input + feedback LEDs
    for (byte i = 0; i < memoryLength; i++) {

      byte row = i / NUMBER_OF_READERS;
      byte column = i % NUMBER_OF_READERS;

      if (row >= NUMBER_OF_ROWS) {
        continue;
      }

      matrix.drawPixel(
        LED_X[column],
        LED_INPUT_Y[row],
        matrix.Color(0, 0, 0)
      );

      matrix.drawPixel(
        LED_X[column],
        LED_FEEDBACK_Y[row],
        matrix.Color(0, 0, 0)
      );
    }

    matrix.show();

#if DEBUG

    Serial.print(F("Complete sequence evaluated. Score: "));
    Serial.println(score);

#endif

    // -------------------------------------------------------
    // At least one error -> Game Over
    // -------------------------------------------------------

    if (error) {

      delay(ROUND_PAUSE);

      startGameOver();

      return;
    }

    // -------------------------------------------------------
    // Everything correct -> next round
    // -------------------------------------------------------

    delay(ROUND_PAUSE);

    nextRound();

    return;
  }

  // ---------------------------------------------------------
  // Sequence is NOT complete yet
  // Wait for the next block
  // ---------------------------------------------------------

#if DEBUG

  Serial.print(F("Input incomplete: "));
  Serial.print(inputPosition);
  Serial.print(F(" / "));
  Serial.println(memoryLength);

#endif
}

// ============================== Screens ==================================

void startStartScreen() {

  gameState = STATE_START;

  scrollX = 16;

  getButtonPress();
}

void startGameOver() {

  gameState = STATE_GAME_OVER;

  scrollX = 16;

  getButtonPress();

#if DEBUG

  Serial.print(F("*** GAME OVER *** Score: "));
  Serial.println(score);

#endif
}

// ============================= States ====================================

void loopStart() {

  if (getButtonPress()) {

    newGame();

    return;
  }

  scrollTextStep(START_TEXT, NULL);   // multicolor
}

void loopInput() {

  bool changed = false;

  for (byte i = 0; i < NUMBER_OF_READERS; i++) {

    if (checkReader(i))
      changed = true;
  }

  if (changed) {
    updateInputDisplay();
  }

  if (getButtonPress()) {

    confirmBlock();

    return;
  }

  delay(POLL_INTERVAL);
}

// After the game over scrolling text, the score is displayed briefly.
// Afterwards the screen waits for the button to start a new game.
bool scoreDisplayed = false;
unsigned long scoreDisplayedSince = 0;

const unsigned int SCORE_DISPLAY_DURATION = 3000;

void loopGameOver() {

  if (getButtonPress()) {

    scoreDisplayed = false;

    newGame();

    return;
  }

  if (!scoreDisplayed) {

    bool finished =
      scrollTextStep(
        GAME_OVER_TEXT,
        colors[COLOR_RED]
      );

    if (finished) {

      displayScore(score);

      scoreDisplayed = true;

      scoreDisplayedSince = millis();
    }

  } else if (
    millis() - scoreDisplayedSince >=
    SCORE_DISPLAY_DURATION
  ) {

    scoreDisplayed = false;

    scrollX = 16;
  }
}

// ============================= Setup/Loop ================================

void setup() {

#if DEBUG

  Serial.begin(9600);

  Serial.println(F("=== Color Memory Game ==="));

#endif

  matrix.begin();

  matrix.setBrightness(BRIGHTNESS);

  matrix.setTextWrap(false);

  matrix.fillScreen(0);

  matrix.show();

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  attachInterrupt(
    digitalPinToInterrupt(BUTTON_PIN),
    buttonISR,
    CHANGE
  );

  SPI.begin();

  for (byte i = 0; i < NUMBER_OF_READERS; i++) {

    readers[i].PCD_Init();

    delay(10);

#if DEBUG

    // Expected "Firmware Version: 0x92" or similar - NOT 0x00 or 0xFF.
    Serial.print(F("Reader "));
    Serial.print(i + 1);
    Serial.print(F(": "));

    readers[i].PCD_DumpVersionToSerial();

#endif

    readers[i].PCD_AntennaOff();   // only one antenna active at a time
  }

  for (byte i = 0; i < NUMBER_OF_READERS; i++) {

    readerColor[i] = COLOR_WHITE;

    chipOnReader[i] = false;

    failedAttempts[i] = 0;
  }

  startStartScreen();
}

void loop() {

  switch (gameState) {

    case STATE_START:
      loopStart();
      break;

    case STATE_INPUT:
      loopInput();
      break;

    case STATE_GAME_OVER:
      loopGameOver();
      break;

    // STATE_DISPLAY is not processed through loop().
    // The sequence is displayed blocking inside nextRound()
    // by displaySequence().
  }
}