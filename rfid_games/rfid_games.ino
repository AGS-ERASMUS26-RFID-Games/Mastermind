/* =========================================================================
   RFID GAMES: MASTERMIND + COLOR MEMORY GAME
   4 RFID readers (RC522), 16x16 LED matrix and 3 buttons
   -------------------------------------------------------------------------
   Two games share the same hardware. Button 2 selects Mastermind,
   button 3 selects the Color Memory Game, button 1 starts a game and
   confirms inputs. After switching on, Mastermind is selected.

   Game selection:
     On the start and end screens a short press of button 2 / 3 shows the
     start screen of the selected game.
     During a running game a short press of button 2 / 3 is ignored, so an
     accidental press never ends a game. To quit a running game, HOLD
     button 2 or 3 for ABORT_HOLD_TIME (3 s): the game is ended and the
     start screen of the selected game appears.

   MASTERMIND
     The Arduino picks a secret sequence of 4 colors
     (Red / Green / Blue / Yellow, colors may repeat).
     The player places a color chip on each of the 4 readers and confirms
     with button 1. The feedback appears to the right:
       Green  = right color in the right position
       Yellow = right color, but wrong position
     The feedback is NOT position-based: first all green, then all yellow
     LEDs, the rest stay off.

     Flow:
       1. Start screen (scrolling text "MASTERMIND - PRESS BUTTON"). Press
          button 1 -> the game starts.
          The moment the button is pressed serves as the random seed.
       2. Attempt 1 is in row 1, attempt 2 in row 2, and so on.
          - No chip on a reader   -> LED white
          - Chip present          -> LED in the chip color
          - All 4 occupied        -> row blinks, button 1 confirms
          If the input is identical to the last attempt, nothing blinks
          and the button is ignored (protection against double confirmation).
       3. All 4 correct -> win screen, after MM_MAX_ATTEMPTS -> lose screen.
       4. Win/lose screen: scrolling text, then the solution as 4 color
          blocks (win: below it the number of attempts, lose: a red X).
          Scrolling texts: "YOU WIN!" resp. "YOU LOSE".
          Button 1 -> new game.

     Matrix layout (row/position counted from 1):
       Row 1..MM_MAX_ATTEMPTS, position 3-6   : inputs
       Row 1..MM_MAX_ATTEMPTS, position 10-13 : feedback
       Row 16, position 10-13                 : solution (only if MM_DEBUG_SHOW_SOLUTION)

   COLOR MEMORY GAME
     Every round the Arduino generates a COMPLETELY NEW random sequence
     (the old one is not extended). Round 1 has 1 color, round 2 has 2
     colors, and so on up to CM_MAX_LENGTH (20). After that every round
     has 20 colors (endless).

     Flow:
       1. Start screen (scrolling text "COLOR MEMORY GAME - PRESS BUTTON").
          Button 1 -> round 1 starts (random seed = moment of the press).
       2. The whole sequence is shown at once on a grid of 5 rows x 4
          positions. Sequence position n has a fixed place:
          grid row = n / 4, grid position = n % 4. The display time grows
          with the length (CM_SHOW_TIME_BASE + CM_SHOW_TIME_PER_COLOR per
          color). Then all used grid places turn white.
       3. Input in blocks of 4: reader 1..4 = position 1..4 of the current
          grid row. The last block only needs as many readers as colors are
          left; the readers not needed are not read at all and can never
          get a chip (-> no cross-reading onto an empty reader, see
          activeReaderCount()). When all required readers carry a known chip, the row
          blinks and button 1 confirms the block. Confirmed blocks stay
          visible in their colors. Before the next block can be confirmed,
          the chips have to be removed from the required readers (this
          also prevents a double confirmation).
       4. Only after the complete sequence has been entered is it compared
          position by position: directly below every color a green (correct)
          or red (wrong) LED appears. Every correct position = 1 point. The
          score adds up over all rounds of a game.
       5. Everything correct -> next round. At least one wrong position ->
          Game over screen: scrolling text "GAME OVER", then the score.
          Button 1 -> new game.

     Matrix layout (row/position counted from 1):
       Grid positions 2, 6, 11, 15
       Input rows 2, 5, 8, 11, 14 - feedback directly below: 3, 6, 9, 12, 15

   Colors: index in colors[]
       0 = White (no / unknown chip)
       1 = Red,  2 = Green,  3 = Blue,  4 = Yellow

   WIRING (Arduino Mega 2560)
     Shared across all 4 readers (hardware SPI of the Mega):
       SCK   -> Pin 52
       MOSI  -> Pin 51
       MISO  -> Pin 50
       RST   -> Pin 9
       3.3V  -> 3.3V     (NOT 5V!)
       GND   -> GND
       SCK/MOSI/MISO are also available on the 6-pin ICSP header.
       Pins 11/12/13 are NOT connected to SPI on the Mega!
     Individually per reader:
       SDA/SS reader 1 -> Pin 10
       SDA/SS reader 2 -> Pin 8
       SDA/SS reader 3 -> Pin 7
       SDA/SS reader 4 -> Pin 6
     LED matrix:
       DIN   -> Pin 5
     Buttons (one contact -> pin, other contact -> GND,
     internal pullup, pressed = LOW, all interrupt-capable):
       Button 1 (start / confirm)          -> Pin 2
       Button 2 (select Mastermind)        -> Pin 3
       Button 3 (select Color Memory Game) -> Pin 18 (TX1, free as long as
                                              Serial1 is not used)
     Pin 53 (hardware SS) stays unconnected. It must remain an OUTPUT so
     the Mega stays SPI master - SPI.begin() takes care of that.
   ========================================================================= */

// This sketch and its pin assignment are made for the Mega 2560.
// Select "Arduino Mega or Mega 2560" under Tools > Board.
#if !defined(__AVR_ATmega2560__)
#error "Wrong board selected: this sketch is wired for the Arduino Mega 2560."
#endif

#include <SPI.h>
#include <MFRC522.h>
#include <Adafruit_GFX.h>
#include <Adafruit_NeoMatrix.h>
#include <Adafruit_NeoPixel.h>
#include <Fonts/TomThumb.h>   // small 3x5 font for numbers with 3-4 digits
#include <avr/pgmspace.h>

// ============================== Settings =================================

// 1 = serial output on (read UIDs, solution, feedback)
// 0 = off (saves memory, for the finished game)
#define DEBUG 1

// ------------------------------ Mastermind -------------------------------

// true = show the secret sequence in row 16 during the game (for testing)
const bool MM_DEBUG_SHOW_SOLUTION = false;

// Maximum number of attempts, after which the game is lost
const byte MM_MAX_ATTEMPTS = 10;
static_assert(MM_MAX_ATTEMPTS >= 1 && MM_MAX_ATTEMPTS <= 15,
              "MM_MAX_ATTEMPTS must be between 1 and 15 (row 16 is reserved for the debug solution)");

// -------------------------- Color Memory Game ----------------------------

// Maximum length of the memory sequence (grid: 5 rows x 4 positions).
// Once reached, every further round has this length (endless).
const byte CM_MAX_LENGTH = 20;

// Times in milliseconds
const unsigned int CM_SHOW_PAUSE          = 300;   // dark pause before the sequence appears
const unsigned int CM_SHOW_TIME_BASE      = 2000;  // display time of the sequence ...
const unsigned int CM_SHOW_TIME_PER_COLOR = 500;   // ... plus this much per color

// ------------------------------- Shared ----------------------------------

// Matrix brightness (0..255)
const byte BRIGHTNESS = 4;

// Times in milliseconds
const unsigned int BLINK_INTERVAL     = 250;   // blinking of a complete input
const unsigned int SCROLL_INTERVAL    = 70;    // speed of the scrolling texts (smaller = faster)
const unsigned int END_IMAGE_DURATION = 4000;  // how long the end image stays
const unsigned int RESULT_PAUSE       = 1500;  // show the last feedback before the next screen
const byte         DEBOUNCE_TIME      = 30;    // button debounce
const unsigned int ABORT_HOLD_TIME    = 3000;  // hold button 2 / 3 this long to quit a running game

// --------------------------------- Pins ----------------------------------

// Hardware SPI of the Mega (fixed, set up by SPI.begin(), listed here
// only for reference): MISO = 50, MOSI = 51, SCK = 52, SS = 53.

const byte MATRIX_PIN = 5;

// Buttons. All pins are interrupt-capable on the Mega (D2, D3, D18-D21).
const byte BUTTON_COUNT        = 3;
const byte BUTTON_CONFIRM      = 0;   // button 1: start / confirm
const byte BUTTON_MASTERMIND   = 1;   // button 2: select Mastermind
const byte BUTTON_COLOR_MEMORY = 2;   // button 3: select Color Memory Game

// Pin 18 = TX1, free as long as Serial1 is not used
const byte BUTTON_PINS[BUTTON_COUNT] = { 2, 3, 18 };

