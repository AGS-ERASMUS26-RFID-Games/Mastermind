# Mastermind – Documentation

Arduino Mega 2560 version of the game *Mastermind* with four RFID readers,
a 16×16 LED matrix and a confirmation button.

This file describes the wiring (section 5), the game principle and the
structure of the code. The complete schematic is `mastermind.kicad_sch`
(KiCad 7).

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
   **blinks**. Pressing the button confirms the input. With the chip lock
   active, all readers are checked once more first; if the result differs
   from the display, the press is ignored and the corrected row is shown
   (see "Chip lock").
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
- **Lost:** The maximum number of attempts (`MAX_ATTEMPTS`) is reached
  without the sequence being guessed.

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
| `DEBUG_SHOW_SOLUTION` | `true` (test setting) | `true` = show the secret sequence in row 16 during the game (for testing). Set to `false` for normal play. |
| `MAX_ATTEMPTS` | `1` (test setting) | Number of attempts, after which the game is lost. Allowed are **1–15** (row 16 is reserved for the debug solution). An invalid value produces a clear error message at compile time. Use e.g. `10` for normal play. |
| `BRIGHTNESS` | `4` | Brightness of the matrix (0–255) |
| `BLINK_INTERVAL` | `250` ms | Blink rate of the complete input |
| `SCROLL_INTERVAL` | `70` ms | Speed of the scrolling texts (smaller = faster) |
| `END_IMAGE_DURATION` | `4000` ms | Dwell time of the solution image before the scrolling text runs again |
| `RESULT_PAUSE` | `1500` ms | Leave the last evaluation on screen before the end screen appears |
| `DEBOUNCE_TIME` | `30` ms | Debounce of the button |

The RFID timing values (`MAX_FAILED_READS`, `POLL_INTERVAL`,
`ANTENNA_WAIT`) come from the original reader sketch and are unchanged. If
chips are detected unreliably, increase `ANTENNA_WAIT` first.

### Range of the readers: gain and transmitter power

If a chip is also detected by a neighboring reader, the range of the
readers can be reduced with two settings. Both are applied to all four
readers.

`RFID_GAIN_STEP` (default `2`) sets the receiver gain, i.e. how well the
reader "hears" the answer of a chip. The RC522 only knows these six values,
there are no steps in between:

| Step | 0 | 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|---|
| Gain | 18 dB | 23 dB | 33 dB (library default) | 38 dB | 43 dB | 48 dB |

`TX_POWER_STEP` (default `4`, allowed `1`–`8`) sets the transmitter power,
i.e. the strength of the field itself. A weaker field means a chip on a
neighboring reader is no longer powered at all, which usually works better
than a lower gain. Step 8 is the library default (full power). The MFRC522
library has no function for this; the sketch writes the driver conductance
registers directly: `CWGsPReg` = step × 4 and the upper half of `GsNReg` =
step (step 8 → `0x20` / `0x8` = reset values). The field does not drop
linearly: neighboring steps may hardly differ, and below a certain step
detection stops abruptly.

How far the range actually drops depends on the module and its antenna, so
the right combination has to be found by testing. For this, both values
can be changed at runtime via the Serial Monitor (`DEBUG 1`, 9600 baud),
without uploading the sketch again:

| Input | Effect |
|---|---|
| `g0` … `g5` | set gain step |
| `t1` … `t8` | set transmitter power step |
| `?` | show current settings |

After every change the values read back from each reader are printed
(gain `0x00`/`0x10`/`0x40`/…, `CWGsP` = step × 4, `GsN` with the step in the
upper digit). If a reader shows different values, it did not accept the
setting. Recommended procedure: for each setting, first check that chips
lying directly on each reader are still detected, then check that a chip on
one reader no longer appears on its neighbor. Enter the combination found
in `RFID_GAIN_STEP` and `TX_POWER_STEP` – changes made via serial are lost
after a restart.

### Chip lock

If gain and transmitter power alone do not prevent cross-reading, the chip
lock (`CHIP_LOCK = true`, default) makes sure that every chip is assigned
to exactly one reader. Each polling round then runs in three steps:

1. **Read:** every reader reads *all* chips in its field (up to
   `MAX_CHIPS_PER_READER`, default 3), not just one. This matters because
   with two chips in the field the RC522 selects one by its UID bits, not by
   distance – a reader could otherwise report the neighbor's chip instead of
   its own. The reader wakes all chips (WUPA), reads one, puts it to sleep
   (HALT) and then asks again with REQA, which sleeping chips ignore, until
   no chip answers any more.
