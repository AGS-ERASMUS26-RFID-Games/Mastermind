# Mastermind

The Arduino picks a secret sequence of four colors: **Red, Green, Blue, Yellow**. Colors can repeat. Find the sequence!

## How to play

1. **Place chips:** Put a chip on each of the four readers. The current row shows their colors (white = no or unknown chip).
2. **Confirm:** When all four chips are recognized, the row blinks. Press **button 1** to confirm.
3. **Read the result:** Four LEDs to the right show how close you are:
   - 🟢 **Green** = right color, right position
   - 🟡 **Yellow** = right color, wrong position
   - ⚫ **Off** = no hit

   The LEDs are sorted (green first, then yellow), so their order does *not* tell you which position was right.
4. **Try again:** Your next guess goes into the next row.

Every chip in the secret sequence counts only once. With the secret **Red · Red · Green · Blue**, the guess **Red · Green · Green · Blue** gives 3 green and 0 yellow, because the second Green has no match left.

## Win or lose

- **Win:** All four result LEDs are green.
- **Lose:** You run out of attempts.

Button 1 starts a new game.