const byte READER_COUNT = 4;   // = length of a Mastermind sequence / of an input block

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

// Gain and transmitter power are set INDIVIDUALLY for every reader, because
// the modules differ. The values were found with rfid_sweep_test.ino
// (block "RECOMMENDED SETTINGS"); repeat the test after changing the
// hardware (module swapped, readers moved, other chips).

// Receiver gain per reader (step 0..5). Lower = shorter range.
// The RC522 only knows these 6 values, there are no steps in between:
//   step:   0      1      2      3      4      5
//   gain:  18 dB  23 dB  33 dB  38 dB  43 dB  48 dB
//                        ^ library default after PCD_Init()
//                                     reader:  1  2  3  4
constexpr byte RFID_GAIN_STEP[READER_COUNT] = { 2, 2, 1, 1 };

// Transmitter power per reader (step 1..8), i.e. the strength of the field
// itself. Step 8 = library default (full power).
// The library has no function for this, so the driver conductance registers
// are written directly: CWGsP = step * 4, CWGsN = step
// (step 8 -> 0x20 / 0x8 = reset values, step 4 -> 0x10 / 0x4, ...).
// The field does not drop linearly: neighboring steps may hardly differ,
// below a certain step detection stops abruptly.
//                                     reader:  1  2  3  4
constexpr byte TX_POWER_STEP[READER_COUNT]  = { 2, 1, 1, 2 };

// Gain used by ALL readers during the power test of the chip lock. The test
// then searches the transmitter power steps 1..8 on every reader, so the
// results of different readers stay comparable although their normal
// settings differ. Step 2 = 33 dB = library default.
const byte PROBE_GAIN_STEP = 2;

// All values can also be changed at runtime via the Serial Monitor
// (DEBUG 1): "g<reader><step>" = gain, "t<reader><step>" = transmitter
// power, reader 0 = all readers, "?" = show current settings.
// Enter the values found afterwards here.

const byte GAIN_STEP_COUNT = 6;
const byte TX_STEP_MAX     = 8;

// Checks every entry of RFID_GAIN_STEP / TX_POWER_STEP at compile time
constexpr bool rfidStepsValid(byte i) {
  return i >= READER_COUNT ||
         (RFID_GAIN_STEP[i] < GAIN_STEP_COUNT &&
          TX_POWER_STEP[i] >= 1 && TX_POWER_STEP[i] <= TX_STEP_MAX &&
          rfidStepsValid(i + 1));
}
static_assert(rfidStepsValid(0),
              "RFID_GAIN_STEP must be 0..5 and TX_POWER_STEP 1..8 for every reader");
static_assert(PROBE_GAIN_STEP < GAIN_STEP_COUNT, "PROBE_GAIN_STEP must be 0..5");

// Read errors (see readReader()): if a reader gets an answer it cannot
// decode - typically its own chip plus the weak answer of a chip on the
// neighbor reader, which garbles it - the reader immediately reads again
// with less gain, one step at a time down to this gain step. The weak
// neighbor chip drops below the receiver threshold first, the own chip
// directly on the antenna is still read. 0 = down to 18 dB.
const byte FALLBACK_MIN_GAIN_STEP = 0;
static_assert(FALLBACK_MIN_GAIN_STEP < GAIN_STEP_COUNT, "FALLBACK_MIN_GAIN_STEP must be 0..5");

// Chip lock against cross-reading (see documentation, "Chip lock").
// true  = every chip is assigned to exactly one reader, even if several
//         readers see it. Costs a little time per polling round.
// false = every reader simply shows the first chip it sees (old behavior).
const bool CHIP_LOCK = true;

// Max. number of chips read per reader and round
// (its own chip + chips of neighbors that reach into its field)
const byte MAX_CHIPS_PER_READER = 3;

// How long a decision of the power test stays valid (ms).
// Before a button confirmation the test is always repeated.
const unsigned int PROBE_CACHE_TIME = 3000;

// ------------------------------ Layout -----------------------------------

// --- Mastermind ---

// Columns (position from 1) of the input - readers 1..4
const byte MM_READER_LED_POSITION[READER_COUNT] = { 3, 4, 5, 6 };

// Columns (position from 1) of the feedback
const byte MM_FEEDBACK_POSITION[READER_COUNT] = { 10, 11, 12, 13 };

// Row for the debug display of the solution
const byte MM_SOLUTION_DEBUG_ROW = 16;

// --- Color Memory Game ---
// Grid of CM_ROW_COUNT rows x 4 positions. Sequence position n (from 0)
// lies in grid row n / 4 at grid position n % 4.

const byte CM_ROW_COUNT = 5;

// Columns (position from 1) of the grid - readers 1..4
const byte CM_GRID_POSITION[READER_COUNT] = { 2, 6, 11, 15 };

// Rows (from 1) of the colors and of the feedback directly below them
const byte CM_INPUT_ROW[CM_ROW_COUNT]    = { 2, 5, 8, 11, 14 };
const byte CM_FEEDBACK_ROW[CM_ROW_COUNT] = { 3, 6, 9, 12, 15 };

static_assert(CM_MAX_LENGTH >= 1 && CM_MAX_LENGTH <= CM_ROW_COUNT * READER_COUNT,
              "CM_MAX_LENGTH must be between 1 and 20 (grid of 5 rows x 4 positions)");

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
  {  60, 255,  120},  // 2 Green
  {  0,   0, 255},  // 3 Blue
  {255, 220,   0}   // 4 Yellow
};

const byte OFF[3] = { 0, 0, 0 };

// ------------------------------ Texts ------------------------------------
// Texts on the matrix. Stored in flash (PROGMEM) instead of RAM.

const char TEXT_MM_START[]     PROGMEM = "MASTERMIND - PRESS BUTTON";
const char TEXT_MM_WIN[]       PROGMEM = "YOU WIN!";
const char TEXT_MM_LOSE[]      PROGMEM = "YOU LOSE";
const char TEXT_CM_START[]     PROGMEM = "COLOR MEMORY GAME - PRESS BUTTON";
const char TEXT_CM_GAME_OVER[] PROGMEM = "GAME OVER";

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

// States - Mastermind
const byte S_MM_START     = 0;   // start screen
const byte S_MM_GAME      = 1;   // running game
const byte S_MM_WON       = 2;   // win screen
const byte S_MM_LOST      = 3;   // lose screen
// States - Color Memory Game
const byte S_CM_START     = 4;   // start screen
const byte S_CM_SHOW      = 5;   // sequence is being shown
const byte S_CM_INPUT     = 6;   // player enters the sequence
const byte S_CM_GAME_OVER = 7;   // game over screen

byte state = S_MM_START;

// Mastermind
byte mmSecretCode[READER_COUNT];   // color indices 1..4
byte mmLastAttempt[READER_COUNT];  // last confirmed input (0 = none yet)
byte mmAttemptCount = 0;           // number of attempts already confirmed

// Color Memory Game
byte cmSequence[CM_MAX_LENGTH];    // color indices 1..4
byte cmInput[CM_MAX_LENGTH];       // confirmed input of the player
byte cmLength = 0;                 // length of the current sequence
byte cmInputPosition = 0;          // number of colors already confirmed
unsigned int cmScore = 0;          // correct positions over all rounds
bool cmWaitForEmpty = false;       // chips of the last block still have to be removed
bool cmSequenceVisible = false;    // S_CM_SHOW: false = dark pause, true = sequence visible
unsigned long cmShowSince = 0;     // start of the current S_CM_SHOW phase

// Blinking
bool blinkOn = true;
unsigned long lastBlinkToggle = 0;

// Scrolling text / end screen
int16_t scrollX = 16;
unsigned long lastScrollStep = 0;
bool endImageActive = false;
unsigned long endImageSince = 0;

// Reader
byte gainStep[READER_COUNT];   // current settings per reader (changeable via serial),
byte txStep[READER_COUNT];     // loaded from RFID_GAIN_STEP / TX_POWER_STEP in setup()
byte readerColor[READER_COUNT];             // color index per reader
byte currentUid[READER_COUNT][UID_LENGTH];  // UID of the chip currently present
bool chipPresent[READER_COUNT];             // is a chip currently present?
byte failedReads[READER_COUNT];             // counter for dropouts

// Chip lock
const byte NO_READER    = 0xFF;
const byte NO_THRESHOLD = 0xFF;
const byte MAX_UIDS     = READER_COUNT * MAX_CHIPS_PER_READER;

// All chips each reader saw in the current polling round
byte seenCount[READER_COUNT];
byte seenUid[READER_COUNT][MAX_CHIPS_PER_READER][UID_LENGTH];

// Remembered results of the power test (one entry per contested chip)
struct ProbeResult {
  byte uid[UID_LENGTH];
  byte mask;              // readers that competed (bit 0 = reader 1)
  byte winner;            // reader index or NO_READER (tie)
  unsigned long time;     // millis() of the test
  bool valid;
};
ProbeResult probeCache[READER_COUNT];

