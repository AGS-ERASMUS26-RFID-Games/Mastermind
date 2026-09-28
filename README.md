# RFID Games – Documentation

Arduino Mega 2560 project with two games, *Mastermind* and the
*Color Memory Game*, on the same hardware: four RFID readers, a 16×16 LED
matrix and three buttons.

This file describes the game selection, both game principles, the settings,
the display, the structure of the code and the wiring (section 7). Both
games are in one sketch, `rfid_games.ino`. The complete schematic is
`mastermind.kicad_sch` (KiCad 7); it did not change for the second game.

---

## 1. Game selection

| Button | Pin | Function |
|---|---|---|
| Button 1 | D2 | start a game, confirm an input |
| Button 2 | D3 | select **Mastermind**; hold 3 s = quit the running game |
| Button 3 | D18 | select **Color Memory Game**; hold 3 s = quit the running game |

- After switching on, **Mastermind** is selected and its start screen appears.
- Button 2 and button 3 work on every **start screen** and every **end
  screen** (win, lose, game over). They show the start screen of the
  selected game. Button 1 then starts it.
- During a **running game**, a short press of button 2 or button 3 is
  ignored, so an accidental press never ends a game. The press is discarded
  and does not act later either.
- **Quitting a running game:** hold button 2 or button 3 for 3 s
  (`ABORT_HOLD_TIME`). The game ends immediately and the start screen of
  the selected game appears – button 2 → Mastermind, button 3 → Color
  Memory Game. There is no progress display; after 3 s the start screen
  simply appears. Releasing earlier cancels the hold, the game continues.
  This works in every phase of a running game, also while the Color Memory
  sequence is shown.
- A button that is already held down when a game starts does not quit the
  new game; it has to be released and pressed again first. The same
  applies after quitting: the button still held does not count twice.
- On an end screen, button 1 starts a new game of the **same** game
  directly.

---

## 2. Mastermind

At the start of a game, the Arduino rolls a secret sequence of four colors.
The choices are **Red, Green, Blue and Yellow**; each color may appear more
than once. The player tries to guess this sequence.

### Course of a round

1. **Input:** A color chip is placed on each of the four readers. The
   corresponding LED in the current row shows the color of the chip. If no
   chip (or an unknown chip) is present, the LED lights up white.
2. **Confirm:** As soon as a known chip lies on all four readers, the row
   **blinks**. Pressing button 1 confirms the input. With the chip lock
   active, all readers are checked once more first; if the result differs
   from the display, the press is ignored and the corrected row is shown
   (see "Chip lock").
3. **Evaluate:** To the right of the input, the result appears as four LEDs:
   - **Green** = right color in the right position
   - **Yellow** = right color, but in the wrong position
   - **Red** = no hit for this chip: its color does not appear in the
     sequence, or all chips of this color in the sequence are already
     matched by other hits (see the duplicate rule below)

   The evaluation is **not position-based**: all green LEDs light up first,
   then all yellow ones, then the red ones. So the order does *not*
   reveal which position is correct – exactly like real Mastermind.
4. **Continue:** The next attempt is entered in the row below.

### Winning and losing

- **Won:** All four evaluation LEDs are green (color *and* position correct
  everywhere).
- **Lost:** The maximum number of attempts (`MM_MAX_ATTEMPTS`) is reached
  without the sequence being guessed.

In both cases a final screen appears (see section 5), after which a new game
starts at the press of button 1.

### Evaluation – an example

Secret sequence: **Red · Red · Green · Blue**

| Input | Green | Yellow | Red | Explanation |
|---|---|---|---|---|
| Red · Green · Green · Blue | 3 | 0 | 1 | Positions 1, 3, 4 match; the second green is one too many |
| Red · Blue · Blue · Red | 1 | 1 | 2 | Position 1 matches (green); another Red exists in the sequence but is misplaced (yellow) |
| Yellow · Yellow · Yellow · Yellow | 0 | 0 | 4 | Yellow does not appear in the sequence at all |

Important is the rule for duplicate colors: Each chip of the secret sequence
can produce only **one** hit. A second Red in the input only turns yellow if
the secret sequence also contains a second (not yet matched) Red.

---

## 3. Color Memory Game

