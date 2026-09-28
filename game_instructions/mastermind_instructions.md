# 1. Mastermind

At the start of a game, the Arduino rolls a secret sequence of four colors.
The choices are **Red, Green, Blue and Yellow**; each color may appear more
than once. The player tries to guess this sequence.

## Course of a round

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
   - **Off** = no hit for this color

   The evaluation is **not position-based**: all green LEDs light up first,
   then all yellow ones. So the order does *not* reveal which position is
   correct – exactly like real Mastermind.
4. **Continue:** The next attempt is entered in the row below.

## Winning and losing

- **Won:** All four evaluation LEDs are green (color *and* position correct
  everywhere).
- **Lost:** The maximum number of attempts (`MM_MAX_ATTEMPTS`) is reached
  without the sequence being guessed.

In both cases a final screen appears (see section 5), after which a new game
starts at the press of button 1.

## Evaluation – an example

Secret sequence: **Red · Red · Green · Blue**

| Input | Green | Yellow | Explanation |
|---|---|---|---|
| Red · Green · Green · Blue | 3 | 0 | Positions 1, 3, 4 match; the second green is one too many |
| Red · Blue · Blue · Red | 1 | 1 | Position 1 matches (green); another Red exists in the sequence but is misplaced (yellow) |
| Yellow · Yellow · Yellow · Yellow | 0 | 0 | Yellow does not appear in the sequence at all |

Important is the rule for duplicate colors: Each chip of the secret sequence
can produce only **one** hit. A second Red in the input only turns yellow if
the secret sequence also contains a second (not yet matched) Red.