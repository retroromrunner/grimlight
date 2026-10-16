# GRIMLIGHT: The Hollow Bell

**A turn-based gothic puzzle of light and shadow for Game Boy Advance.**

![Title screen](media/title.png)

On Halloween night, the Hollow Bell of Grimlight Manor tolls for the dead — and something has stolen the manor's soul relics. You are the last lantern-keeper. Cross the manor, the crypt, and the belfry, then descend to the Deep Bell far below — recover every relic, ring the bell, and lay the haunting to rest.

![Gameplay — Candlelit Entry](media/gameplay-manor.png)

## How it plays

Every step you take, the haunting takes one too.

- **Your lantern is your shield — and your life.** Wraiths drift toward you through the dark but cannot enter the light — and cannot be destroyed. Herd them out of your path, or put them to work: a wraith standing on a pressure plate holds it down just as a brazier does. But your oil burns down with every step: as it empties your light shrinks, and at zero the lantern dies and the room restarts.
- **Skeletons** patrol their routes, light or no light. Time them.
- **Gloomweavers** creep diagonally through the dark every turn. Only bright light freezes them — at the edge of your glow they keep coming, eyes first.
- **Push braziers** to move your safe ground, block a patrol, or hold pressure plates — some gates need two plates held at once.
- **Flip lever switches** to swap paired gates, light every candle to unseal a candle-locked door, and toll bell runes to freeze every enemy where it stands — or to break the web sealing a door.
- One vault is sealed by **four seal levers** that only turn in one true order. A wrong turn resets them all.
- Collect every relic in the room to unseal the door — then escape.

14 rooms across 4 chapters: the Manor, the Crypt, the Belfry, and the Deep Bell.

![Chapter intro](media/intro-manor.png)

![The Belfry](media/gameplay-belfry.png)

![Chapter Four — The Airlock](media/gameplay-airlock.png)

![The Deep Bell](media/gameplay-deepbell.png)

## Controls

- **D-pad** — move (one tile = one turn)
- **A** — wait a turn
- **B** — restart the room
- **START** — pause

## Play it

Download `grimlight-v13.gba` from the Releases page and open it in any Game Boy Advance emulator (mGBA recommended). Real hardware works too, via flash cart.

## Built with

Hand-written C direct to the GBA hardware — tiled Mode 0 backgrounds, sprite walk cycles, flickering flames and rising embers, a four-step light-falloff palette, and a PSG soundtrack of organ arpeggios, bell tolls, and wind. Every room is machine-verified solvable: a solver plays all 14 rooms end to end against the real game code before a build ships.