The player has to remember a random color sequence and rebuild it with the
chips. The sequence gets one color longer with every successful round.

### Course of a round

1. **New sequence:** At the beginning of every round the Arduino generates a
   **completely new** random sequence of the colors Red, Green, Blue and
   Yellow. The previous sequence is not extended. Round 1 has 1 color,
   round 2 has 2 colors, and so on up to `CM_MAX_LENGTH` (20). After that,
   every round has 20 colors – the game continues endlessly until a mistake
   is made.
2. **Display:** After a short dark pause, the **whole sequence is shown at
   once** on a grid (see section 5). The display time grows with the
   length: `CM_SHOW_TIME_BASE` + `CM_SHOW_TIME_PER_COLOR` per color
   (default 1.5 s for 1 color, 11 s for 20 colors). Then every used grid
   place turns white: the player sees how many colors are needed, but no
   longer which ones.
3. **Input in blocks:** Because there are only four readers, the sequence is
   entered in blocks of four – one block per grid row. Reader 1 to 4
   correspond to position 1 to 4 of the current row. The last block only
   needs as many readers as colors are left (e.g. 2 readers for a sequence
   of 6). The readers that are not needed are **not read at all** and can
   never get a chip (see "Active readers" in section 4). A chip placed on
   them is ignored.
   - The current row shows the chips live, exactly as in Mastermind (no /
     unknown chip → white).
   - When a known chip lies on every required reader, the row **blinks** and
     button 1 confirms the block. With the chip lock active, the readers are
     checked once more first (see "Chip lock").
   - The confirmed block stays visible on the grid in its colors.
4. **Remove the chips:** Before the next block can be confirmed, the chips
   have to be removed from the required readers. As long as chips of the
   last block are still lying there, the next row stays **white** (it does
   not show the chip colors and does not blink), and button 1 is ignored.
   Only when all required readers were empty does the row show the chips
   live again. This also applies to the first block of a round: chips still
   lying there from the previous round are not shown. This makes it possible to use the same
   chips several times in one sequence (e.g. Red – Blue – Red – Red – Red)
   and prevents a double press from confirming the same block twice.
5. **Evaluate:** Only when the complete sequence has been entered does the
   Arduino compare it with the generated sequence, **position by
   position**. Directly below every color an LED appears:
   - **Green** = correct color at this position → **1 point**
   - **Red** = wrong color at this position
6. **Continue:** If every position is correct, the next round starts with a
   new sequence that is one color longer. If at least one position is
   wrong, the game is over.

### Evaluation – an example

Generated sequence: **Red · Blue · Yellow · Green**

| Position | Sequence | Input | Result |
|---|---|---|---|
| 1 | Red | Red | Green LED, +1 point |
| 2 | Blue | Blue | Green LED, +1 point |
| 3 | Yellow | Green | Red LED |
| 4 | Green | Green | Green LED, +1 point |

Position 3 is wrong, so the game ends after this evaluation.

### Scoring

The score is the number of correct positions **over all rounds of a game**.
Correct positions of the last, failed round count as well.

Example: rounds 1 to 3 without a mistake, then 2 of 4 positions correct in
round 4 → score = 1 + 2 + 3 + 2 = **8**.

### Game over

The end screen alternates between the scrolling text “GAME OVER” in red and
the score as a green number (section 5). Button 1 starts a new game in
round 1 with score 0; button 2 or 3 switches the game.

---

## 4. Operation and important constants

All settings are grouped together at the top of the sketch under *Settings*
and *Pins*: first the Mastermind settings (prefix `MM_`), then the Color
Memory Game settings (prefix `CM_`), then the settings both games share.

### Mastermind

| Constant | Default | Meaning |
|---|---|---|
| `MM_DEBUG_SHOW_SOLUTION` | `true` (test setting) | `true` = show the secret sequence in row 16 during the game (for testing). Set to `false` for normal play. |
| `MM_MAX_ATTEMPTS` | `1` (test setting) | Number of attempts, after which the game is lost. Allowed are **1–15** (row 16 is reserved for the debug solution). An invalid value produces a clear error message at compile time. Use e.g. `10` for normal play. |

### Color Memory Game