// Buttons (written inside the interrupts -> volatile)
volatile bool buttonPressed[BUTTON_COUNT];
volatile unsigned long lastButtonEdge[BUTTON_COUNT];
volatile unsigned long buttonTimeIsr[BUTTON_COUNT];   // micros() at press
volatile unsigned long buttonPressMillis[BUTTON_COUNT]; // millis() at press (for holding)
volatile bool buttonHoldUsed[BUTTON_COUNT];           // this press may no longer count as "held"
unsigned long buttonTime = 0;                         // copy for the main program

// ============================== Buttons ==================================

// Called on every level change at a button pin.
// A press only counts if the pin is LOW and the last edge is longer ago
// than DEBOUNCE_TIME. The bouncing on press AND on release produces edges
// in quick succession and is thereby ignored.
void handleButtonEdge(byte b) {
  unsigned long now = millis();
  if (digitalRead(BUTTON_PINS[b]) == LOW && (now - lastButtonEdge[b]) >= DEBOUNCE_TIME) {
    buttonPressed[b]     = true;
    buttonTimeIsr[b]     = micros();
    buttonPressMillis[b] = now;     // a new press starts a new hold
    buttonHoldUsed[b]    = false;
  }
  lastButtonEdge[b] = now;
}

// One interrupt routine per button (an ISR cannot take parameters)
void buttonConfirmISR()     { handleButtonEdge(BUTTON_CONFIRM); }
void buttonMastermindISR()  { handleButtonEdge(BUTTON_MASTERMIND); }
void buttonColorMemoryISR() { handleButtonEdge(BUTTON_COLOR_MEMORY); }

// Returns true if button b was pressed since the last call, and resets
// the flag. The moment of the press is copied to buttonTime.
// Can also be used to "discard" old presses.
bool fetchButton(byte b) {
  noInterrupts();
  bool pressed = buttonPressed[b];
  buttonPressed[b] = false;
  if (pressed) buttonTime = buttonTimeIsr[b];
  interrupts();
  return pressed;
}

// Discards pending presses of all buttons
void discardButtons() {
  for (byte b = 0; b < BUTTON_COUNT; b++) fetchButton(b);
}

// Is button b still held down, for at least ABORT_HOLD_TIME since its last
// press? The press time comes from the interrupt; whether the button is
// still down is read directly from the pin. A release and a new press
// start a new hold, because the interrupt then stores a new press time.
bool buttonHeld(byte b) {
  noInterrupts();
  unsigned long pressedAt = buttonPressMillis[b];
  bool used = buttonHoldUsed[b];
  interrupts();

  return !used &&
         digitalRead(BUTTON_PINS[b]) == LOW &&
         millis() - pressedAt >= ABORT_HOLD_TIME;
}

// Buttons that are held down at this moment no longer count as "held"
// until they are released and pressed again. Called when a game starts
// (a button held since the start screen must not quit the new game) and
// after quitting (the button is usually still down).
void cancelHolds() {
  noInterrupts();
  for (byte b = 0; b < BUTTON_COUNT; b++) buttonHoldUsed[b] = true;
  interrupts();
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

// Draws a number (0..9999) horizontally centered. y = top row (from 0)
// of a 7-pixel-high area.
//   1-2 digits: default font, bold (2 pixels wide)
//   3-4 digits: small 3x5 font (TomThumb), vertically centered in the area
// Larger values are shown as 9999.
void drawNumber(unsigned int value, int16_t y, const byte color[3]) {
  if (value > 9999) value = 9999;

  char numStr[6];
  utoa(value, numStr, 10);
  byte length = strlen(numStr);
  uint16_t c = colorValue(color);

  if (length <= 2) {
    const byte BOLD_WIDTH = 6;   // 5-pixel character + 1-pixel thickening
    const byte SPACING    = 2;   // gap between two digits
    byte total = length * BOLD_WIDTH + (length - 1) * SPACING;
    int16_t x = (16 - total) / 2;
    for (byte i = 0; i < length; i++) {
      int16_t xc = x + i * (BOLD_WIDTH + SPACING);
      matrix.drawChar(xc,     y, numStr[i], c, c, 1);
      matrix.drawChar(xc + 1, y, numStr[i], c, c, 1);   // offset by 1 pixel = bold
    }

  } else {
    const byte SMALL_WIDTH = 4;   // 3-pixel digit + 1-pixel spacing
    byte total = length * SMALL_WIDTH - 1;
    int16_t x = (16 - total) / 2;
    matrix.setFont(&TomThumb);
    for (byte i = 0; i < length; i++) {
      // Custom fonts are drawn at the baseline: digit rows y+1 .. y+5
      matrix.drawChar(x + i * SMALL_WIDTH, y + 6, numStr[i], c, c, 1);
    }
    matrix.setFont(NULL);         // back to the default font
  }
}

// ============================== RFID =====================================

// Polls one reader and reads ALL chips in its field (up to
// MAX_CHIPS_PER_READER), so that its own chip is found even if a
// neighbor's chip also reaches into the field. Writes the UIDs into
// uids[] and returns how many were found.
// Procedure: WUPA wakes all chips, one of them is selected (anticollision),
// read and put to sleep (HALT). REQA then only wakes chips that are not yet
// asleep - so the next chip answers, until none is left.
// Only the antenna of the reader being polled is on at any time.
// error = true: a chip answered, but the answer could not be decoded
// (garbled request answer, failed anticollision / select). The read stops
// there. No answer at all (timeout) is not an error.
byte readAllChips(MFRC522 &r, byte uids[][UID_LENGTH], bool &error) {
  byte count = 0;
  byte maxChips = CHIP_LOCK ? MAX_CHIPS_PER_READER : 1;
  error = false;

  r.PCD_AntennaOn();
  delay(ANTENNA_WAIT);

  for (byte n = 0; n < maxChips; n++) {
    byte atqa[2];
    byte atqaSize = sizeof(atqa);

    MFRC522::StatusCode status = (n == 0)
      ? r.PICC_WakeupA(atqa, &atqaSize)     // first chip: wake everything
      : r.PICC_RequestA(atqa, &atqaSize);   // further chips: only those not asleep

    if (status != MFRC522::STATUS_OK && status != MFRC522::STATUS_COLLISION) {
      if (status != MFRC522::STATUS_TIMEOUT) error = true;   // garbled, not "nobody there"
      break;
    }
    if (!r.PICC_ReadCardSerial()) {
      error = true;   // a chip answered, but selecting / reading its UID failed
      break;
    }

    bool duplicate = false;
    for (byte k = 0; k < count; k++) {
      if (memcmp(uids[k], r.uid.uidByte, UID_LENGTH) == 0) duplicate = true;
    }
    if (!duplicate) {
      memcpy(uids[count], r.uid.uidByte, UID_LENGTH);
      count++;
    }

    r.PICC_HaltA();
    if (duplicate) break;   // chip did not go to sleep -> stop, no endless loop
  }

  r.PCD_AntennaOff();
  return count;
}

// Sets the transmitter power (step 1..8) of one reader.
// Only the "no modulation" values are changed; ModGsP has no effect anyway
// (the library forces 100 % ASK), and the lower 4 bits of GsNReg (ModGsN)
// are kept as they are.
void setTxPower(byte i, byte step) {
  reader[i].PCD_WriteRegister(MFRC522::CWGsPReg, step * 4);
  byte gsn = reader[i].PCD_ReadRegister(MFRC522::GsNReg);
  reader[i].PCD_WriteRegister(MFRC522::GsNReg, (step << 4) | (gsn & 0x0F));
}

// Register values for the gain steps 0..5
const byte GAIN_VALUES[GAIN_STEP_COUNT] = {
  MFRC522::RxGain_18dB, MFRC522::RxGain_23dB, MFRC522::RxGain_33dB,
  MFRC522::RxGain_38dB, MFRC522::RxGain_43dB, MFRC522::RxGain_48dB
};

// Writes the gain and transmitter power of reader i (gainStep[i] / txStep[i]).
void applyReaderSettings(byte i) {
  reader[i].PCD_SetAntennaGain(GAIN_VALUES[gainStep[i]]);
  setTxPower(i, txStep[i]);
}

// Writes the settings of all readers.
// Must be called after PCD_Init(), because PCD_Init() resets both.
void applyRfidSettings() {
  for (byte i = 0; i < READER_COUNT; i++) {
    applyReaderSettings(i);
    probeCache[i].valid = false;   // old power-test results no longer apply
  }
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

// Prints a UID in hex, e.g. "04 34 13 BB"
void printUid(const byte* uid) {
  for (byte i = 0; i < UID_LENGTH; i++) {
    if (uid[i] < 0x10) Serial.print('0');
    Serial.print(uid[i], HEX);
    if (i < UID_LENGTH - 1) Serial.print(' ');
  }
}

// Prints the readers of a bit mask, e.g. "1+2"
void printReaders(byte mask) {
  bool first = true;
  for (byte i = 0; i < READER_COUNT; i++) {
    if (mask & (1 << i)) {
      if (!first) Serial.print('+');
      Serial.print(i + 1);
      first = false;
    }
  }
}

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
    printUid(uid);
    Serial.print(']');
  } else {
    Serial.print(F("   [no chip]"));
  }
  Serial.println();
}

