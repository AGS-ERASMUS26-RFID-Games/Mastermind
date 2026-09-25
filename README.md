# Mastermind – Documentation

Arduino UNO version of the game *Mastermind* with four RFID readers, a
16×16 LED matrix and a confirmation button.

> The wiring is documented separately. This file describes the game
> principle and the structure of the code.

---

## 1. Game principle

At the start of a game, the Arduino rolls a secret sequence of four colors.
The choices are **Red, Green, Blue and Yellow**; each color may appear more
than once. The player tries to guess this sequence.

### Course of a round

1. **Input:** A color chip is placed on each of the four readers. The
   corresponding LED in the current row shows the color of the chip. If no
   chip (or an unknown chip) is present, the LED lights up white.
2. **Confirm:** As soon as a known chip lies on all four readers, the row
   **blinks**. Pressing the button confirms the input.
3. **Evaluate:** To the right of the input, the result appears as four LEDs:
   - **Green** = right color in the right position
   - **Yellow** = right color, but in the wrong position
   - **Off** = no hit for this color

   The evaluation is **not position-based**: all green LEDs light up first,
   then all yellow ones. So the order does *not* reveal which position is
   correct – exactly like real Mastermind.
4. **Continue:** The next attempt is entered in the row below.

### Winning and losing

- **Won:** All four evaluation LEDs are green (color *and* position correct
  everywhere).
- **Lost:** The maximum number of attempts (`MAX_ATTEMPTS`, default **10**)
  is reached without the sequence being guessed.

In both cases a final screen appears (see section 3), after which a new game
starts at the press of the button.

### Evaluation – an example

Secret sequence: **Red · Red · Green · Blue**

| Input | Green | Yellow | Explanation |
|---|---|---|---|
| Red · Green · Green · Blue | 3 | 0 | Positions 1, 3, 4 match; the second green is one too many |
| Red · Blue · Blue · Red | 1 | 1 | Position 1 matches (green); another Red exists in the sequence but is misplaced (yellow) |
| Yellow · Yellow · Yellow · Yellow | 0 | 0 | Yellow does not appear in the sequence at all |

Important is the rule for duplicate colors: Each chip of the secret sequence
can produce only **one** hit. A second Red in the input only turns yellow if
the secret sequence also contains a second (not yet matched) Red.

---

## 2. Operation and important constants

All settings are grouped together at the top of the sketch under *Settings*
and *Pins*.

| Constant | Default | Meaning |
|---|---|---|
| `DEBUG` | `1` | `1` = serial output on (UIDs, solution, evaluation), `0` = off (saves a little memory) |
| `DEBUG_SHOW_SOLUTION` | `false` | `true` = show the secret sequence in row 16 during the game (for testing) |
| `MAX_ATTEMPTS` | `10` | Number of attempts, after which the game is lost. Allowed are **1–15** (row 16 is reserved for the debug solution). An invalid value produces a clear error message at compile time. |
| `BRIGHTNESS` | `4` | Brightness of the matrix (0–255) |
| `BLINK_INTERVAL` | `250` ms | Blink rate of the complete input |
| `SCROLL_INTERVAL` | `70` ms | Speed of the scrolling texts (smaller = faster) |
| `END_IMAGE_DURATION` | `4000` ms | Dwell time of the solution image before the scrolling text runs again |
| `RESULT_PAUSE` | `1500` ms | Leave the last evaluation on screen before the end screen appears |
| `DEBOUNCE_TIME` | `30` ms | Debounce of the button |

The RFID timing values (`MAX_FAILED_READS`, `POLL_INTERVAL`,
`ANTENNA_WAIT`) come from the original reader sketch and are unchanged. If
chips are detected unreliably, increase `ANTENNA_WAIT` first.

### First-time setup: filling the chip table

The 16 chips are entered in the `CHIPS[]` table – each UID plus a color index
(1 = Red, 2 = Green, 3 = Blue, 4 = Yellow). To determine a chip's UID, leave
`DEBUG` at `1`, open the Serial Monitor (9600 baud) and place the chip on a
reader. Write the printed hex UID into the matching slot in the table. At
startup, `checkTable()` automatically checks for empty slots, duplicate UIDs,
wrong color distribution and invalid color indices, and reports anomalies via
serial.

---

## 3. Display on the matrix

The matrix is 16×16 pixels. Rows and positions are counted **from 1** in the
code (`setLED(row, position, color)`).

```
        Position →   3  4  5  6      10 11 12 13
Row 1                [ Input    ]    [ Feedback ]   ← Attempt 1
Row 2                [ Input    ]    [ Feedback ]   ← Attempt 2
  ...
Row 10               [ Input    ]    [ Feedback ]   ← Attempt 10
  ...
Row 16                               [ Solution ]   ← only with DEBUG_SHOW_SOLUTION
```

- **Input** (columns 3–6): the four readers from left to right.
- **Feedback** (columns 10–13): green/yellow/off, see above.
- **Row 16** shows the secret sequence when `DEBUG_SHOW_SOLUTION` is active.

All texts on the matrix are in English, and the serial debug output is in
English as well.

### Orientation of the matrix