| Constant | Default | Meaning |
|---|---|---|
| `CM_MAX_LENGTH` | `20` | Maximum length of the sequence. Allowed are **1–20** (the grid has 5 rows × 4 positions), checked at compile time. After reaching it, every round has this length. |
| `CM_SHOW_PAUSE` | `300` ms | Dark pause before the sequence appears |
| `CM_SHOW_TIME_BASE` | `1000` ms | Display time of the sequence … |
| `CM_SHOW_TIME_PER_COLOR` | `500` ms | … plus this much per color |

### Shared

| Constant | Default | Meaning |
|---|---|---|
| `DEBUG` | `1` | `1` = serial output on (UIDs, solution, evaluation), `0` = off (saves a little memory) |
| `BRIGHTNESS` | `4` | Brightness of the matrix (0–255) |
| `BLINK_INTERVAL` | `250` ms | Blink rate of a complete input |
| `SCROLL_INTERVAL` | `70` ms | Speed of the scrolling texts (smaller = faster) |
| `END_IMAGE_DURATION` | `4000` ms | Dwell time of the end image before the scrolling text runs again |
| `RESULT_PAUSE` | `1500` ms | Leave the last evaluation on screen before the next screen (end screen resp. next round) appears |
| `DEBOUNCE_TIME` | `30` ms | Debounce of the buttons |
| `ABORT_HOLD_TIME` | `3000` ms | Hold button 2 / 3 this long to quit a running game |

The RFID timing values (`MAX_FAILED_READS`, `POLL_INTERVAL`,
`ANTENNA_WAIT`) come from the original reader sketch and are unchanged. If
chips are detected unreliably, increase `ANTENNA_WAIT` first.

### Range of the readers: gain and transmitter power

The four readers lie only about **3 mm** apart. In addition, some readers
do not lie completely flat, because the pins on their back lift them up on
one side. A tilted reader has a different distance to the chip than its
neighbors, and its field reaches further to one side. Both make it easy for
a chip to be detected by a neighboring reader as well.

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

How far the range actually drops depends on the module, its antenna and
how flat it lies, so the right combination has to be found by testing. For
this, both values can be changed at runtime via the Serial Monitor (`DEBUG 1`, 9600 baud),
without uploading the sketch again. This works in every state of both
games:

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

### Active readers

Only the readers the running game needs at the moment are read. In
Mastermind these are always all four. In the Color Memory Game they are only
the readers of the current block: in round 1 only reader 1, for a sequence
of 6 first readers 1–4, then readers 1–2. `activeReaderCount()` returns this
number; `pollReaders()` skips all readers above it.

An inactive reader therefore can never get a chip, neither by its own read
nor by the chip lock. This matters because an empty reader is exactly the
one that picks up the chip of its neighbor most easily. Example: the last
block has 2 colors, and the chip on reader 2 is also seen by reader 3.
Reader 3 is not read at all, so the chip only appears at reader 2 and
belongs to it immediately – no power test, no tie, no white LED.

If an inactive reader still shows a chip from the previous block, it is
forgotten as soon as the reader becomes inactive. A side effect of skipping
the inactive readers: a round with fewer readers is faster.

One limit remains: if a chip is placed on an unused reader while its
active neighbor is **empty**, the neighbor may pick it up and show its
color, because the unused reader cannot claim the chip any more. Chips on
unused readers should therefore simply be avoided.

### Chip lock

If gain and transmitter power alone do not prevent cross-reading, the chip
lock (`CHIP_LOCK = true`, default) makes sure that every chip is assigned
to exactly one reader. It is used by both games. Each polling round then
runs in three steps:

