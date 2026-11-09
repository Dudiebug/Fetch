# Human testing

In-game checks for Fetch. Each one stays PENDING until someone has run it in the game and sent the log.

## Fetch: style clones (docs/CLONE.md)

PENDING — HUMAN TESTING. None of this has run in the game under the name Fetch, so step 1 comes first.
Copy `dsound.dll` next to the game exe. On Proton, add `dsound=n,b` to `WINEDLLOVERRIDES`, beside any override you already have. On a
first start it writes `Fetch.ini` beside the exe. Run steps 1 to 15 and 17 to 21 with Fetch alone, then step 16.

1. `record=0`, `clone=0`, `menu_key=0`. Start the game and play a fight with sound. Expected: sound works, and `Fetch.log` says
   `nothing to do, nothing hooked`.
2. Put a `.hsmmodel` in `Clones/` and set `clone=<name>`. Expected: `Fetch.log` has `clone: <name>: model, <hours> h of play`
   and, about 20 s after the game starts, `fetch: started`. Send the log either way.
3. Start a Free Mode fight. Expected: after about 3 s a fighter appears 5 m ahead, facing you, and Free Mode's own fighters go.
   Log: `clone: in the fight`.
4. Stand still near it. Expected: it moves, turns towards you, guards and swings. It does not stand frozen or spin.
5. Fight it. Expected: its swings look like the recorded player's, sharp and not smeared, and it does not twitch between moves.
6. Compare awareness with replay: fight it with `brain=model`, then `brain=replay` with the `.hsmclone` beside it. Expected: the
   model keeps facing you and closes in at least as well. Say which felt more like the player.
7. Disarm it, then walk behind it. Report what it does. It has never been judged on these, and both were problems with replay.
8. Kill it. Expected: `clone: down`, the body stays about 8 s, and another comes about 3 s later.
9. Die. Expected: it lets go of its buttons and nothing crashes.
10. Watch the frame rate with the clone in. Expected: no regular stutter.
11. Set `duel=0`. Expected: Free Mode's fighters stay.
12. Set `record=1` and `clone=0`, then fight. Expected: log `clone: recording how this player fights`, and
    `Clones/<your name>.hsmclone` grows during the fight. Quit, play again: the same file grows. `tools/hsmclone.py info` reads it,
    and its kit is not 0 bytes.
13. Run `clonetrain Clones/<your name>.hsmclone` on that file, with the game closed and again with it running. Expected: it ends in
    under 5 minutes with a file of about 6 MB, and the game does not stutter while it runs. Fight the result: it wears your kit.
14. Fight a clone, then enter a Steam lobby. Expected: the clone goes, and the log says `fetch: in a Steam lobby`. No clone spawns
    while in it, and recording pauses. Leave the lobby: the log says `fetch: left the Steam lobby`, and a clone comes back.
15. Put a damaged or random `.hsmmodel` in `Clones/` and name it. Expected: `clone: the model file was refused: <why>`, no crash,
    and the recording is used if there is one.
16. Install any other mod beside it, including HSMMod. Expected: the game starts every time over several launches, both work, and a
    clone fights offline. Enter a lobby with a clone in: step 14 holds. Send `Fetch.log` either way.
17. Fight a clone trained on several weapons, with each weapon length. Expected: with a sword it swings like a sword, not like a
    poleaxe. Say which weapons still look wrong.
18. Fight it for 5 minutes. Expected: it rarely drops its weapon by itself, about as often as the player did. Count the drops.
19. With a version 1 `Clones/<your name>.hsmclone` there, set `record=1` and fight. Expected: the log says the old file was moved,
    `<your name>.hsmclone.old` holds it, and a new file grows.
20. Record a fight with armour on, then one without, with several weapons dropped nearby. Expected: `tools/hsmclone.py info` reads
    both and no stutter while recording. Send both files: whether armour and dropped weapons were read is checked from them.
