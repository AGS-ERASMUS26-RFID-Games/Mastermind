# Color Memory Game

The player has to remember a random color sequence and rebuild it with the
chips. The sequence gets one color longer with every successful round.

## Course of a round

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
   last block are still lying there, the next row shows them but does not
   blink, and button 1 is ignored. This makes it possible to use the same
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

## Evaluation – an example

Generated sequence: **Red · Blue · Yellow · Green**

| Position | Sequence | Input | Result |
|---|---|---|---|
| 1 | Red | Red | Green LED, +1 point |
| 2 | Blue | Blue | Green LED, +1 point |
| 3 | Yellow | Green | Red LED |
| 4 | Green | Green | Green LED, +1 point |

Position 3 is wrong, so the game ends after this evaluation.

## Scoring

The score is the number of correct positions **over all rounds of a game**.
Correct positions of the last, failed round count as well.

Example: rounds 1 to 3 without a mistake, then 2 of 4 positions correct in
round 4 → score = 1 + 2 + 3 + 2 = **8**.

## Game over

The end screen alternates between the scrolling text “GAME OVER” in red and
the score as a green number (section 5). Button 1 starts a new game in
round 1 with score 0; button 2 or 3 switches the game.