1. **Read:** every active reader reads *all* chips in its field (up to
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

Before a press of button 1 is accepted – a Mastermind attempt as well as a
Color Memory block – one more complete round runs with a fresh power test
(no remembered results). Only if nothing changes is the input accepted. A
wrong color can therefore never be confirmed.

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
(1 = Red, 2 = Green, 3 = Blue, 4 = Yellow). Both games use the same table.
To determine a chip's UID, leave `DEBUG` at `1`, open the Serial Monitor
(9600 baud), start a game and place the chip on a reader. Write the printed
hex UID into the matching slot in the table. At startup, `checkTable()`
automatically checks for empty slots, duplicate UIDs, wrong color
distribution and invalid color indices, and reports anomalies via serial.

---

## 5. Display on the matrix

The matrix is 16×16 pixels. Rows and positions are counted **from 1** in the
code (`setLED(row, position, color)`).

All texts on the matrix are in English, and the serial debug output is in
English as well.

### Mastermind

```
        Position →   3  4  5  6      10 11 12 13
Row 1                [ Input    ]    [ Feedback ]   ← Attempt 1
Row 2                [ Input    ]    [ Feedback ]   ← Attempt 2
  ...
Row 10               [ Input    ]    [ Feedback ]   ← Attempt 10
  ...
Row 16                               [ Solution ]   ← only with MM_DEBUG_SHOW_SOLUTION
```

- **Input** (columns 3–6): the four readers from left to right.
- **Feedback** (columns 10–13): green/yellow/red, see section 2.
- **Row 16** shows the secret sequence when `MM_DEBUG_SHOW_SOLUTION` is
  active.

### Color Memory Game

The sequence is placed on a grid of 5 rows × 4 positions. Sequence position
*n* (counted from 0 in the code) lies in grid row *n* / 4 at grid position
*n* % 4. Every grid row is one input block; its feedback lies directly
below it.

```
          Position →    2     6        11    15
Row 1
Row 2   Block 1         [1]   [2]      [3]   [4]     ← colors 1–4
Row 3                   [ feedback for colors 1–4 ]
Row 4
Row 5   Block 2         [5]   [6]      [7]   [8]
Row 6                   [ feedback for colors 5–8 ]
  ...
Row 14  Block 5         [17]  [18]     [19]  [20]
Row 15                  [ feedback for colors 17–20 ]
Row 16
```

- **Colors** (rows 2, 5, 8, 11, 14): during the display the sequence, then
  white placeholders. During the input the current row shows the chips
  live (white until the chips of the previous block were removed);
  confirmed blocks show the entered colors.
- **Feedback** (rows 3, 6, 9, 12, 15): green = correct, red = wrong, only
  after the complete sequence has been entered.
- The arrangement is set in `CM_GRID_POSITION[]`, `CM_INPUT_ROW[]` and
  `CM_FEEDBACK_ROW[]`.

### Orientation of the matrix

On our matrix the first LED (pixel 0) sits **top right**, the first row runs
from right to left, and each further row alternates direction (zigzag). In the
code this is set via `NEO_MATRIX_TOP + NEO_MATRIX_RIGHT + NEO_MATRIX_ROWS +
NEO_MATRIX_ZIGZAG`. The library uses this to convert the coordinates itself,
so that position 1 is on the left and row 1 at the top, and text scrolls
through the right way round. If the matrix is ever installed differently, only
this line needs to be adjusted.

### Start and end screens

- **Start screens:** colorful scrolling text “MASTERMIND - PRESS BUTTON”
  resp. “COLOR MEMORY GAME - PRESS BUTTON”.

All end screens alternate between a scrolling text and a still image until a
button is pressed:

| End screen | Scrolling text | Still image |
|---|---|---|
| Mastermind won | “YOU WIN!” in green | solution as four 2×2 color blocks, below it the **number of attempts** as a green number |
| Mastermind lost | “YOU LOSE” in red | solution as four 2×2 color blocks, below it a **red X** |
| Color Memory game over | “GAME OVER” in red | the **score** as a green number |

Numbers with 1–2 digits are drawn in the default font, bold. Numbers with
3–4 digits (a Color Memory score from 100 upwards) do not fit in this font
and are drawn in the small 3×5 font `TomThumb` (part of the Adafruit GFX
library). Values above 9999 are shown as 9999.

---

## 6. Structure of the code

The sketch is organized as a **state machine**. There is always exactly one
active state, and `loop()` calls the matching function depending on the
state:

```
  Mastermind                               Color Memory Game

  S_MM_START                               S_CM_START
      │ Button 1                               │ Button 1
      ▼                                        ▼
  S_MM_GAME ─ all green ──────► S_MM_WON   S_CM_SHOW ◄────────────────────────┐
      ▲  │                         │           │ display time over            │ all correct
      │  └─ MM_MAX_ATTEMPTS ► S_MM_LOST        ▼                              │ (next round)
      │                            │       S_CM_INPUT ── sequence entered ────┤
      └──────── Button 1 ◄─────────┘                                          │ error
                                                                              ▼
                                           S_CM_SHOW ◄── Button 1 ──── S_CM_GAME_OVER
                                           (new game, round 1)

  From every start and end screen:  Button 2 → S_MM_START,  Button 3 → S_CM_START
  From every running game state:    hold button 2 / 3 for 3 s → S_MM_START / S_CM_START
```

| State | Constant | Function in `loop()` |
|---|---|---|
| Mastermind start screen | `S_MM_START` | `loopStart()` |
| Mastermind running game | `S_MM_GAME` | `mmLoopGame()` |
| Mastermind win screen | `S_MM_WON` | `loopEnd()` |
| Mastermind lose screen | `S_MM_LOST` | `loopEnd()` |
| Color Memory start screen | `S_CM_START` | `loopStart()` |
| Color Memory sequence display | `S_CM_SHOW` | `cmLoopShow()` |
| Color Memory input | `S_CM_INPUT` | `cmLoopInput()` |
| Color Memory game over screen | `S_CM_GAME_OVER` | `loopEnd()` |

### Naming conventions

- Everything that belongs to only **one game** has a prefix: `mm` / `MM_`
  for Mastermind (e.g. `mmNewGame()`, `mmSecretCode`, `MM_MAX_ATTEMPTS`),
  `cm` / `CM_` for the Color Memory Game (e.g. `cmNewGame()`,
  `cmSequence`, `CM_MAX_LENGTH`). State constants: `S_MM_…`, `S_CM_…`;
  texts: `TEXT_MM_…`, `TEXT_CM_…`.
- Everything that **both games share** has no prefix: buttons, RFID, chip
  lock, matrix helpers, blinking, start/end screens.
- Constants in `UPPER_CASE`, variables and functions in `camelCase`, rows
  and positions on the matrix counted from 1.

### Sections of the sketch

The sketch is one file, in this order: *Settings*, *Pins*, *RFID timing*,
*Layout*, *Colors*, *Texts*, *Chip-color table*, *Objects*, *State*, then
the functions: *Buttons*, *LED matrix*, *RFID*, *Chip lock*, *Shared game
helpers*, *Mastermind*, *Color Memory Game*, *Screens*, *States*,
*Setup/Loop*.

### The most important functions

**Flow / states**

- `setup()` – initializes the matrix, the three button interrupts and the
  four readers (first all `PCD_Init()` calls, then `applyRfidSettings()` and
  antenna-off, so no later init resets these settings), and shows the
  Mastermind start screen.
- `loop()` – dispatches to the loop functions depending on `state`.
- `loopStart()` – shows the colorful start scrolling text of the selected
  game; button 1 calls `mmNewGame()` resp. `cmNewGame()`, button 2 / 3
  switch the game.
- `mmLoopGame()` – polls the readers, updates the display, lets the row
  blink and reacts to button 1.
- `cmLoopShow()` – dark pause, then shows the sequence for
  `cmShowDuration()`, then calls `cmStartInput()`. Non-blocking.
- `cmLoopInput()` – polls the readers, releases the next block once its
  readers are empty, lets the row blink and reacts to button 1.
- `loopEnd()` – alternates between the final scrolling text and the end
  image of the current end screen; button 1 starts a new game of the same
  game, button 2 / 3 switch the game.

**Game selection / screens**

- `selectGame()` – on start and end screens: button 2 → `S_MM_START`,
  button 3 → `S_CM_START`.
- `ignoreSelectButtons()` – in the running game: discards short presses of
  button 2 / 3.
- `checkAbort()` – called at the beginning of every running game state
  (`mmLoopGame()`, `cmLoopShow()`, `cmLoopInput()`). Calls
  `ignoreSelectButtons()` and quits the game if button 2 / 3 is held for
  `ABORT_HOLD_TIME`.
- `startStartScreen(startState)` / `startEndScreen(endState)` – enter a
  start resp. end screen and discard old button presses.
- `drawEndImage()` – calls `mmDrawEndImage()` or `cmDrawEndImage()`.

**Shared game helpers**

- `activeReaderCount()` – number of readers the running game needs at the
  moment (Mastermind 4, Color Memory the size of the current block). Only
  readers 1 to this number are read.
- `inputConfirmable()` / `drawInputRow()` – pass the work on to the game
  that is running (`mm…` or `cm…` function of the same name).
- `blinkTick()` – advances the blinking of a confirmable input row. Also
  called during the RFID polling so the blinking stays even.
- `restartBlink()` – starts the blinking visible and redraws the row (after
  every change).

**Mastermind**

- `mmNewGame()` – generates the secret sequence and resets everything.
- `mmConfirmAttempt()` – checks whether confirmation is allowed, evaluates
  the attempt, draws the result, increments `mmAttemptCount` and decides
  between win/lose/continue.
- `mmEvaluate(attempt, green, yellow)` – the actual Mastermind evaluation
  (green = color + position, yellow = color only). Handles duplicate colors
  correctly via two count fields.
- `mmInputComplete()` / `mmInputConfirmable()` – check whether all four
  readers are occupied resp. whether the input is also different from the
  last attempt. Only then does the row blink and button 1 get accepted.
- `mmDrawInputRow()`, `mmDrawFeedback(row, green, yellow)`,
  `mmDrawEndImage()` – display.

**Color Memory Game**

- `cmNewGame()` – resets length and score, calls `cmNextRound()`.
- `cmNextRound()` – makes the sequence one color longer (up to
  `CM_MAX_LENGTH`), generates it completely new and starts `S_CM_SHOW`.
- `cmStartInput()` – turns the grid places white and starts `S_CM_INPUT`.
- `cmBlockSize()` – number of readers needed for the current block (4, or
  fewer for the last block). This is also the number of active readers
  during the input.
- `cmInputComplete()` / `cmInputConfirmable()` – check whether all required
  readers carry a known chip resp. whether additionally the chips of the
  last block were removed (`cmWaitForEmpty`).
- `cmBlockReadersEmpty()` – are the required readers free of chips?
- `cmConfirmBlock()` – stores the block in `cmInput[]` (without evaluating
  it) and calls `cmEvaluate()` once the sequence is complete.
- `cmEvaluate()` – compares position by position, draws green/red below
  the colors, adds the points and decides between next round and game
  over.
- `cmSetGridLED(index, color)` / `cmSetFeedbackLED(index, color)` – set the
  LED of a sequence position resp. the feedback below it.
- `cmDrawInputRow()`, `cmDrawEndImage()` – display. `cmDrawInputRow()`
  draws the current row white as long as `cmWaitForEmpty` is set.

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
- `pollReaders(fresh)` – one complete round: read all active readers,
  assign the chips, update the display state. Inactive readers are skipped
  and forget their chip. Returns `true` if a displayed color has
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
- `scrollTextStep(text, color)` – draws **one** step of a scrolling text
  (non-blocking) and reports when the text has run through completely.
- `drawNumber(value, y, color)` – draws a centered number (1–2 digits bold,
  3–4 digits in the small font). Used by both end images.

**Buttons**

- `handleButtonEdge(b)` – shared logic of the interrupt routines. Detects a
  real press via edge + debounce and remembers the moment (`micros()`).
- `buttonConfirmISR()`, `buttonMastermindISR()`, `buttonColorMemoryISR()` –
  one interrupt routine per button (an ISR cannot take parameters); each
  only calls `handleButtonEdge()`.
- `fetchButton(b)` – hands a pending press of button `b` to the main
  program and resets the flag; also used to discard old presses.
- `discardButtons()` – discards the presses of all buttons.
- `buttonHeld(b)` – is button `b` still down, for at least
  `ABORT_HOLD_TIME` since its last press? The press time
  (`buttonPressMillis[]`) is stored by the interrupt; whether the button is
  still down is read directly from the pin. A release and a new press start
  a new hold, because the interrupt then stores a new press time.
- `cancelHolds()` – buttons held at this moment no longer count as held
  (`buttonHoldUsed[]`) until they are pressed again. Called when a game
  starts and after quitting.

### Why interrupts for the buttons?

When no chip is present, each reader waits for a timeout. A full polling round
can therefore take more than 100 ms – with the chip lock and all readers
occupied about 250–300 ms, during a power test even longer. A short click that
falls exactly within that time would be missed with a simple `digitalRead()`.
All three button pins are interrupt-capable, so every press is captured via an
interrupt and buffered safely – no button press is lost, guaranteed.

Holding a button does not need an extra interrupt: the interrupt already
stores the moment of the press, and `buttonHeld()` only reads the pin in
the main program. The debounce keeps the bouncing on release from counting
as a new press. A long polling round only delays the quitting by a moment;
the hold itself is measured from the press.

On the Mega the interrupt-capable pins are D2, D3, D18, D19, D20 and D21.
`attachInterrupt(digitalPinToInterrupt(pin), …)` translates the pin number
into the right interrupt number itself, so nothing has to be changed in the
code if a button is moved to another of these pins – only its entry in
`BUTTON_PINS[]`. The Arduino interrupt number and the interrupt of the
ATmega2560 itself are not the same:

| Button | Pin | Arduino interrupt | ATmega2560 interrupt |
|---|---|---|---|
| Button 1 | D2 | 0 | INT4 |
| Button 2 | D3 | 1 | INT5 |
| Button 3 | D18 | 5 | INT3 |

### Why is the Color Memory display a separate state?

With 20 colors the sequence stays on screen for 11 s. Instead of waiting
with `delay()`, `cmLoopShow()` measures the time with `millis()` – like the
end screens. `loop()` therefore keeps running: the Serial Monitor tuning
still works during the display, and button presses are buffered and then
deliberately discarded when the input starts. The short pauses after an
evaluation (`RESULT_PAUSE`) still use `delay()`, as before in Mastermind.

### Randomness and memory

- **Randomness:** The moment of the button press that starts a game
  (`micros()`) serves as the seed for the random number generator. This is
  practically unpredictable, so the sequences reliably differ from game to
  game.
- **Memory:** The Mega has 8 KB of RAM (the UNO only 2 KB), of which the
  matrix occupies 768 bytes. The chip table (`CHIPS[]`) and all texts still
  live in flash (`PROGMEM`) instead of in RAM. On the Mega this is no longer
  strictly necessary, but it costs nothing and leaves RAM free for later
  extensions. Already-played Mastermind attempts are not stored separately
  – they are on the matrix anyway. The
  Color Memory Game needs two arrays of `CM_MAX_LENGTH` bytes (sequence and
  input), because the input is only evaluated at the end.

### Memory usage (Arduino Mega 2560 / ATmega2560)

Measured with Arduino AVR core 1.8.6 and avr-gcc 7.3.0:

| Variant | Flash (of 248 KB) | RAM (incl. 768-byte matrix buffer at runtime) |
|---|---|---|
| `DEBUG 1` | 26 986 bytes ≈ 10 % | 1 669 bytes ≈ 20 % |
| `DEBUG 0` | 22 762 bytes ≈ 9 % | 1 612 bytes ≈ 20 % |

The RAM figure reported by the Arduino IDE is lower (901 resp. 844 bytes)
because it does not yet include the matrix buffer – that is only allocated
at runtime. Depending on the compiler version of the IDE, the flash values
can differ by a few hundred bytes (the Mastermind-only sketch measured
23 232 bytes with this compiler, 23 780 bytes in the IDE). With more than
6 KB of free RAM there is plenty of room for the stack and for extensions
(e.g. more readers, sound).

The chip lock alone needs about 3 KB of flash and about 100 bytes of RAM.
For comparison, the Mastermind-only sketch (`mastermind.ino`, measured in
the IDE):

| Variant | Flash (of 248 KB) | RAM (incl. 768-byte matrix buffer at runtime) |
|---|---|---|
| `DEBUG 1` | 23 780 bytes ≈ 9 % | 1 568 bytes ≈ 19 % |
| `DEBUG 0` | 20 530 bytes ≈ 8 % | 1 513 bytes ≈ 18 % |

Compared with the same compiler (`DEBUG 1`: 26 986 vs. 23 232 bytes), the
Color Memory Game and the game selection add about 3.7 KB of flash and about
100 bytes of RAM.

---

## 7. Wiring (Arduino Mega 2560)

The complete schematic is in `mastermind.kicad_sch`. In the sketch, the pins
are defined at the top under *Pins*.

![Schematic – Arduino Mega 2560 version](/mastermind_kicad/mastermind_schematic.png)

### Pin assignment

| Mega pin | Signal | Connected to | Note |
|---|---|---|---|
| D2 | `BTN1` | Button 1 (start / confirm) → GND | interrupt pin, `INPUT_PULLUP`, pressed = LOW |
| D3 | `BTN2` | Button 2 (select Mastermind) → GND | interrupt pin, `INPUT_PULLUP`, pressed = LOW |
| D5 | `LED_DIN` | LED matrix DATA/DIN | |
| D6 | `SS4` | Reader 4 SDA | |
| D7 | `SS3` | Reader 3 SDA | |
| D8 | `SS2` | Reader 2 SDA | |
| D9 | `RST` | RST of all 4 readers | shared |
| D10 | `SS1` | Reader 1 SDA | |
| D18 | `BTN3` | Button 3 (select Color Memory Game) → GND | interrupt pin, `INPUT_PULLUP`, pressed = LOW; TX1, free as long as `Serial1` is not used |
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
  matrix and connect its ground to the Mega's GND (common ground!). With a
  20-color sequence the Color Memory Game lights at most about 40 LEDs at
  once, fewer than a scrolling text.

### Libraries

MFRC522 (miguelbalboa), Adafruit GFX, Adafruit NeoMatrix and Adafruit
NeoPixel – all available in the Arduino Library Manager. The small font
`Fonts/TomThumb.h` is part of Adafruit GFX.

---

## 8. Changes compared to the separate sketches

For everyone who knows `mastermind.ino` or `Color-Memory-Game.ino`:

**Mastermind**

- The game logic is unchanged. Game-specific names got the prefix `mm` /
  `MM_` (e.g. `newGame()` → `mmNewGame()`, `MAX_ATTEMPTS` →
  `MM_MAX_ATTEMPTS`, `S_GAME` → `S_MM_GAME`).
- `fetchButton()` now takes the button number (`fetchButton(BUTTON_CONFIRM)`).
- The blinking (`blinkTick()`, `drawInputRow()`) is shared with the Color
  Memory Game; the repeated "blink on, redraw" lines are now
  `restartBlink()`.
- Drawing the number of attempts moved to `drawNumber()`.
- The end-image comment and this documentation now say 2×2 blocks – which
  is what the code always drew (the old documentation said 3×3).

**Both games**

- Button 2 (D3) and button 3 (D18) were only reserved in the Mastermind
  wiring and not used by the firmware. They now select the game and quit a
  running game. The wiring itself did not change.
- A running game can be quit by holding button 2 or 3 for 3 s (new; before
  there was no way out of a running game except a reset).

**Color Memory Game**

- Runs on the Mega wiring and uses the Mastermind RFID code including the
  chip lock instead of its own `readChip()` / `checkReader()`. Names follow
  the Mastermind conventions (e.g. `NUMBER_OF_READERS` → `READER_COUNT`,
  `getButtonPress()` → `fetchButton()`, `memorySequence` → `cmSequence`).
- Blocks are fixed to the readers (reader 1–4 = position 1–4); gaps are no
  longer skipped. A block must be complete (4 chips, the last block only the
  remaining ones) before it blinks and can be confirmed.
- Only the readers of the current block are read (`activeReaderCount()`).
  Unused readers can no longer take over the chip of a neighbor through
  cross-reading.
- The chips have to be removed between two blocks (was described in the game
  principle, but not checked in the code).
- The display time grows with the length (was fixed at 1.1 s).
- The sequence display is a non-blocking state (`S_CM_SHOW`); the unused
  `STATE_DISPLAY` is gone.
- Confirmed blocks stay visible; the feedback appears together with the
  entered colors.
- The feedback now lies **directly** below its row (rows 3, 6, 9, 12, 15).
  In the old code it was one row further down, next to the following input
  row, although the comment said "directly below".
- The score is displayed in the bold style of Mastermind resp. the small
  font from 100 upwards (a 3-digit number was cut off before), and for
  `END_IMAGE_DURATION` (4 s instead of 3 s).
- After round 20 the game continues endlessly with 20 colors (unchanged).
