# Color Memory Game – Game Principle

## 1. Objective of the Game

The **Color Memory Game** is a memory game based on RFID technology and an LED matrix.

The player has to remember a randomly generated sequence of colors. The colors are displayed using a 16×16 LED matrix. The player then has to reproduce the sequence by placing RFID chips with the corresponding colors on four RFID readers.

The sequence becomes longer with every successful round.

---

## 2. Starting the Game

When the Arduino is switched on, the game displays a start screen.

The LED matrix shows the message:

**"COLOR MEMORY GAME - PRESS BUTTON"**

The player starts the game by pressing the button.

After the button is pressed, the first round begins.

---

## 3. Generating the Memory Sequence

At the beginning of every round, the Arduino generates a **completely new random sequence**.

The sequence consists of four possible colors:

* Red
* Green
* Blue
* Yellow

The length of the sequence depends on the current round.

For example:

* Round 1 → 1 color
* Round 2 → 2 colors
* Round 3 → 3 colors
* Round 4 → 4 colors
* Round 5 → 5 colors
* Round 6 → 6 colors
* etc.

The maximum sequence length is **20 colors**.

The sequence is newly generated for every round. The previous sequence is therefore **not extended**.

---

## 4. Displaying the Sequence

After generating the sequence, the LED matrix displays all colors of the sequence.

The colors are displayed **simultaneously** so that the player can memorize their positions.

For example, a sequence could look like:

**Red – Blue – Yellow – Green**

The player has to memorize this sequence before entering it.

The display remains visible for a certain amount of time before the input phase begins.

---

## 5. Entering the Sequence

After the sequence has been displayed, the player has to reproduce it using the RFID chips.

There are four RFID readers.

Each reader can detect one RFID chip at a time.

The player places the colored chips on the readers to enter the sequence.

For example:

**Red → Blue → Yellow → Green**

The system detects the color of every chip by reading its RFID UID.

The UID is assigned to a specific color in the program.

---

## 6. Entering More Than Four Colors

Because there are only four RFID readers, the player can enter a maximum of four colors at the same time.

However, the memory sequence can contain more than four colors.

For example, if the sequence has eight colors:

**Red – Blue – Green – Yellow – Yellow – Red – Blue – Green**

the player enters the sequence in two blocks:

**Block 1:**
Red – Blue – Green – Yellow

**Block 2:**
Yellow – Red – Blue – Green

The player can therefore enter sequences of up to 20 colors even though there are only four RFID readers.

---

## 7. Reusing the Same RFID Chips

The same RFID chip can be used multiple times in one sequence.

For example:

**Red – Blue – Red – Yellow – Red**

is a valid sequence.

The player must remove the chips from the readers before using the same chips again.

This allows the same color to appear multiple times in a sequence.

---

## 8. Evaluating the Input

The entered sequence is **not evaluated after every four colors**.

Instead, all input blocks are stored first.

Only when the complete sequence has been entered does the Arduino compare the player's input with the generated sequence.

This is important for longer sequences because the player can enter the complete sequence without immediately receiving feedback after every block.

---

## 9. Correct and Incorrect Colors

The Arduino compares the player's sequence with the generated sequence **position by position**.

For every position, there are two possible results:

### Correct

If the entered color is exactly the same as the generated color at the same position, the position is correct.

The LED matrix displays this position in **green**.

The player receives **one point** for every correct position.

### Incorrect

If the entered color does not match the generated color at the same position, the position is incorrect.

The LED matrix displays this position in **red**.

An incorrect position causes the game to end.

---

## 10. Example of an Evaluation

Generated sequence:

**Red – Blue – Yellow – Green**

Player input:

**Red – Blue – Green – Green**

The result is:

| Position | Correct Sequence | Player Input | Result       |
| -------- | ---------------- | ------------ | ------------ |
| 1        | Red              | Red          | 🟢 Correct   |
| 2        | Blue             | Blue         | 🟢 Correct   |
| 3        | Yellow           | Green        | 🔴 Incorrect |
| 4        | Green            | Green        | 🟢 Correct   |

The LED matrix therefore displays green for the correct positions and red for the incorrect position.

Because one position is incorrect, the game ends.

---

## 11. Scoring

The player receives **one point for every correctly entered position**.

For example:

**Sequence length: 8**

**Correct positions: 7**

**Score: 7 points**

The score is displayed after the evaluation.

---

## 12. Successful Round

If the entire sequence is correct, the player successfully completes the round.

The game then starts the next round.

The next sequence contains **one additional color**.

For example:

**Round 3 → 3 colors**

After successfully completing it:

**Round 4 → 4 colors**

The difficulty therefore increases with every successful round.

---

## 13. Game Over

If at least one color is entered incorrectly, the game ends.

The LED matrix displays:

**"GAME OVER"**

as a scrolling message.

Afterwards, the player's score is displayed.

The player can then press the button to start a new game.

---

## 14. New Game

When a new game is started:

* The score is reset to 0.
* The sequence length is reset to 1.
* A completely new random sequence is generated.
* The player starts again at Round 1.

The previous game's sequence is not reused.

---

## 15. Complete Game Flow

The complete game process can be summarized as follows:

**Start Screen**
↓
**Press Button**
↓
**Generate Random Sequence**
↓
**Display Sequence**
↓
**Player Enters Sequence**
↓
**Store Input Blocks**
↓
**Complete Sequence Entered**
↓
**Compare Input with Generated Sequence**
↓
**Correct?**

### Yes:

**Display Green Feedback**
↓
**Add Points**
↓
**Next Round**
↓
**Generate New, Longer Sequence**

### No:

**Display Red Feedback**
↓
**Game Over**
↓
**Display Score**
↓
**Press Button**
↓
**New Game**

---

## 16. Purpose of the Game

The main purpose of the Color Memory Game is to combine **memory training with RFID and LED technology**.

The project demonstrates several technical concepts:

* RFID communication
* Reading RFID UIDs
* Assigning RFID tags to colors
* Using multiple RFID readers
* Controlling an LED matrix
* Generating random sequences
* Storing user input
* Comparing arrays and sequences
* Managing different program states
* Creating an interactive game using Arduino

The increasing sequence length makes the game progressively more challenging and tests the player's ability to remember and reproduce longer color sequences.