// Prints a color sequence to serial, e.g. "Red Yellow Yellow Blue "
void reportSequence(const byte* sequence, byte length) {
  for (byte i = 0; i < length; i++) {
    Serial.print(COLOR_NAMES[sequence[i]]);
    Serial.print(' ');
  }
}

const byte GAIN_DB[GAIN_STEP_COUNT] = { 18, 23, 33, 38, 43, 48 };

// Prints a settings array in the form of the constants, e.g. "{ 4, 1, 4, 1 };"
void printStepArray(const byte* steps) {
  Serial.print(F("{ "));
  for (byte i = 0; i < READER_COUNT; i++) {
    Serial.print(steps[i]);
    Serial.print(i < READER_COUNT - 1 ? F(", ") : F(" };"));
  }
  Serial.println();
}

// Prints the current RFID settings of every reader and the registers read
// back from it, then the values in the form of the constants.
void reportRfidSettings() {
  Serial.println(F("RFID settings:"));

  // Expected: gain 0x00/0x10/0x40/0x50/0x60/0x70, CWGsP = step*4, GsN = step in the upper digit
  for (byte i = 0; i < READER_COUNT; i++) {
    Serial.print(F("  Reader "));
    Serial.print(i + 1);
    Serial.print(F(": gain step "));
    Serial.print(gainStep[i]);
    Serial.print(F(" ("));
    Serial.print(GAIN_DB[gainStep[i]]);
    Serial.print(F(" dB), TX step "));
    Serial.print(txStep[i]);
    Serial.print(F("  | gain 0x"));
    Serial.print(reader[i].PCD_GetAntennaGain(), HEX);
    Serial.print(F(" | CWGsP 0x"));
    Serial.print(reader[i].PCD_ReadRegister(MFRC522::CWGsPReg), HEX);
    Serial.print(F(" | GsN 0x"));
    Serial.println(reader[i].PCD_ReadRegister(MFRC522::GsNReg), HEX);
  }
  Serial.print(F("  Power test: gain step "));
  Serial.print(PROBE_GAIN_STEP);
  Serial.print(F(" ("));
  Serial.print(GAIN_DB[PROBE_GAIN_STEP]);
  Serial.println(F(" dB) on all readers, TX steps 1..8"));

  Serial.print(F("  constexpr byte RFID_GAIN_STEP[READER_COUNT] = "));
  printStepArray(gainStep);
  Serial.print(F("  constexpr byte TX_POWER_STEP[READER_COUNT]  = "));
  printStepArray(txStep);
}