On our matrix the first LED (pixel 0) sits **top right**, the first row runs
from right to left, and each further row alternates direction (zigzag). In the
code this is set via `NEO_MATRIX_TOP + NEO_MATRIX_RIGHT + NEO_MATRIX_ROWS +
NEO_MATRIX_ZIGZAG`. The library uses this to convert the coordinates itself,
so that position 1 is on the left and row 1 at the top, and text scrolls
through the right way round. If the matrix is ever installed differently, only
this line needs to be adjusted.

### Win and lose screen

Both final screens alternate between a scrolling text and a still image until
the button is pressed:

- **Scrolling text:** “YOU WIN!” in green resp. “YOU LOSE” in red.
- **Still image:** at the top the correct solution as four large 3×3 color
  blocks. Below it, when winning, the **number of attempts needed** as a green
  number; when losing, a **red X**.

Pressing the button starts a new game from either screen.

---

## 4. Structure of the code

The sketch is organized as a **state machine**. There is always exactly one
active state, and `loop()` calls the matching function depending on the state:

```
      Start
        │  Button
        ▼
   ┌─► Game ───── all green ──────► Won ───────┐
   │    │                                      │ Button
   │    └──── MAX_ATTEMPTS reached ► Lost ──────┤
   └───────────────◄─── new game ──────────────┘
```

| State | Constant | Function in `loop()` |
|---|---|---|
| Start screen | `S_START` | `loopStart()` |
| Running game | `S_GAME` | `loopGame()` |
| Win screen | `S_WON` | `loopEnd()` |
| Lose screen | `S_LOST` | `loopEnd()` |

### The most important functions

**Flow / states**

- `setup()` – initializes the matrix, the button interrupt and the four
  readers, and enters the start screen.
- `loop()` – dispatches to the three loop functions depending on `state`.
- `loopStart()` – shows the colorful start scrolling text “MASTERMIND - PRESS BUTTON”; a button press calls `newGame()`.
- `loopGame()` – polls the readers, updates the display, lets the row blink
  and reacts to the button.
- `loopEnd()` – alternates between the final scrolling text and the solution
  image; the button starts a new game.

**Game logic**

- `newGame()` – generates the secret sequence and resets everything.
- `confirmAttempt()` – checks whether confirmation is allowed, evaluates the
  attempt, draws the result, increments `attemptCount` and decides between
  win/lose/continue.
- `evaluate(attempt, green, yellow)` – the actual Mastermind evaluation
  (green = color + position, yellow = color only). Handles duplicate colors
  correctly via two count fields.
- `inputComplete()` / `inputConfirmable()` – check whether all four readers
  are occupied resp. whether the input is also different from the last
  attempt. Only then does the row blink and the button get accepted.

**RFID**

- `readChip(reader, uidTarget)` – polls a single reader. Only the antenna of
  the reader currently being polled is ever active; this prevents mutual
  interference and relieves the 3.3 V regulator.
- `findColor(uid)` – looks up the UID in the chip table and returns the color
  index (unknown → 0 = White).
- `checkReader(i)` – encapsulates new detection, chip swap and the
  failed-read counter against dropouts. Returns `true` if the displayed color
  has changed.

**Display**

- `setLED(row, position, color)` – sets one LED (coordinates from 1).
- `drawInputRow()` – draws the current input row, including the blink state.
- `drawFeedback(row, green, yellow)` – paints the green/yellow/off LEDs.
- `drawEndImage()` – solution blocks plus attempt count resp. red X.
- `scrollTextStep(text, color)` – draws **one** step of a scrolling text
  (non-blocking) and reports when the text has run through completely.

**Button**

- `buttonISR()` – interrupt routine at pin 2. Detects a real press via
  edge + debounce and remembers the moment (`micros()`).
- `fetchButton()` – hands a pending press to the main program and resets the
  flag; also used to discard old presses.

### Why an interrupt for the button?

When no chip is present, each reader waits for a timeout. A full polling round
can therefore take more than 100 ms. A short click that falls exactly within
that time would be missed with a simple `digitalRead()`. Pin 2 is
interrupt-capable, so the press is captured via an interrupt and buffered
safely – no button press is lost, guaranteed.

### Randomness and memory

- **Randomness:** The moment of the very first button press (`micros()`)
  serves as the seed for the random number generator. This is practically
  unpredictable, so the secret sequence reliably differs from game to game.
- **Memory:** The UNO has only 2 KB of RAM, of which the matrix already
  occupies 768 bytes. That is why the chip table (`CHIPS[]`) and all texts
  live in flash (`PROGMEM`) instead of in RAM. Already-played attempts are not
  stored separately – they are on the matrix anyway.

### Memory usage (Arduino UNO / ATmega328P)

| Variant | Flash | RAM (incl. 768-byte matrix buffer at runtime) |
|---|---|---|
| `DEBUG 1` | approx. 58 % | approx. 71 % |
| `DEBUG 0` | approx. 52 % | approx. 69 % |

The RAM figure reported by the Arduino IDE is lower because it does not yet
include the matrix buffer – that is only allocated at runtime.

## 5. Wiring
![schematic for the Wiring of the electronic parts](mastermind_kicad\images\mastermind_schematic.png)