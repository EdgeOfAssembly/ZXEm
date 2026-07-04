# Manic Miner (1983 Bug-Byte) - Game Logic

## Main Loop

The game runs entirely without ROM calls (no BEEP, no CLS). The main loop at 0x8700 executes each frame:

```
MainLoop:
  1. Draw air supply bar (0x8710)
  2. Draw tune notes if music enabled (0x8730)
  3. Copy level tile buffer 0x5E00 -> 0x5C00 (0x8740)
  4. Copy working buffer 0x7000 -> 0x6000 (0x8750)
  5. Update guardians (CALL 0x8D0F)
  6. Update Willy movement (CALL 0x923A)
  7. Check collisions (CALL 0x8AA5)
  8. Draw guardians to screen (CALL 0x8DAA)
  9. Draw Willy to screen (CALL 0x9105)
 10. Animate special items (CALL 0x8F63)
 11. Copy 0x6000 -> 0x4000 (screen flip) (0x87A0)
 12. Flash color cycling if active (0x87B0)
 13. Draw score/high score/air text (0x87D0)
 14. Scan keyboard (0x87F0)
 15. Play sound effects (0x8850)
 16. Check level completion (0x8FC0)
 17. Loop back
```

## Level Progression

1. Game starts at level 0 (title screen / attract mode)
2. Pressing ENTER or fire starts level 1 ("Central Cavern")
3. Each level requires collecting 5 flashing items
4. After collecting all 5 items, the exit portal opens
5. Walking into the portal advances to the next level
6. Level number stored at 0x8407 (0-19)
7. After level 19, game loops back to title screen (or shows ending)
8. Level data loaded from 0xB000 + (level * 1024)

## Collision Detection

### Willy vs Walls/Floors
- Willy's position (0x8068, 0x806C) is checked against the tile map at 0x5E00
- Each tile cell is 8x8 pixels; Willy is 16x16 pixels
- Four corner checks determine collision with solid tiles
- Tile IDs >= 0x0B are generally solid (walls, floors, platforms)
- Tile 0x00 is empty/air

### Willy vs Guardians
- Guardians are 16x16 sprites with positions tracked in the guardian table (0x80BE+)
- Collision check compares Willy's screen position with each guardian's position
- On collision: Willy dies (lives decremented, death animation plays)
- Death state: 0x806B set to 6, air resets, level restarts

### Willy vs Items
- 5 collectible items per level, positions defined in level data
- Items flash between two colors (animated by toggling attribute byte)
- When Willy's position overlaps an item, it's collected
- Item state tracked at 0x8075-0x807F
- Collecting all 5 items opens the exit portal

### Fall Damage
- Fall distance tracked at 0x806F
- If Willy falls more than a threshold distance, he dies on landing
- Safe fall: any distance onto a solid surface is OK in this version
- Fatal fall: falling off the bottom of the screen

## Scoring

- Score displayed at 0x8444 as 6 ASCII digits
- Points awarded for:
  - Collecting items (varies by level/item)
  - Completing a level (bonus based on remaining air)
  - Killing guardians? (not in this version - guardians are invincible)
- High score stored at 0x8434
- Score is updated by calling the score display routine at 0x87D0

## Lives System

- Lives stored at 0x8408
- Start with 3 lives
- Death: lives decremented, level restarts from beginning
- All items reset on death
- Game Over when lives reach 0
- Game Over screen shows final score, waits for keypress to restart

## Air Supply

- Air supply at 0x8457, displayed as "AIR" + digits at 0x8418
- Decrements each game tick (frame)
- When air reaches 0, Willy dies
- Air resets on level completion and on death
- Air bar drawn as a horizontal bar at the top of the screen

## Special Items (Collectibles)

- 5 items per level
- Each item is an 8x8 flashing sprite
- Item positions defined in level data (bytes 576-607 of level block)
- Items animate by cycling their attribute byte (INK color)
- When collected, the item's tile is replaced with blank (0x00)
- Item collection triggers a sound effect
- All 5 items must be collected to open the exit

## Keys and Doors / Exit Portal

- No traditional key/door system in this version
- Exit portal appears when all 5 items are collected
- Portal is a flashing tile at a fixed position per level
- Walking into the portal triggers level completion
- Level completion: brief animation, then next level loads

## Conveyor Belt Logic

- Conveyor tiles identified by tile ID (0x0E = conveyor left, 0x0F = conveyor right)
- When Willy stands on a conveyor tile:
  - Conveyor direction checked at 0x8C80
  - Willy's X position shifted by 1 pixel per frame in conveyor direction
  - Movement continues until Willy steps off the conveyor tile
- Conveyor state tracked at 0x8069 (movement counter)
- Conveyor movement overrides normal left/right input

## Rope Logic

- This version of Manic Miner (Bug-Byte 1983) does NOT have ropes
- Ropes were added in the later Software Projects version
- The code at 0x8D00-0x8D80 handles guardian vertical movement, not ropes

## Guardian (Nasty) AI

Guardians are defined in the ASCII table at 0x8000. Each guardian has:
- Sprite number (which 16x16 sprite frame to use)
- X position (column, 0-31)
- Y position (row, 0-15)
- Min X bound
- Max X bound
- Movement speed/direction
- Animation frame

Guardian types:
1. **Horizontal patrol** (type 0-2): Moves left/right between min/max bounds
2. **Vertical patrol** (type 3-4): Moves up/down between bounds
3. **Stationary** (type 7): Doesn't move, just animates
4. **Direction flags** in byte 0 of guardian entry:
   - Bit 7: active/inactive
   - Bit 2: vertical movement enabled
   - Bit 1: horizontal direction
   - Bit 0: vertical direction

Guardian update routine at 0x8D10:
- Iterates through guardian table (7 bytes per guardian)
- Updates position based on direction and speed
- Reverses direction at bounds
- Draws guardian sprite at new position

## Death and Game Over

1. Death causes:
   - Collision with guardian
   - Air supply reaches 0
   - Falling off bottom of screen
   - Touching a dangerous tile

2. Death sequence:
   - Willy's state set to 6 (dead)
   - Death animation plays (Willy spins/flashes)
   - Lives decremented
   - If lives > 0: level restarts
   - If lives = 0: Game Over screen

3. Game Over:
   - "Game Over" text displayed
   - Final score shown
   - Waits for keypress
   - Returns to title screen

## Music and Sound

- Music: "In the Hall of the Mountain King" (Grieg)
- Music data at 0x8500-0x85CB
- Toggle on/off with keys H-L during gameplay
- Sound effects: jump, item collect, death, level complete
- Sound output via OUT (0xFE),A toggling bit 4 (EAR/MIC bit)
- No ROM BEEP routine used - all sound is bit-banged

## Title Screen / Attract Mode

- Level 0 is the title screen
- Animated Willy walks across the screen
- Title text scrolls/animates
- Color cycling effects
- Waits for ENTER or fire button to start game
- Kempston joystick supported for start