// Change gain / transmitter power at runtime via the Serial Monitor.
// Command letter, reader (0 = all readers, 1..4), step:
//   g<reader><step>  gain step 0..5,               e.g. g24 = reader 2 -> 43 dB
//   t<reader><step>  transmitter power step 1..8,  e.g. t38 = reader 3 -> step 8
//   ?                show settings
void serialTuning() {
  static char command = 0;
  static byte target  = NO_READER;   // first digit after the letter

  while (Serial.available()) {
    char c = Serial.read();

    if (c == 'g' || c == 'G' || c == 't' || c == 'T') {
      command = tolower(c);
      target  = NO_READER;
    } else if (c == '?') {
      reportRfidSettings();
      command = 0;
    } else if (c >= '0' && c <= '9' && command != 0) {
      byte value = c - '0';
      bool valid;

      if (target == NO_READER) {
        // First digit: the reader
        valid = (value <= READER_COUNT);
        if (valid) target = value;
      } else {
        // Second digit: the step
        valid = (command == 'g') ? (value < GAIN_STEP_COUNT)
                                 : (value >= 1 && value <= TX_STEP_MAX);
        if (valid) {
          for (byte i = 0; i < READER_COUNT; i++) {
            if (target != 0 && target != i + 1) continue;
            if (command == 'g') gainStep[i] = value;
            else                txStep[i]   = value;
          }
          applyRfidSettings();
          reportRfidSettings();
        }
        command = 0;
      }

      if (!valid) {
        Serial.println(F("Invalid input (g<reader 0..4><step 0..5>, t<reader 0..4><step 1..8>, ?)"));
        command = 0;
      }
    }
    // everything else (line endings etc.) is ignored
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

// ============================ Chip lock ==================================
//
// Every polling round runs in three steps (pollReaders):
//   1. Read:    every ACTIVE reader reads ALL chips in its field.
//   2. Assign:  every chip is assigned to at most one reader (resolveOwners).
//   3. Update:  the display state of every reader is updated (updateReader).
//
// Only the active readers take part (activeReaderCount(): Mastermind all 4,
// Color Memory Game only the readers of the current block). An inactive
// reader is not read and forgets its chip, so it is never a candidate.
// Example (block of 2): a chip seen by readers 2 and 3 can only belong to
// reader 2, because reader 3 is not read at all.
//
// Assignment rules, applied repeatedly until everything is decided:
//   a) Elimination: if only one free reader is left for a chip, the chip
//      belongs to it. The reader is then taken. Example: reader 1 sees A+B,
//      reader 2 sees B -> A only fits reader 1, so B belongs to reader 2.
//   b) A chip for which all its readers are taken belongs to nobody
//      (it was only cross-read).
//   c) If a chip is still seen by several free readers, the power test
//      decides: the transmitter power is lowered step by step; the reader
//      that still sees the chip at the lowest step is closest to it.
//   d) If several chips can only belong to the same reader, it keeps the
//      chip it already shows; otherwise the power test decides here too.
//   Tie -> the chip belongs to nobody. In case of doubt the reader therefore
//   shows white: the row does not blink and cannot be confirmed.
// The reader that currently shows a chip counts as a candidate for it even
// if it missed it in this round (dropout) - so a single dropout does not
// hand the chip over to a neighbor.

void blinkTick();          // defined in "Shared game helpers"
byte activeReaderCount();  // defined in "Shared game helpers"

// Pointer to the UID behind a list reference (reader * MAX + index)
const byte* seenUidAt(byte ref) {
  return seenUid[ref / MAX_CHIPS_PER_READER][ref % MAX_CHIPS_PER_READER];
}

// Did reader i see this UID in the current round?
bool sawUid(byte i, const byte* uid) {
  for (byte k = 0; k < seenCount[i]; k++) {
    if (memcmp(seenUid[i][k], uid, UID_LENGTH) == 0) return true;
  }
  return false;
}

// ---------------------- Read errors: less gain --------------------------

const byte FALLBACK_NONE   = 0xFF;   // read without error at the normal gain
const byte FALLBACK_FAILED = 0xFE;   // error even at the lowest gain step

#if DEBUG
byte fallbackState[READER_COUNT];    // last state per reader (set in setup)
#endif

// Prints a line when the read-error state of reader i changes, so the
// Serial Monitor is not flooded every round.
void noteFallback(byte i, byte state) {
#if DEBUG
  if (fallbackState[i] == state) return;
  fallbackState[i] = state;
  Serial.print(F("Reader "));
  Serial.print(i + 1);
  if (state == FALLBACK_NONE) {
    Serial.println(F(": reads normally again"));
  } else if (state == FALLBACK_FAILED) {
    Serial.println(F(": read error, also with less gain"));
  } else {
    Serial.print(F(": read error at "));
    Serial.print(GAIN_DB[gainStep[i]]);
    Serial.print(F(" dB -> clean read at "));
    Serial.print(GAIN_DB[state]);
    Serial.println(F(" dB"));
  }
#else
  (void)i;
  (void)state;
#endif
}

// Reads reader i like readAllChips(), but repairs read errors.
// Typical cause: the own chip lies on the reader and a chip on the neighbor
// reader answers weakly at the same time. With high gain the reader hears
// both, the answer is garbled and the reader would report NO chip - the own
// chip would disappear as long as the neighbor chip lies there.
// Then the reader reads again with less gain, one step at a time down to
// FALLBACK_MIN_GAIN_STEP, until the read is clean. The weak neighbor chip
// drops below the receiver threshold first, the own chip is still read.
// The normal gain is restored afterwards. Result: the clean read, otherwise
// the read with the most chips.
byte readReader(byte i, byte uids[][UID_LENGTH]) {
  bool error;
  byte count = readAllChips(reader[i], uids, error);
  if (!error) {
    noteFallback(i, FALLBACK_NONE);
    return count;
  }

  byte retry[MAX_CHIPS_PER_READER][UID_LENGTH];
  byte state = FALLBACK_FAILED;

  for (int8_t g = (int8_t)gainStep[i] - 1; g >= (int8_t)FALLBACK_MIN_GAIN_STEP; g--) {
    reader[i].PCD_SetAntennaGain(GAIN_VALUES[g]);
    byte retryCount = readAllChips(reader[i], retry, error);
    if (retryCount > count || (!error && retryCount == count)) {
      memcpy(uids, retry, retryCount * UID_LENGTH);
      count = retryCount;
    }
    if (!error) {
      state = g;
      break;
    }
  }

  reader[i].PCD_SetAntennaGain(GAIN_VALUES[gainStep[i]]);   // normal gain again
  noteFallback(i, state);
  return count;
}

// Does reader i see the UID at transmitter power step "step"?
bool seesUidAt(byte i, const byte* uid, byte step) {
  byte uids[MAX_CHIPS_PER_READER][UID_LENGTH];
  bool error;   // not used here: in the power test a read error = not seen
  setTxPower(i, step);
  byte count = readAllChips(reader[i], uids, error);
  for (byte k = 0; k < count; k++) {
    if (memcmp(uids[k], uid, UID_LENGTH) == 0) return true;
  }
  return false;
}

// Power test: lowest transmitter power step at which reader i still sees
// the chip (binary search, 4 reads). NO_THRESHOLD = not seen at all.
// Every reader is measured with the same gain (PROBE_GAIN_STEP) over the
// same range 1..TX_STEP_MAX, so the steps of different readers can be
// compared although their normal settings differ.
byte probeThreshold(byte i, const byte* uid) {
  byte result = NO_THRESHOLD;

  reader[i].PCD_SetAntennaGain(GAIN_VALUES[PROBE_GAIN_STEP]);
  if (seesUidAt(i, uid, TX_STEP_MAX)) {
    byte low = 1, high = TX_STEP_MAX;
    while (low < high) {
      byte middle = (low + high) / 2;
      blinkTick();
      if (seesUidAt(i, uid, middle)) high = middle;
      else                           low  = middle + 1;
    }
    result = low;
  }

  applyReaderSettings(i);   // back to the normal settings of this reader
  return result;
}

// Chip seen by several readers (mask): the reader with the lowest power
// step wins. Tie -> the tied reader that already shows the chip keeps it,
// otherwise NO_READER. Not seen by anyone -> NO_READER.
// Results are remembered for PROBE_CACHE_TIME, unless fresh == true.
byte decideContested(const byte* uid, byte mask, bool fresh) {
  // Remembered result?
  if (!fresh) {
    for (byte c = 0; c < READER_COUNT; c++) {
      ProbeResult &p = probeCache[c];
      if (p.valid && p.mask == mask &&
          millis() - p.time < PROBE_CACHE_TIME &&
          memcmp(p.uid, uid, UID_LENGTH) == 0) {
        return p.winner;
      }
    }
  }

#if DEBUG
  Serial.print(F("Chip "));
  printUid(uid);
  Serial.print(F(" seen by readers "));
  printReaders(mask);
  Serial.print(F(" -> power test:"));
#endif

  byte best = NO_THRESHOLD;
  byte winner = NO_READER;
  bool tie = false;
  byte threshold[READER_COUNT];

  for (byte i = 0; i < READER_COUNT; i++) {
    threshold[i] = NO_THRESHOLD;
    if (!(mask & (1 << i))) continue;
    byte t = probeThreshold(i, uid);
    threshold[i] = t;
#if DEBUG
    Serial.print(F(" R"));
    Serial.print(i + 1);
    Serial.print('=');
    if (t == NO_THRESHOLD) Serial.print('-');
    else                   Serial.print(t);
#endif
    if (t < best) {
      best = t;
      winner = i;
      tie = false;
    } else if (t == best && t != NO_THRESHOLD) {
      tie = true;
    }
  }
  // Tie: the reader that already shows this chip keeps it (at most one
  // reader can show it), so a tie does not make the chip vanish.
  // If none of the tied readers shows it: nobody gets it.
  bool kept = false;
  if (tie) {
    winner = NO_READER;
    for (byte i = 0; i < READER_COUNT; i++) {
      if (threshold[i] == best && chipPresent[i] &&
          memcmp(currentUid[i], uid, UID_LENGTH) == 0) {
        winner = i;
        kept = true;
      }
    }
  }
  (void)kept;   // only used for the debug output

#if DEBUG
  if (winner == NO_READER) {
    Serial.println(F(" -> undecided, nobody"));
  } else {
    Serial.print(kept ? F(" -> tie, reader ") : F(" -> reader "));
    Serial.print(winner + 1);
    Serial.println(kept ? F(" keeps it") : F(""));
  }
#endif

  // Remember: slot with the same UID, otherwise a free or the oldest one
  byte slot = 0;
  for (byte c = 0; c < READER_COUNT; c++) {
    if (probeCache[c].valid && memcmp(probeCache[c].uid, uid, UID_LENGTH) == 0) {
      slot = c;
      break;
    }
    if (!probeCache[c].valid ||
        (probeCache[slot].valid && probeCache[c].time < probeCache[slot].time)) {
      slot = c;
    }
  }
  memcpy(probeCache[slot].uid, uid, UID_LENGTH);
  probeCache[slot].mask   = mask;
  probeCache[slot].winner = winner;
  probeCache[slot].time   = millis();
  probeCache[slot].valid  = true;

  return winner;
}

// Several chips can only belong to reader r (forced = list of list
// indices). Returns the list index of the winner or NO_READER.
byte decideOnReader(byte r, const byte* listRef, const byte* forced,
                    byte forcedCount, bool fresh) {
  // It keeps the chip it already shows (not for the fresh check)
  if (!fresh && chipPresent[r]) {
    for (byte f = 0; f < forcedCount; f++) {
      if (memcmp(seenUidAt(listRef[forced[f]]), currentUid[r], UID_LENGTH) == 0) {
        return forced[f];
      }
    }
  }

#if DEBUG
  Serial.print(F("Reader "));
  Serial.print(r + 1);
  Serial.print(F(" sees "));
  Serial.print(forcedCount);
  Serial.print(F(" chips -> power test:"));
#endif

  byte best = NO_THRESHOLD;
  byte winner = NO_READER;
  bool tie = false;

  byte threshold[MAX_UIDS];

  for (byte f = 0; f < forcedCount; f++) {
    byte t = probeThreshold(r, seenUidAt(listRef[forced[f]]));
    threshold[f] = t;
#if DEBUG
    Serial.print(' ');
    if (t == NO_THRESHOLD) Serial.print('-');
    else                   Serial.print(t);
#endif
    if (t < best) {
      best = t;
      winner = forced[f];
      tie = false;
    } else if (t == best && t != NO_THRESHOLD) {
      tie = true;
    }
  }

  // Tie: if the chip the reader already shows is among the tied ones, it
  // keeps it; otherwise white
  if (tie) {
    winner = NO_READER;
    for (byte f = 0; f < forcedCount; f++) {
      if (threshold[f] == best && chipPresent[r] &&
          memcmp(seenUidAt(listRef[forced[f]]), currentUid[r], UID_LENGTH) == 0) {
        winner = forced[f];
      }
    }
  }

#if DEBUG
  if (winner == NO_READER) {
    Serial.println(F(" -> undecided, white"));
  } else {
    Serial.print(F(" -> keeps "));
    printUid(seenUidAt(listRef[winner]));
    Serial.println();
  }
#endif
  return winner;
}

// Assigns every chip seen in this round to at most one reader.
// owner[i] = UID assigned to reader i, or NULL.
void resolveOwners(const byte* owner[], bool fresh) {
  byte listRef[MAX_UIDS];   // where the UID bytes are (see seenUidAt)
  byte cand[MAX_UIDS];      // candidate readers as bit mask; 0 = decided
  byte listCount = 0;

  // Collect all different UIDs and which readers saw them
  for (byte i = 0; i < READER_COUNT; i++) {
    for (byte k = 0; k < seenCount[i]; k++) {
      byte j = 0;
      while (j < listCount &&
             memcmp(seenUidAt(listRef[j]), seenUid[i][k], UID_LENGTH) != 0) j++;
      if (j == listCount) {
        listRef[listCount] = i * MAX_CHIPS_PER_READER + k;
        cand[listCount] = 0;
        listCount++;
      }
      cand[j] |= (1 << i);
    }
  }

  // The reader currently showing a chip remains a candidate for it
  for (byte i = 0; i < READER_COUNT; i++) {
    if (!chipPresent[i]) continue;
    for (byte j = 0; j < listCount; j++) {
      if (memcmp(seenUidAt(listRef[j]), currentUid[i], UID_LENGTH) == 0) {
        cand[j] |= (1 << i);
      }
    }
  }

  for (byte i = 0; i < READER_COUNT; i++) owner[i] = NULL;
  byte taken = 0;   // readers that are decided

  while (true) {
    bool progress = false;

    // b) all candidates taken -> the chip belongs to nobody
    for (byte j = 0; j < listCount; j++) {
      if (cand[j] != 0 && (cand[j] & ~taken) == 0) {
        cand[j] = 0;
        progress = true;
      }
    }

    // a) + d) readers that are the last free candidate for some chips
    for (byte r = 0; r < READER_COUNT; r++) {
      if (taken & (1 << r)) continue;

      byte forced[MAX_UIDS];
      byte forcedCount = 0;
      for (byte j = 0; j < listCount; j++) {
        if (cand[j] != 0 && (cand[j] & ~taken) == (1 << r)) {
          forced[forcedCount++] = j;
        }
      }
      if (forcedCount == 0) continue;

      byte winner = (forcedCount == 1)
        ? forced[0]
        : decideOnReader(r, listRef, forced, forcedCount, fresh);

      taken |= (1 << r);
      if (winner != NO_READER) {
        owner[r] = seenUidAt(listRef[winner]);
        cand[winner] = 0;
      }
      progress = true;
    }

    if (progress) continue;

    // c) a chip with several free candidates -> power test
    byte j = 0;
    while (j < listCount && cand[j] == 0) j++;
    if (j == listCount) break;   // everything decided

    byte mask = cand[j] & ~taken;
    byte winner = decideContested(seenUidAt(listRef[j]), mask, fresh);
    cand[j] = 0;
    if (winner != NO_READER) {
      owner[winner] = seenUidAt(listRef[j]);
      taken |= (1 << winner);
    }
  }
}

// Removes the chip of reader i from the display at once
bool removeChip(byte i) {
  chipPresent[i] = false;
  failedReads[i] = 0;
  bool changed   = (readerColor[i] != COLOR_WHITE);
  readerColor[i] = COLOR_WHITE;
#if DEBUG
  report(i, NULL);
#endif
  return changed;
}

// Updates the state of reader i. uid = chip assigned to it and actually
// seen in this round, NULL = none.
// Returns true if the displayed color has changed.
bool updateReader(byte i, const byte* uid) {
  if (uid != NULL) {
    // --- A chip is present ---
    failedReads[i] = 0;

    bool isNew      = !chipPresent[i];
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
    if (failedReads[i] >= MAX_FAILED_READS) return removeChip(i);
  }
  return false;
}

// One complete polling round over all active readers.
// fresh = true: ignore remembered power-test results (before confirming).
// Returns true if a displayed color has changed.
bool pollReaders(bool fresh) {
  byte active = activeReaderCount();

  // 1. Read
  for (byte i = 0; i < READER_COUNT; i++) {
    if (i >= active) {
      // Inactive reader: not read, so it cannot see or own a chip.
      // A chip it still shows from before is forgotten. This is not a
      // "change", because an inactive reader is not displayed.
      seenCount[i] = 0;
      if (chipPresent[i]) removeChip(i);
      continue;
    }
    seenCount[i] = readReader(i, seenUid[i]);
    blinkTick();   // between the readers so the blinking stays even
  }

  // 2. Assign
  const byte* owner[READER_COUNT];
  if (CHIP_LOCK) {
    resolveOwners(owner, fresh);
  } else {
    for (byte i = 0; i < READER_COUNT; i++) {
      owner[i] = (seenCount[i] > 0) ? seenUid[i][0] : NULL;
    }
  }

  bool changed = false;

  // A chip can only lie on one reader: if it now belongs to another
  // reader, remove it here at once (no waiting for dropouts)
  if (CHIP_LOCK) {
    for (byte i = 0; i < READER_COUNT; i++) {
      if (!chipPresent[i]) continue;
      for (byte w = 0; w < READER_COUNT; w++) {
        if (w != i && owner[w] != NULL &&
            memcmp(owner[w], currentUid[i], UID_LENGTH) == 0) {
          if (removeChip(i)) changed = true;
          break;
        }
      }
    }
  }

  // 3. Update. A chip assigned only because the reader already showed it
  //    (not seen this round) counts as a dropout.
  for (byte i = 0; i < READER_COUNT; i++) {
    const byte* uid = owner[i];
    if (uid != NULL && !sawUid(i, uid)) uid = NULL;
    if (updateReader(i, uid)) changed = true;
  }
  return changed;
}

// ========================= Shared game helpers ===========================
// Active readers and blinking of the input row, used by both games. The
// functions pass the work on to the game that is currently running
// (depending on state).

bool mmInputConfirmable();   // defined in "Mastermind"
void mmDrawInputRow();
bool cmInputConfirmable();   // defined in "Color Memory Game"
void cmDrawInputRow();
byte cmBlockSize();

// Number of readers the running game needs at the moment (readers
// 1..count). Only these are read by pollReaders() and can get a chip.
//   Mastermind:        always all 4
//   Color Memory Game: the readers of the current block (fewer for the
//                      last block, e.g. only 1 in round 1)
byte activeReaderCount() {
  if (state == S_CM_INPUT) return cmBlockSize();
  return READER_COUNT;
}

// May the current input be confirmed? (-> row blinks)
bool inputConfirmable() {
  if (state == S_MM_GAME)  return mmInputConfirmable();
  if (state == S_CM_INPUT) return cmInputConfirmable();
  return false;
}

// Draws the current input row of the running game
void drawInputRow() {
  if (state == S_MM_GAME)       mmDrawInputRow();
  else if (state == S_CM_INPUT) cmDrawInputRow();
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

// Start the blinking visible and draw the input row at once
// (after every change, so the color is seen immediately)
void restartBlink() {
  blinkOn = true;
  lastBlinkToggle = millis();
  drawInputRow();
}

void startEndScreen(byte endState);   // defined in "Screens"

// ============================== Mastermind ===============================

// Row (from 1) in which input currently happens
byte mmCurrentRow() {
  return mmAttemptCount + 1;
}

// Is a known chip present on all readers?
bool mmInputComplete() {
  for (byte i = 0; i < READER_COUNT; i++) {
    if (readerColor[i] == COLOR_WHITE) return false;
  }
  return true;
}

// Complete AND different from the last attempt -> may be confirmed
bool mmInputConfirmable() {
  return mmInputComplete() &&
         memcmp(readerColor, mmLastAttempt, READER_COUNT) != 0;
}

// Draws the current input row (takes the blinking into account)
void mmDrawInputRow() {
  bool visible = !mmInputConfirmable() || blinkOn;

  for (byte i = 0; i < READER_COUNT; i++) {
    setLED(mmCurrentRow(), MM_READER_LED_POSITION[i],
           visible ? colors[readerColor[i]] : OFF);
  }
  matrix.show();
}

// Compares an attempt with the secret sequence.
// green = right color + position, yellow = right color, wrong position
void mmEvaluate(const byte* attempt, byte &green, byte &yellow) {
  byte remainingSecret[COLOR_COUNT]  = { 0 };
  byte remainingAttempt[COLOR_COUNT] = { 0 };

  green  = 0;
  yellow = 0;

  for (byte i = 0; i < READER_COUNT; i++) {
    if (attempt[i] == mmSecretCode[i]) {
      green++;
    } else {
      // Only count the positions that were not hit for "yellow"
      remainingSecret[mmSecretCode[i]]++;
      remainingAttempt[attempt[i]]++;
    }
  }

  for (byte f = 1; f < COLOR_COUNT; f++) {
    yellow += min(remainingSecret[f], remainingAttempt[f]);
  }
}

// Show the feedback: first green, then yellow, rest red
// (red = this chip is neither the right color nor at the right position)
void mmDrawFeedback(byte row, byte green, byte yellow) {
  for (byte i = 0; i < READER_COUNT; i++) {
    const byte* f = colors[COLOR_RED];
    if (i < green)               f = colors[COLOR_GREEN];
    else if (i < green + yellow) f = colors[COLOR_YELLOW];
    setLED(row, MM_FEEDBACK_POSITION[i], f);
  }
}

// Button 1 was pressed during the game
void mmConfirmAttempt() {
  if (!mmInputConfirmable()) {
#if DEBUG
    Serial.println(F("Button ignored (input incomplete or same as last time)"));
#endif
    return;
  }

  // Chip lock: one more complete round with a fresh power test. If the
  // result differs from the display, nothing is confirmed - the player
  // sees the corrected row and presses again.
  if (CHIP_LOCK && pollReaders(true)) {
    restartBlink();
    fetchButton(BUTTON_CONFIRM);   // discard presses during the check
#if DEBUG
    Serial.println(F("Button ignored (input changed during the check)"));
#endif
    return;
  }
  if (!mmInputConfirmable()) return;

  byte row = mmCurrentRow();
  byte green, yellow;
  mmEvaluate(readerColor, green, yellow);

  // Leave the input fixed (not blinking) + feedback next to it
  for (byte i = 0; i < READER_COUNT; i++) {
    setLED(row, MM_READER_LED_POSITION[i], colors[readerColor[i]]);
  }
  mmDrawFeedback(row, green, yellow);
  matrix.show();

  memcpy(mmLastAttempt, readerColor, READER_COUNT);
  mmAttemptCount++;

#if DEBUG
  Serial.print(F("Attempt "));
  Serial.print(mmAttemptCount);
  Serial.print(F(": "));
  reportSequence(mmLastAttempt, READER_COUNT);
  Serial.print(F("-> green "));
  Serial.print(green);
  Serial.print(F(", yellow "));
  Serial.println(yellow);
#endif

  if (green == READER_COUNT) {
    delay(RESULT_PAUSE);
    startEndScreen(S_MM_WON);
  } else if (mmAttemptCount >= MM_MAX_ATTEMPTS) {
    delay(RESULT_PAUSE);
    startEndScreen(S_MM_LOST);
  } else {
    // Next row: immediately shows the chips present (not blinking,
    // since identical to the attempt just confirmed)
    restartBlink();
  }
}

// Generate the secret sequence and start a new game
void mmNewGame() {
  randomSeed(buttonTime);   // moment of the button press = true randomness

  for (byte i = 0; i < READER_COUNT; i++) {
    mmSecretCode[i]  = random(1, COLOR_COUNT);   // 1..4
    mmLastAttempt[i] = COLOR_WHITE;              // no attempt yet
  }
  mmAttemptCount = 0;

  matrix.fillScreen(0);

  if (MM_DEBUG_SHOW_SOLUTION) {
    for (byte i = 0; i < READER_COUNT; i++) {
      setLED(MM_SOLUTION_DEBUG_ROW, MM_FEEDBACK_POSITION[i], colors[mmSecretCode[i]]);
    }
  }

#if DEBUG
  Serial.print(F("=== New Mastermind game === Solution: "));
  reportSequence(mmSecretCode, READER_COUNT);
  Serial.println();
#endif

  state = S_MM_GAME;   // before restartBlink(): drawInputRow() depends on the state
  restartBlink();

  discardButtons();    // discard old button presses
  cancelHolds();       // a button held since the start screen does not quit the game
}

// Still image: solution as 4 color blocks (2x2), below it attempts resp. red X
void mmDrawEndImage() {
  matrix.fillScreen(0);

  // Solution, blocks at x = 1, 5, 9, 13 and y = 2..3 (from 0)
  for (byte i = 0; i < READER_COUNT; i++) {
    matrix.fillRect(1 + i * 4, 2, 2, 2, colorValue(colors[mmSecretCode[i]]));
  }

  if (state == S_MM_WON) {
    // Number of attempts, bold and centered
    drawNumber(mmAttemptCount, 7, colors[COLOR_GREEN]);

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

// ========================== Color Memory Game ============================

// Set the grid LED of sequence position index (from 0)
void cmSetGridLED(byte index, const byte color[3]) {
  setLED(CM_INPUT_ROW[index / READER_COUNT],
         CM_GRID_POSITION[index % READER_COUNT], color);
}

// Set the feedback LED directly below sequence position index (from 0)
void cmSetFeedbackLED(byte index, const byte color[3]) {
  setLED(CM_FEEDBACK_ROW[index / READER_COUNT],
         CM_GRID_POSITION[index % READER_COUNT], color);
}

// Number of readers needed for the current block
// (4, for the last block only the colors that are left)
byte cmBlockSize() {
  byte remaining = cmLength - cmInputPosition;
  return (remaining < READER_COUNT) ? remaining : READER_COUNT;
}

// Are all readers of the current block free of chips?
bool cmBlockReadersEmpty() {
  for (byte i = 0; i < cmBlockSize(); i++) {
    if (chipPresent[i]) return false;
  }
  return true;
}

// Is a known chip present on all readers of the current block?
bool cmInputComplete() {
  for (byte i = 0; i < cmBlockSize(); i++) {
    if (readerColor[i] == COLOR_WHITE) return false;
  }
  return true;
}

// Complete AND the chips of the last block were removed -> may be confirmed
bool cmInputConfirmable() {
  return !cmWaitForEmpty && cmInputComplete();
}

// Draws the current input row (takes the blinking into account).
// As long as the chips of the last block have not all been removed
// (cmWaitForEmpty), the row stays white: the chip colors only appear once
// all readers of the block were empty and chips are placed again.
void cmDrawInputRow() {
  bool visible = !cmInputConfirmable() || blinkOn;

  for (byte i = 0; i < cmBlockSize(); i++) {
    const byte* color;
    if (cmWaitForEmpty) color = colors[COLOR_WHITE];
    else if (visible)   color = colors[readerColor[i]];
    else                color = OFF;
    cmSetGridLED(cmInputPosition + i, color);
  }
  matrix.show();
}

// Display time of the sequence, grows with its length
unsigned long cmShowDuration() {
  return CM_SHOW_TIME_BASE + (unsigned long)cmLength * CM_SHOW_TIME_PER_COLOR;
}

// Next round: generate a COMPLETELY NEW sequence, one color longer than
// before (up to CM_MAX_LENGTH), and start showing it.
void cmNextRound() {
  if (cmLength < CM_MAX_LENGTH) cmLength++;

  for (byte i = 0; i < cmLength; i++) {
    cmSequence[i] = random(1, COLOR_COUNT);   // 1..4
  }
  cmInputPosition = 0;
  cmWaitForEmpty  = true;   // chips of the last round must be removed first

#if DEBUG
  Serial.print(F("New round, "));
  Serial.print(cmLength);
  Serial.print(F(" colors: "));
  reportSequence(cmSequence, cmLength);
  Serial.println();
#endif

  matrix.fillScreen(0);
  matrix.show();

  cmSequenceVisible = false;   // dark pause first
  cmShowSince = millis();
  state = S_CM_SHOW;
}

// Start a new game in round 1
void cmNewGame() {
  randomSeed(buttonTime);   // moment of the button press = true randomness

  cmLength = 0;
  cmScore  = 0;
  cancelHolds();   // a button held since the start screen does not quit the game

#if DEBUG
  Serial.println(F("=== New Color Memory game ==="));
#endif

  cmNextRound();
}

// End of the display: all used grid places turn white, input begins
void cmStartInput() {
  for (byte i = 0; i < cmLength; i++) {
    cmSetGridLED(i, colors[COLOR_WHITE]);
  }

  state = S_CM_INPUT;   // before restartBlink(): drawInputRow() depends on the state
  restartBlink();

  discardButtons();     // discard presses made during the display
}

// Compare the complete input position by position and show the result
// directly below the colors. Every correct position = 1 point.
void cmEvaluate() {
  bool error = false;
  byte correctCount = 0;

  for (byte i = 0; i < cmLength; i++) {
    bool correct = (cmInput[i] == cmSequence[i]);
    cmSetFeedbackLED(i, colors[correct ? COLOR_GREEN : COLOR_RED]);

    if (correct) {
      correctCount++;
      cmScore++;
    } else {
      error = true;
#if DEBUG
      Serial.print(F("Position "));
      Serial.print(i + 1);
      Serial.print(F(": wrong - expected "));
      Serial.print(COLOR_NAMES[cmSequence[i]]);
      Serial.print(F(", entered "));
      Serial.println(COLOR_NAMES[cmInput[i]]);
#endif
    }
  }
  matrix.show();

#if DEBUG
  Serial.print(F("Evaluation: "));
  Serial.print(correctCount);
  Serial.print(F(" / "));
  Serial.print(cmLength);
  Serial.print(F(" correct -> score "));
  Serial.println(cmScore);
#endif

  delay(RESULT_PAUSE);

  if (error) startEndScreen(S_CM_GAME_OVER);
  else       cmNextRound();
}

// Button 1 was pressed during the input
void cmConfirmBlock() {
  if (!cmInputConfirmable()) {
#if DEBUG
    Serial.println(cmWaitForEmpty
      ? F("Button ignored (remove the chips of the last block first)")
      : F("Button ignored (block incomplete)"));
#endif
    return;
  }

  // Chip lock: one more complete round with a fresh power test (see
  // mmConfirmAttempt)
  if (CHIP_LOCK && pollReaders(true)) {
    restartBlink();
    fetchButton(BUTTON_CONFIRM);   // discard presses during the check
#if DEBUG
    Serial.println(F("Button ignored (input changed during the check)"));
#endif
    return;
  }
  if (!cmInputConfirmable()) return;

  // Store the block and leave it fixed (not blinking) on the grid.
  // It is NOT evaluated yet.
  byte size = cmBlockSize();
  for (byte i = 0; i < size; i++) {
    cmInput[cmInputPosition + i] = readerColor[i];
    cmSetGridLED(cmInputPosition + i, colors[readerColor[i]]);
  }
  matrix.show();

#if DEBUG
  Serial.print(F("Block "));
  Serial.print(cmInputPosition / READER_COUNT + 1);
  Serial.print(F(": "));
  reportSequence(cmInput + cmInputPosition, size);
  Serial.print(F("-> "));
  Serial.print(cmInputPosition + size);
  Serial.print(F(" / "));
  Serial.println(cmLength);
#endif

  cmInputPosition += size;
  cmWaitForEmpty = true;   // chips have to be removed before the next block

  if (cmInputPosition >= cmLength) {
    cmEvaluate();
    return;
  }

  // Next row: stays white until all chips were removed and new ones are
  // placed (see cmDrawInputRow)
  restartBlink();
}

// Still image: score (green)
void cmDrawEndImage() {
  matrix.fillScreen(0);
  drawNumber(cmScore, TEXT_Y, colors[COLOR_GREEN]);
  matrix.show();
}

// =============================== Screens =================================

// Start screen of a game (S_MM_START or S_CM_START)
void startStartScreen(byte startState) {
  state = startState;
  scrollX = 16;
  discardButtons();

#if DEBUG
  Serial.println(state == S_MM_START
    ? F("Selected game: Mastermind")
    : F("Selected game: Color Memory Game"));
#endif
}

// End screen (S_MM_WON, S_MM_LOST or S_CM_GAME_OVER)
void startEndScreen(byte endState) {
  state = endState;
  scrollX = 16;
  endImageActive = false;
  discardButtons();   // discard presses during the result pause

#if DEBUG
  if (state == S_MM_WON) {
    Serial.println(F("*** WON ***"));
  } else if (state == S_MM_LOST) {
    Serial.println(F("*** LOST ***"));
  } else {
    Serial.print(F("*** GAME OVER *** Score: "));
    Serial.println(cmScore);
  }
#endif
}

// Still image of the current end screen
void drawEndImage() {
  if (state == S_CM_GAME_OVER) cmDrawEndImage();
  else                         mmDrawEndImage();
}

// Button 2 / 3 on a start or end screen: show the start screen of the
// selected game. Returns true if a game was selected.
bool selectGame() {
  if (fetchButton(BUTTON_MASTERMIND)) {
    startStartScreen(S_MM_START);
    return true;
  }
  if (fetchButton(BUTTON_COLOR_MEMORY)) {
    startStartScreen(S_CM_START);
    return true;
  }
  return false;
}

// During a running game a short press of button 2 / 3 is ignored (the
// press is discarded, so it does not act later on the end screen)
void ignoreSelectButtons() {
  if (fetchButton(BUTTON_MASTERMIND) | fetchButton(BUTTON_COLOR_MEMORY)) {   // | : fetch both
#if DEBUG
    Serial.println(F("Game selection ignored (game running - hold the button to quit)"));
#endif
  }
}

// Called in every running game state. Short presses of button 2 / 3 are
// ignored; holding one of them for ABORT_HOLD_TIME quits the game and shows
// the start screen of the selected game. Returns true if the game was quit.
bool checkAbort() {
  ignoreSelectButtons();

  byte startState;
  if (buttonHeld(BUTTON_MASTERMIND))        startState = S_MM_START;
  else if (buttonHeld(BUTTON_COLOR_MEMORY)) startState = S_CM_START;
  else return false;

#if DEBUG
  Serial.println(F("*** Game quit (button held) ***"));
#endif
  cancelHolds();   // the button is still down: it must not count again
  startStartScreen(startState);
  return true;
}

// =============================== States ==================================

// S_MM_START, S_CM_START
void loopStart() {
  if (selectGame()) return;

  bool mastermind = (state == S_MM_START);

  if (fetchButton(BUTTON_CONFIRM)) {
    if (mastermind) mmNewGame();
    else            cmNewGame();
    return;
  }
  scrollTextStep(mastermind ? TEXT_MM_START : TEXT_CM_START, NULL);   // colorful
}

// S_MM_GAME
void mmLoopGame() {
  if (checkAbort()) return;

  if (pollReaders(false)) {
    restartBlink();   // start visible after every change
  }

  if (fetchButton(BUTTON_CONFIRM)) {
    mmConfirmAttempt();
    return;
  }

  delay(POLL_INTERVAL);
}

// S_CM_SHOW: dark pause, then the whole sequence for cmShowDuration()
void cmLoopShow() {
  if (checkAbort()) return;

  if (!cmSequenceVisible) {
    if (millis() - cmShowSince >= CM_SHOW_PAUSE) {
      for (byte i = 0; i < cmLength; i++) {
        cmSetGridLED(i, colors[cmSequence[i]]);
      }
      matrix.show();
      cmSequenceVisible = true;
      cmShowSince = millis();
    }
  } else if (millis() - cmShowSince >= cmShowDuration()) {
    cmStartInput();
  }
}

// S_CM_INPUT
void cmLoopInput() {
  if (checkAbort()) return;

  bool changed = pollReaders(false);

  // Chips of the last block removed -> the next block may be confirmed
  if (cmWaitForEmpty && cmBlockReadersEmpty()) {
    cmWaitForEmpty = false;
    changed = true;
#if DEBUG
    Serial.println(F("Readers empty - next block can be entered"));
#endif
  }

  if (changed) {
    restartBlink();   // start visible after every change
  }

  if (fetchButton(BUTTON_CONFIRM)) {
    cmConfirmBlock();
    return;
  }

  delay(POLL_INTERVAL);
}

// S_MM_WON, S_MM_LOST, S_CM_GAME_OVER:
// alternates between the scrolling text and the end image
void loopEnd() {
  if (selectGame()) return;

  if (fetchButton(BUTTON_CONFIRM)) {
    if (state == S_CM_GAME_OVER) cmNewGame();
    else                         mmNewGame();
    return;
  }

  if (!endImageActive) {
    const char* text  = TEXT_CM_GAME_OVER;
    const byte* color = colors[COLOR_RED];
    if (state == S_MM_WON) {
      text  = TEXT_MM_WIN;
      color = colors[COLOR_GREEN];
    } else if (state == S_MM_LOST) {
      text  = TEXT_MM_LOSE;
    }

    if (scrollTextStep(text, color)) {
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
  Serial.println(F("=== RFID Games: Mastermind + Color Memory Game ==="));
#endif

  // LED matrix
  matrix.begin();
  matrix.setBrightness(BRIGHTNESS);
  matrix.setTextWrap(false);
  matrix.fillScreen(0);
  matrix.show();

  // Buttons
  for (byte b = 0; b < BUTTON_COUNT; b++) {
    pinMode(BUTTON_PINS[b], INPUT_PULLUP);
    buttonPressed[b]  = false;
    buttonHoldUsed[b] = true;    // no hold without a press
  }
  attachInterrupt(digitalPinToInterrupt(BUTTON_PINS[BUTTON_CONFIRM]),      buttonConfirmISR,     CHANGE);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PINS[BUTTON_MASTERMIND]),   buttonMastermindISR,  CHANGE);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PINS[BUTTON_COLOR_MEMORY]), buttonColorMemoryISR, CHANGE);

  // RFID readers
  // SPI.begin() uses pins 50/51/52 on the Mega and makes pin 53 (SS) an
  // output, which keeps the Mega in SPI master mode.
  SPI.begin();

  // Step 1: initialize all readers.
  // PCD_Init() resets the reader (gain and transmitter power back to the
  // defaults, antenna on). Because RST is shared, all inits are done first,
  // and the settings are only made afterwards in a separate loop - so no
  // later init can undo them.
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
  }

  // Step 2: set gain + transmitter power of every reader and switch off
  // the antennas
  for (byte i = 0; i < READER_COUNT; i++) {
    gainStep[i] = RFID_GAIN_STEP[i];
    txStep[i]   = TX_POWER_STEP[i];
  }
  applyRfidSettings();
#if DEBUG
  reportRfidSettings();
#endif

  for (byte i = 0; i < READER_COUNT; i++) {
    reader[i].PCD_AntennaOff();   // only ever one antenna active at a time

    readerColor[i]  = COLOR_WHITE;
    chipPresent[i]  = false;
    failedReads[i]  = 0;
#if DEBUG
    fallbackState[i] = FALLBACK_NONE;
#endif
  }

#if DEBUG
  checkTable();
#endif

  startStartScreen(S_MM_START);   // Mastermind is selected after switching on
}

void loop() {
#if DEBUG
  serialTuning();
#endif

  switch (state) {
    case S_MM_START:
    case S_CM_START:     loopStart();   break;
    case S_MM_GAME:      mmLoopGame();  break;
    case S_CM_SHOW:      cmLoopShow();  break;
    case S_CM_INPUT:     cmLoopInput(); break;
    case S_MM_WON:
    case S_MM_LOST:
    case S_CM_GAME_OVER: loopEnd();     break;
  }
}