21. Put a version 1 `.hsmmodel` in `Clones/` and name it. Expected: `clone: the model file was refused: <why>`, no crash.
22. Set `record=1` and `clone=0`, and fight with a forged sword, a forged mace, a forged hafted weapon and a forged polearm, in short
    and long forms. Expected: for each one, `Fetch.log` has `adapter: forged weapon <head> + <grip> is type <n>`, with 1 to 7 in the
    game's order: Swords 1H, Swords 2H, Maces 1H, Maces 2H, Hafted 1H, Hafted 2H, Polearms. Send the log either way: this read has not
    run in the game, and a `0` means the parts were not understood.
23. Fight on after step 22, landing hits on the head, an arm and a leg, and taking some. Expected: `tools/hsmclone.py info` reads
    `Clones/<your name>.hsmclone`. Send the file: whether types and hits were read is checked from it.
24. Train on that file and fight the clone with a forged sword, then an axe, then a polearm. Expected: it swings each as you did, and
    a type you never played looks like the nearest one you did.
25. With HSMMod removed (no `dwmapi.dll` beside the game), start the game from Steam with `record=1` and `clone=0`, and fight for a
    minute. Expected: `Fetch.log` has `fetch: Steam is up (started by Fetch)`, `Clones/` appears with your Steam name's `.hsmclone`, and
    the game still shows you as playing in Steam. Then set `clone=<your name>` and train: the clone joins an offline fight.
26. With HSMMod installed as well, start the game. Expected: `fetch: Steam is up (already started)`, and multiplayer lobbies still work.
    Join one: the clone leaves and recording pauses. Leave it: both come back.
27. Quit Steam, then start `HalfSwordUE5-Win64-Shipping.exe` directly. Expected: `fetch: Steam did not start` once, no crash, no clone
    and no recording.

### The menu (;) and fight events — PENDING — HUMAN TESTING

Fetch alone first. Leave `menu_key` out of `Fetch.ini`, or set `menu_key=;`. (P, pre6's key, is the game's pause key: the menu
could not open with it.)

28. `record=0`, `clone=0`. Start a Free Mode fight and press `;`. Expected: a FETCH panel in the middle of the screen with six rows,
    the first marked `>`. Your fighter does not move while it is open, and the game does not pause. `Fetch.log`: `fetch: menu: added
    to UI_HUD`, `fetch: menu open`. Press `;` again: it closes and your fighter answers the keys at once (no stuck run or guard).
29. Open the menu. W/S and the arrows move the `>`, and wrap at the ends. The hint at the bottom says `; or Esc: close`. Esc
    closes it, and the game's pause menu does not open with it. Enter on Close closes it. Send a screenshot.
30. On "Record my fighting", press D. Expected: ON, the log says `clone: recording on` then, in a fight, `clone: recording how this
    player fights, and the fight's events`. `Fetch.ini` now says `record=1`. Fight a minute, open the menu: the first status line
    counts the time kept and the MB. Turn it OFF: the log says `clone: recording off`, and `Fetch.ini` says `record=0`.
31. Record a fight in which you hit the foe with a weapon, punch, kick, parry, thrust, grab them, get grabbed, fall, drop and pick
    up a weapon, kill them and die once. Expected: the menu's hits, clashes, parries, damage, kills and deaths go up as you do
    them. Then `tools/hsmclone.py info Clones/<your name>.hsmclone` prints an `events:` line with each of those kinds. Send the file
    and the log: whether each event was read right (hand, body part) is checked from them.
32. With two clones in `Clones/`, open the menu, go to "Clone to fight" and press D. Expected: the next name, `Clone: loading...` under the
    rows for a moment, then the clone in the fight goes and the new one comes about 3 s later. Log: `fetch: clone now <name>`.
    Press D quickly several times: only the last one comes, and nothing stutters. Step to `none`: the clone goes and none comes.