2. **Assign:** every chip is assigned to at most one reader, by these rules:
   - *Elimination:* if only one free reader is left for a chip, the chip
     belongs to it and the reader is taken. Example: reader 1 sees A and B,
     reader 2 only B → A can only belong to reader 1, so B belongs to
     reader 2. This solves the common case without any measurement.
   - A chip whose readers are all taken belongs to nobody (it was only
     cross-read).
   - *Power test:* if a chip is still seen by several free readers, the
     transmitter power of each of them is lowered step by step (binary
     search, 3–4 reads per reader). The reader that still sees the chip at
     the lowest step is closest and gets it. The result is remembered for
     `PROBE_CACHE_TIME` (default 3000 ms) as long as the same readers
     compete, so the test does not run in every round.
   - If several chips can only belong to the same reader, it keeps the chip
     it already shows; otherwise the power test decides here as well.
   - *In case of doubt white:* on a tie nobody gets the chip. The affected
     reader shows white, the row does not blink and cannot be confirmed –
     the player sees the problem and straightens the chip.

   The reader that currently shows a chip stays a candidate for it even if
   it missed it in this round, so a single dropout does not hand the chip
   over to a neighbor.
3. **Update:** if a chip now belongs to a different reader than before, it
   is removed from the old reader at once. Otherwise the usual rules apply
   (new chip / swap immediately, removal after `MAX_FAILED_READS` failed
   reads).

Before a button press is accepted, one more complete round runs with a
fresh power test (no remembered results). Only if nothing changes is the
attempt evaluated. A wrong color can therefore never be confirmed.

With `DEBUG 1` every power test is printed, e.g.
`Chip 04 99 32 BB seen by readers 1+2 -> power test: R1=4 R2=1 -> reader 2`
(the number is the lowest step at which the reader still saw the chip,
`-` = not at all). This also shows how clearly the readers can be told
apart: if the numbers are far apart, the setting is good; if they are
often equal, lower the transmitter power a little further.

**Cost:** a reader with a chip needs one extra request per round (about
25 ms timeout), so a round with all four readers occupied takes roughly
100 ms longer. A power test takes a few hundred milliseconds, during which
blinking may stutter briefly. `CHIP_LOCK = false` restores the old
behavior (every reader shows the first chip it sees) for comparison.

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
  readers (first all `PCD_Init()` calls, then `applyRfidSettings()` and
  antenna-off, so no later init resets these settings), and enters the
  start screen.
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

- `readAllChips(reader, uids)` – polls a single reader and reads all chips
  in its field (with `CHIP_LOCK = false` only the first). Only the antenna
  of the reader currently being polled is ever active; this prevents mutual
  interference and relieves the 3.3 V regulator.
- `setTxPower(i, step)` – sets the transmitter power of one reader.
- `findColor(uid)` – looks up the UID in the chip table and returns the color
  index (unknown → 0 = White).
- `applyRfidSettings()` – writes the current gain and transmitter power
  step to all four readers.
- `serialTuning()` – (only with `DEBUG 1`) reads the commands `g0`–`g5`,
  `t1`–`t8` and `?` from the Serial Monitor.
- `pollReaders(fresh)` – one complete round: read all readers, assign the
  chips, update the display state. Returns `true` if a displayed color has
  changed. `fresh = true` ignores remembered power-test results (used
  before confirming).
- `updateReader(i, uid)` – encapsulates new detection, chip swap and the
  failed-read counter against dropouts for one reader.

**Chip lock**

- `resolveOwners(owner, fresh)` – assigns every chip seen in the round to
  at most one reader (elimination, then power test).
- `decideContested(uid, mask, fresh)` – power test for a chip seen by
  several readers, with the remembered results (`probeCache`).
- `decideOnReader(...)` – several chips that can only belong to the same
  reader: keep the current one, otherwise power test.
- `probeThreshold(i, uid)` – lowest transmitter power step at which reader
  `i` still sees the chip (binary search).

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
can therefore take more than 100 ms – with the chip lock and all readers
occupied about 250–300 ms, during a power test even longer. A short click that falls exactly within
that time would be missed with a simple `digitalRead()`. Pin 2 is
interrupt-capable, so the press is captured via an interrupt and buffered
safely – no button press is lost, guaranteed.

On the Mega the interrupt-capable pins are D2, D3, D18, D19, D20 and D21.
`attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), …)` translates the pin
number into the right interrupt number itself (pin 2 is interrupt 0 on the
Mega as well, internally INT4), so nothing has to be changed in the code if
the button is moved to another of these pins.

### Randomness and memory

