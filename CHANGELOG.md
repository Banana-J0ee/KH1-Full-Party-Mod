# Changelog

## 1.0.2-epic

Port of 1.0.2 to the Epic Games Store release. Same features and fixes as
1.0.2; only the game addresses differ (see "The Epic port" in the README).

## 1.0.2

Fixes
- Crash when a summon ends with the extra member in the party. Big summons
  overflow their model memory into the spot where the extra member's model
  was kept. The extra member's model now has its own memory, away from where
  summons load.
- Changing Donald's or Goofy's weapon while they are the extra member now
  swaps the 3D model right away (only the stats changed before).
- The extra member followed Donald's party commands (attack / regroup): one
  press ordered both. Each press now orders exactly one ally.
- The extra member only ever healed Sora, and Donald and Goofy never healed
  the extra member. The game's own heal rules are unchanged (itself and Sora
  first, same HP/MP thresholds); "the other friend" they consider is now the
  more hurt of the two other party members.

Robustness
- If any patch does not match the game, everything already applied is undone
  and the game runs unmodified.
- Game builds with the same code at other addresses are detected
  automatically.
- `kh1party_debug.txt` / `kh1party_off.txt` also work from the game folder.
- Source code included; the leftover DINPUT8 proxy exports are gone.

## 1.0.0

First release.