33. Change "Clone brain" with a `.hsmmodel` and `.hsmclone` of one name: the clone line says `model` or `replay` to match.
34. "One on one" OFF: Free Mode's fighters stop going. "Bring the clone back": the clone goes, and a new one comes about 3 s later.
35. Open the menu, then let the foe kill you. Expected: the menu closes by itself and the death screen works as usual. Open it again
    after respawning, close it, and leave the level through the pause menu: the main menu takes the mouse as usual. In a Steam
    lobby, `;` does nothing.
36. Set `menu_key=F2`: F2 opens it, `;` does nothing, and the hint says `F2 or Esc: close`. Set `menu_key=0`: no key opens it. Set
    `menu_key=nonsense`: the log says it is not a key, and `;` opens it. Set `menu_key=P` (as pre6 wrote it): the log says
    `menu_key=P is the game's pause key; the menu is on ;`, `Fetch.ini` now says `menu_key=;`, and `;` opens it.
37. Play a long fight with recording on and the menu shut. Expected: no stutter that is not there with `record=0`.
38. With HSMMod installed too, open the Fetch menu in an offline fight, and HSMMod's own chat or menus. Expected: both work, and
    keys go back to the fighter when each closes.

### Training on fight events — PENDING — HUMAN TESTING

39. Train on a recording made with pre6 or later (`clonetrain Clones/<name>.hsmclone`). Expected: a line
    `events: N moments have them, M of those led to a hit landed or more`, with N about all the moments recorded since pre6.
    On a recording from before pre6: `no fight events in this recording (made before pre6)`.
40. Fight the new model. Expected: `Fetch.log` loads it as usual, and the clone answers the fight: it backs off, presses in or
    grabs after a blow the way you did when recording. Compare with the same recording trained with `--outcome 0` and on a pre6
    model: say which lands more hits on you.
41. A model trained before this version (`all.hsmmodel` of pre7) still loads and fights as before.
42. Put `train.bat` next to `clonetrain.exe` and double-click it. Expected: it lists every `.hsmclone` in `Clones/` with its size
    and whether it has a model. Pick one, press Enter at each question: it trains for up to 4.5 min, says `Done:` and the
    model's path, keeps the old model as `.hsmmodel.bak`, and goes back to the list. `A` trains them all; `Q` quits.

### Recording after a level change (pre9) — PENDING — HUMAN TESTING

43. With `record=1`, start in the tavern (or any level), then go to another fight level, e.g. Map_Arena_Yard. Expected: no
    crash; `Fetch.log` shows `adapter: bound N input events` and then `clone: recording how this player fights, and the fight's
    events`. pre8 crashed between those two lines.

### Replay leaves out what was not fighting (pre10) — PENDING — HUMAN TESTING

Set `brain=replay` and `clone=<name>` with a `.hsmclone` in `Clones/`.

44. Start an offline fight. Expected: after `clone: <name>: replay, ...`, a line
    `clone: it copies N% of the recording; left out: W% walking off a beaten foe, I% 3 s or more with no input`.
45. Get knocked down, or fall, while the clone is still standing. Expected: it comes in and strikes at you on the ground, or waits
    close by, as the player did to a downed foe. It does not stand still far off or run to the fence.
46. Stand up and keep fighting. Expected: it no longer looks down and swings at the ground as if you were lying there.
47. When the clone itself falls, expected: it gets up the way the player did, not with standing input.

### Replay feels pain, avoids what failed and turns to face (pre10) — PENDING — HUMAN TESTING

Set `brain=replay` and `clone=<name>` with a `.hsmclone` in `Clones/`.

48. Walk behind the clone, within arm's reach, and hit it. Expected: within about 1 s it stops what it is doing and turns
    to face you. It does not keep playing a move with its back to you.
49. Keep hitting it while it does one thing (a swing, a step). Expected: it changes to a different move within a second or two,
    and does not loop the same move over and over while you hit it.
50. Fight normally for a minute. Expected: it plays no worse than before; no stuck turning on the spot, no spinning while you
    stand in front of it.