- **Randomness:** The moment of the very first button press (`micros()`)
  serves as the seed for the random number generator. This is practically
  unpredictable, so the secret sequence reliably differs from game to game.
- **Memory:** The Mega has 8 KB of RAM (the UNO only 2 KB), of which the
  matrix occupies 768 bytes. The chip table (`CHIPS[]`) and all texts still
  live in flash (`PROGMEM`) instead of in RAM. On the Mega this is no longer
  strictly necessary, but it costs nothing and leaves RAM free for later
  extensions. Already-played attempts are not stored separately – they are
  on the matrix anyway.

### Memory usage (Arduino Mega 2560 / ATmega2560)

Measured with Arduino AVR core 1.8.6:

| Variant | Flash (of 248 KB) | RAM (incl. 768-byte matrix buffer at runtime) |
|---|---|---|
| `DEBUG 1` | 23 780 bytes ≈ 9 % | 1 568 bytes ≈ 19 % |
| `DEBUG 0` | 20 530 bytes ≈ 8 % | 1 513 bytes ≈ 18 % |

The chip lock needs about 3 KB of flash and about 100 bytes of RAM. With
more than 6 KB of free RAM there is plenty of room for the stack and for
extensions (e.g. the two reserved buttons, more readers, sound).

The RAM figure reported by the Arduino IDE is lower (800 resp. 745 bytes)
because it does not yet include the matrix buffer – that is only allocated
at runtime.

---

## 5. Wiring (Arduino Mega 2560)

The complete schematic is in `mastermind.kicad_sch`. In the sketch, the pins
are defined at the top under *Pins*.

![Schematic – Arduino Mega 2560 version](/mastermind_kicad/mastermind_schematic.png)

### Pin assignment

| Mega pin | Signal | Connected to | Note |
|---|---|---|---|
| D2 | `BTN1` | Button 1 (confirm) → GND | interrupt pin, `INPUT_PULLUP`, pressed = LOW |
| D3 | `BTN2` | Button 2 → GND | reserved, not used by the firmware yet (interrupt pin) |
| D5 | `LED_DIN` | LED matrix DATA/DIN | |
| D6 | `SS4` | Reader 4 SDA | |
| D7 | `SS3` | Reader 3 SDA | |
| D8 | `SS2` | Reader 2 SDA | |
| D9 | `RST` | RST of all 4 readers | shared |
| D10 | `SS1` | Reader 1 SDA | |
| D18 | `BTN3` | Button 3 → GND | reserved, not used by the firmware yet (interrupt pin; TX1, free as long as `Serial1` is not used) |
| D50 | `MISO` | MISO of all 4 readers | hardware SPI, shared |
| D51 | `MOSI` | MOSI of all 4 readers | hardware SPI, shared |
| D52 | `SCK` | SCK of all 4 readers | hardware SPI, shared |
| D53 | – | not connected | hardware SS, stays an output (`SPI.begin()`) |
| 3.3V | `+3V3` | 3.3V of all 4 readers | **not 5 V!** |
| 5V | `+5V` | LED matrix 5V | see "Power supply" |
| GND | `GND` | readers, matrix, buttons | common ground |
| D0 / D1 | – | not connected | USB serial (Serial Monitor) |

The IRQ pins of the readers are not used.

### Changes compared to the UNO wiring

- **SPI moves:** on the Mega the hardware SPI is on D50 (MISO), D51 (MOSI)
  and D52 (SCK) – *not* on D11/D12/D13 as on the UNO. The same three signals
  are also available on the 6-pin ICSP header. If the readers stay connected
  to D11–D13, they are not detected (firmware version `0x00` or `0xFF` in the
  Serial Monitor).
- **Button 3 moves** from D4 to D18, so all three buttons are on
  interrupt-capable pins (on the Mega: D2, D3, D18, D19, D20, D21).
- All other signals (SS1–SS4, RST, LED_DIN, button 1 and 2) keep their pin
  numbers.
- In the Arduino IDE select **Tools → Board → "Arduino Mega or Mega 2560"**
  (processor ATmega2560). If another board is selected, the sketch stops at
  compile time with `Wrong board selected` on purpose.

### Power supply

- The RC522 readers run on **3.3 V only**. The Mega's 3.3 V pin (onboard
  regulator, approx. 150 mA) is enough because only one reader antenna is
  switched on at any time.
- At low brightness (`BRIGHTNESS = 4`) the matrix can run from the Mega's
  5 V pin. For higher brightness use an external 5 V power supply for the
  matrix and connect its ground to the Mega's GND (common ground!).