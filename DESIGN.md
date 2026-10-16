# GRIMLIGHT — The Hollow Bell (GBA)

A turn-based gothic puzzle-strategy game for Game Boy Advance, built from scratch in C
(direct hardware registers, Mode 0 tiled background + sprites, PSG music/SFX).
Playtest builds go to Cori first; nothing is published until she approves the exact ROM.

## Premise
On Halloween night the Hollow Bell of Grimlight Manor tolls for the dead.
You are the last lantern-keeper. Cross the manor, the crypt, and the belfry;
recover the stolen soul relics; ring the bell and lay the haunting to rest.

## Core loop (turn-based — every step you take, the haunting takes one too)
- Move with the D-pad (one tile = one turn). A = wait a turn. B = restart the room. START = pause.
- **Light is safety.** Your lantern lights a radius around you (bright → dim → dark → pitch).
  Candles and braziers are fixed/movable light sources. Your oil burns down as you walk;
  at empty, the radius shrinks (3 → 2 → 1). Oil flasks refill it.
- **Wraiths** drift toward you through the dark but cannot enter the light. Herd them with
  your lantern; a wraith caught with nowhere dark to flee dissolves (+100).
- **Skeletons** patrol straight lines, reversing at obstacles, light or no light. Time them.
- **Gloomweavers (spiders)** creep diagonally toward you every other turn, but freeze in light.
- **Pushable braziers**: slide them to move a safe zone, block a patrol, or hold a
  **pressure plate** that keeps a **gate** open.
- **Candle stands**: bump one to light/snuff it (costs a turn) — sometimes darkness is a tool.
- **Bell runes**: step on one to toll the bell — every enemy freezes for 3 turns.
- Collect every relic in the room to unseal the door, then escape. Death restarts the room
  (score penalty). Score: relics +150, dissolved wraiths +100, lit candles +25, room bonus
  for few moves.

## Presentation (GBA hardware)
- 15×10 grid of 16px tiles = full 240×160 screen; 4 brightness palette banks per tile give
  real light falloff; in pitch black only enemy eyes glow.
- Smooth gliding movement between tiles, walk-cycle sprites, flickering flame sprites,
  rising ember particles, palette fades, screen shake on bell tolls and deaths.
- Chapter palette shifts: Manor (violet stone), Crypt (moss & bone), Belfry (midnight bronze).
- PSG soundtrack: square-wave organ arpeggios over an Andalusian progression, wave-channel
  bell tolls, noise-channel wind; SFX for steps, pushes, chimes, stings.

## Levels (9)
Manor: 1 Candlelit Entry · 2 The Long Gallery · 3 Oil and Ashes
Crypt: 4 Brazier Puzzles · 5 The Plate and the Gate · 6 Weaver's Dark
Belfry: 7 Patrol of Bones · 8 Snuff the Lights · 9 The Hollow Bell