51. Beat the clone. Expected: `Fetch.log` has
    `clone: down; this fight it turned to face you first N times, and was hurt playing M moves it then left alone`, with N
    above 0 if you stood behind it, and M above 0 if you hit it.

### Armour, strikes, training sessions and the clone's weapon (pre11) — PENDING — HUMAN TESTING

Fetch alone first, `record=1`. Send `Fetch.log` and `Clones/<your name>.hsmclone` after each group of steps.

52. Start a Free Mode fight in some armour, against a foe in armour. Expected: within a few seconds `Fetch.log` has
    `clone: armour: me helm=... head=... (N pieces)` and the same for `foe`, with the parts you can see on each. Take off a piece or
    let one be knocked off: a new `clone: armour:` line. Say any part that reads wrong (for example mail read as plate, an open helmet
    read as visored). Each kind of piece is logged once as `adapter: armour <class> slot=... -> ...`.
53. Fight a foe in plate with a visored helmet, then one with bare arms. Hit bare skin, armour, and try for gaps (armpit, visor slit).
    Expected: one `clone: strikes: watching ...` line at the start, then `clone: strike: ... -> bare|gap|beat|held` per blow. The
    menu's `Strikes:` line goes up to match. Say whether each label matches what you saw: a blow that clearly bounced should read
    `held`, one into bare skin `bare`. `tools/hsmclone.py info` prints a `your strikes:` line.
54. With modded armour or weapons installed (any you use), repeat 52 and 53. Expected: no crash; the modded pieces get an
    `adapter: armour` line, and the parts they cover read sensibly. Send the lines for any that read wrong.
55. Fight a clone whose recorded kit uses a mod you then remove. Expected: no crash; the clone wears your kit instead.
56. Open the menu, choose "Training session" (full tour) and "Session length" 10 min, and press Enter on it. Expected: the menu
    closes, any clone leaves, a banner at the top centre says `Get ready, Stage 1 of 8: ... vs ..., 1:15 left (starts in 5)`. Within
    the countdown you are handed a weapon of that type (log: `drill: weapon given: type N tier 4 as <class> (reads as type N)`), and
    after it one foe comes 5 m ahead and fights you (log: `drill: foe spawned: tier T, budget B`). Kill it: another comes 3 s later.
    Free Mode's own fighters stay gone. Send a screenshot of the banner. Report any `drill: weapon not given: ...` line.
57. Let the tour run to the end. Expected: a new weapon and kind of foe each stage, a `drill: stage i/n over: ...` line per stage, then
    the banner shows the summary for about 20 s and the log has `drill: done (done), ...`. The foes of the plate stages should mostly
    wear more than those of the bare stages: say if not. `tools/hsmclone.py info` prints `training stages: 8`. If `record=0` was set
    before the session, recording is off again afterwards and the clone comes back.
58. Start a session, then press Enter on the row again: it stops at once with a summary. Start one and join a Steam lobby, or leave
    the fight through the pause menu: it ends (`drill: done (left the fight)`). Die during a stage: on respawn you get the stage's
    weapon again. Start one outside Free Mode (the tavern): the training line says `only in a Free Mode fight`.
59. Run a second session with "fill gaps". Expected: `drill: plan:` lists mostly the pairs the first session and your play had least of.
60. Set `clone_weapon=random` and fight a clone trained on several weapon types. Expected: `clone: has moves for ...` when it loads,
    and each time a clone comes in, `clone: weapon: <type> (random)` and it holds a weapon of that type. Then `clone_weapon=match`: it
    takes your weapon's type, or keeps its kit with a log line if it has no moves for it. Change it in the menu row "Clone's weapon":
    the next clone uses the new setting, and `Fetch.ini` holds it.
61. Train on a recording made with pre11 (after 52 to 57). Expected: a model of version 5 that loads and fights. Fight a clone of
    someone who fights plate and bare foes differently: say whether the clone does too. A model trained with pre8 or pre9 (version 4) and `all.hsmmodel` (version 3)
    still load and fight as before.
