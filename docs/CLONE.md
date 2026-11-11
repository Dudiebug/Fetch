# Style clones

Fetch, named after the double of a living person in Irish folklore, lets a player record how they fight, train a small model from it, share one file, and let anyone
fight that style offline. It has nothing to do with the Adaptive Rival mod and uses none of its code, data or design. It is a mod of
its own: it needs no other mod, and does not care what else is installed. With HSMMod, the multiplayer mod, it records your play in
its lobbies too, and with HSMMod 0.8.0-pre44 or newer the two share a small link so that the Fetch menu works there
([Playing with HSMMod](#playing-with-hsmmod)).

The in-game checks in `docs/HUMAN_TESTING.md` are still to be done for this version.

## Install

Copy `dsound.dll` next to the game exe (`HalfswordUE5/Binaries/Win64`). On Proton, add it to the launch options, beside any override
you already have:

```
WINEDLLOVERRIDES="dsound=n,b" %command%
```

The first start writes `Fetch.ini` next to the exe and logs to `Fetch.log`. This mod used to be called DarkSword: a `DarkSword.ini` is ignored, so copy any settings across. With `record=0`, `clone=0` and `menu_key=0` it hooks nothing.

| Key under `[fetch]` | Meaning |
|---|---|
| `record=1` | Writes how you fight to `Clones/<name>.hsmclone`, about 22 MB an hour. Later sessions add to the same file. |
| `record_clones=1` | Your fight against a clone (yours or anyone's) is recorded too, as against any foe. `0`: nothing is kept while a clone is in, as before pre13. |
| `record_online=1` | Your own play in a Steam lobby (a multiplayer fight) is recorded too (pre14 on). `0`: nothing is kept in a lobby, as before pre14. |
| `name=` | The file's name. Empty: your Steam name. The menu's "Record to" sets it, and "Rename this recording" changes it. |
| `clone=<name>` | Offline, the clone called `<name>` in `Clones/` joins each fight level, 5 m ahead of you. `1`: the first one there. `0`: none. |
| `duel=0` | Keeps Free Mode's own fighters in the fight. By default they are removed while a clone is in. |
| `brain=auto` | `auto` uses the `.hsmmodel` if there is one, else the `.hsmclone`. `model` or `replay` prefer that one. |
| `menu_key=;` | The key that opens the Fetch menu in game: a letter, a digit 1 to 9, F1 to F12 or `;`. `0`: no menu. P (the game's pause key) and W, A, S, D (the menu's own keys) are refused, and `;` is used and written back. |
| `strikes=1` | Reads how each blow met skin or armour (below). `0` turns it off. It only watches the game's damage function and changes nothing. |
| `parries=1` | Reads each parry (below). `0` turns it off. It only watches the game's parry function and changes nothing. |
| `clone_weapon=kit` | What the clone holds when it comes in. `kit`: what its recording held. `random`: a weapon type it has moves for, new each time. `match`: your weapon's type, if it has moves for it. Or one type: `sword1h`, `sword2h`, `mace1h`, `mace2h`, `axe1h`, `axe2h`, `polearm`. |
| `drill=tour` | The kind of training session (below): `tour` or `gaps`. |
| `drill_minutes=20` | A training session's length, 5 to 60 minutes. |
| `show_mind=0` | `1` shows the clone thinking while it fights (below). The menu's "Show the clone thinking" and the `=` key set it. |
| `mind_key==` | The key that shows or hides it in game: `=` by default, or a letter, a digit, F1 to F12 or `;`. `0`: no key. |

## The menu (;)

In a fight, press `;` (the key right of L on a US keyboard). A panel opens in the middle of the screen, and while it is open your keys go to it, not to your fighter.

| Row | Left / right, or Enter |
|---|---|
| Record my fighting | Turns recording on or off. Off writes what was kept first. |
| Record to | The recording your play goes into: your own (your Steam name), each recording in `Clones/`, or New... Enter on New... types a name (below). The next fight goes into the one chosen. |
| Rename this recording | Enter types a new name for the recording chosen in Record to (below). |
| Clone to fight | Steps through the clones in `Clones/` (and none). The new one is read in the background, then replaces the one in the fight. |
| Clone brain | `auto`, `model` or `replay`, as `brain=`. The clone is read again. |
| One on one | As `duel=`. |
| Bring the clone back | Enter: the clone leaves the fight, and a new one comes about 3 s later. |
| Clone's weapon | As `clone_weapon=`: its own kit, random, the same as mine, or one weapon type. It takes effect when the next clone comes in. |
| Training session | Left / right: full tour or fill gaps. Enter starts a session, or stops the one running. |
| Session length | 5 to 60 minutes, in steps of 5. |
| Merge recordings | Enter opens a page that merges recordings into one new clone (below). |
| Show the clone thinking | As `show_mind=`: a panel on the left that shows how the clone picks its moves (below). `=` does the same outside the menu. |
| Close | Enter closes the menu. |

W/S or the arrow keys choose a row, A/D or the arrows change it, Enter or Space changes it forwards or runs it, and `;` or Esc closes
the menu. Every change is written to `Fetch.ini` at once, so it holds next time. Under the rows the menu shows how long recording
has kept play and the file's size, the fight's tally since the game started (hits landed and taken, clashes, parries made and taken, damage dealt
and taken, kills, deaths), how your blows met the foe's armour, what the clone is doing, and the training session. The menu does not
pause the game.

The menu opens with your fighter alive and the game's HUD on screen: not in the main menu, not over the pause menu. In a Steam lobby
it opens only with HSMMod 0.8.0-pre44 or newer in the game, and not while HSMMod's chat is open. There the rows for the clone and for
training (Clone to fight, Clone brain, One on one, Bring the clone back, Clone's weapon, Training session, Session length, Show the
clone thinking) read "offline only" and do not change: clones stay offline. It closes by itself when you die, when the HUD goes (a
level change), when HSMMod's chat opens, or in a lobby without that HSMMod.

### Naming your recordings

Each recording is a file of its own in `Clones/`, so you can keep several: one per weapon, say, or one for a friend who plays on
your machine. In the menu, "Record to" chooses which one your play goes into, and New... makes one. While you type a name the keys
are letters: W, A, S and D type, and nothing moves. Letters, digits, spaces, `-` and `_` go in, up to 32 of them. Backspace erases,
Enter keeps the name, and Esc leaves things as they were. A space is `_` in the file's name. The file is made by the first fight
recorded into it, and a training session records into it too. The choice is `name=` in `Fetch.ini`.

"Rename this recording" renames the one chosen: its file, its model (`.hsmmodel`) if it has one, and the name inside it, which is
the name the clone shows. The new name goes on the end of the file, so every Fetch, old or new, reads the new name and the rest as
before. It does not rename onto another recording, or in the middle of a recorded fight. If "Clone to fight" was that recording, it
follows. Names Windows keeps for itself (`CON`, `NUL`, `COM1` and the like) are refused.

### Merging clones

Several recordings can be merged into one new clone: yours and a friend's, say, or one per weapon. The clone then picks its moves
from all of them. The recordings themselves are not changed.

- **In the menu:** "Merge recordings", Enter. The page lists the recordings in `Clones/`. Enter (or A/D) picks one, or unpicks it;
  the number beside it is its place in the merge. Then "Name of the new clone", Enter, type a name as above, Enter, and "Make it".
  The status line shows how far it has got ("Merging: reading 2 of 3..."), then "Made Sword_Duels: 1:12:05 of play from 3
  recordings". The new clone is in "Clone to fight" at once. Esc goes back to the first page. A recording being recorded into can
  be picked: what it has kept so far goes in.
- **On the desktop:** `train.bat`, then `M`, the numbers of the recordings (`1 3 4`) and a name. It offers to train the new clone
  straight after. Or drop two recordings or more on `merge.bat`, all at once, and type a name: the new clone goes beside the first.
  Both run `clonetrain --merge OUT.hsmclone NAME IN.hsmclone IN.hsmclone...`.

The order picked is the merge order. The clone looks like the first one picked that has a look saved (its last kit). Each player's
mouse sensitivity, and what they did, stay their own: `tools/hsmclone.py info` lists whose play is in it ("play of: Ana (3 plays,
0.80 h), Bo (1 play, 0.20 h)"). A merged clone can be merged again, and recorded into: the new play is the new clone's own.

A merged clone may be up to three quarters of the largest a recording can be, so it can still be recorded into. If the picked ones
are bigger than that together, the ones that fit (in the order picked) are merged and the status line says so. A recording that
ends in a broken block (the game closed mid-write) goes in up to that block, as Fetch would read it. A name that is taken is refused,
and nothing is overwritten: the file is written as `NAME.hsmclone.part` and takes its name only when it is whole.

A merged clone is an ordinary recording, of block kinds every Fetch knows: a Name block before each player's play, and the new name
and look at the end. A Fetch before pre12 reads it whole, with all its play, under the new name; it reads it as one player, so it
guesses one mouse sensitivity for the play that keeps none (pre12 keeps it in the file, so this is only for older recordings merged).

### Watching the clone think

Press `=` in a fight (or "Show the clone thinking" in the menu, or `show_mind=1`) to put a panel on the left of the screen while a clone fights. It is off by
default, shows only offline, and hides while the menu is open. `=` again hides it, and the choice is kept in `Fetch.ini`. It is drawn five times a second.

- **Now playing:** the move the clone is on (the keys it holds and the weapon type), whose recording it came from, how far into it
  the clone is, and why it plays it: *best match* (the moment that fit best), *kept playing* (the move it was on still fit best,
  so it went on) or *turning to face* (the foe is off to the side or behind, and the clone turns to it first).
- **Match map:** left to right is which way the foe is, from 180 degrees to the left to 180 to the right, with straight ahead in
  the middle; up is how far, to 6 m (further is drawn at the top). The white square is the fight now. The dots are where the foe was
  in the moments the clone weighed, at most eight, the one played ringed. A merged clone colours each player's moments its own
  colour, and names them above the map.
- **Cost bars:** what each of those moments cost, cheapest first, drawn against the dearest. Shorter fits better. A mark says what
  changed a cost: `75% stay` (the move playing goes on cheaper), `x2 again` (picked a moment ago), `x4 hurt` (the clone was hurt
  playing it).
- **A trained model** (`.hsmmodel`) has no moments to weigh: the panel shows the kinds of move it may play next and how likely each
  is, the one played marked, in place of the map and the bars.

The panel costs a little time each time the clone looks (it keeps the eight cheapest moments, not only the cheapest). Every half
minute the log says what a look takes: `clone: 300 looks in 30 s, 0.40 ms each on average, 1.10 ms at most (the clone-thinking
overlay on)`. With the panel off the clone picks exactly as before; with it on it picks the same moments too.

## Record, share, fight

**The main, most tested way to use Fetch is without training:** record, then fight the recording. The clone replays real moments of
the recorded player's input, the ones that best fit the fight it is in.

1. **Record.** Set `record=1` (or "Record my fighting" in the menu) and fight. Only play near a living foe is kept. Your play in a
   Steam lobby is kept too (pre14 on; `record_online=0` turns it off; [Playing with HSMMod](#playing-with-hsmmod)). Fighting a clone, your own included, is recorded like any fight (pre13 on; `record_clones=0` turns it
   off): it is your play, and the clone is the foe. The clone's own keys are never taken for yours.
2. **Share.** Send the `.hsmclone` from `Clones/`. It grows with play, about 22 MB an hour. It holds the name, the last kit fought in,
   and the recording. The mod uploads nothing.
3. **Fight.** Put the file in `Clones/`, set `clone=Ana` (or pick it in the menu's "Clone to fight"), and start an offline fight.
   If a `.hsmmodel` of the same name is there too, `brain=auto` uses the model: set `brain=replay` to stay with the recording.

### Optional: train a model

A model has been tested less in game than replay. It plays the same kind of real clips, but a small neural network picks them, and
the file is about 6 MB however long you played, so it is handier to share. It holds the model, the name, the last kit fought in, and
a bank of short real clips of your input.

Run the trainer on the file. It needs no GPU, no network and no installs. It runs at low priority outside the game, uses
half your CPU threads (at most 8) and stops inside 4.5 minutes, however long the recording is.

```
clonetrain Clones/Ana.hsmclone
```

This writes `Clones/Ana.hsmmodel` beside it. `--threads N` and `--minutes M` change the limits, and `--outcome X` how much
hits landed and taken weigh a move (below).

Or double-click `train.bat`, kept next to `clonetrain.exe` and `dsound.dll`. It lists the recordings in `Clones/`, with
their size and whether each has a model yet, and asks which to train (or all of them), the time limit, the weight of results
and the CPU threads; Enter keeps the usual setting. An old model is kept as `NAME.hsmmodel.bak`, and stays as it was if
training fails.

Share the `.hsmmodel` as you would the `.hsmclone`, and fight it the same way.

With `clone_weapon=` set to anything but `kit`, the clone is handed a weapon of that type half a second after it comes in, built by
the game's own weapon generator. `random` and `match` only pick a type it has moves for: in a model, a type it was trained on; in a
recording, one with at least a minute of play. If none fits, it keeps its kit, and the log says so. `Fetch.log` lists the types each
clone has moves for when it loads.

### Training sessions

A recording only teaches a clone what it holds. Most players fight with one or two weapons against whoever turns up, so a clone of
them has nothing to go on with a polearm, or against a foe in plate. A training session fixes that: a run of stages, each one weapon
type for you against one kind of foe, recorded into your own file.

Start one from the menu's "Training session" row, in an offline Free Mode fight. It needs no clone in the fight: one that is there
leaves, and comes back when the session ends.

- **Kinds:** a **full tour** takes every weapon type (Swords, Maces and Hafted, 1H and 2H, and Polearms) against every kind of foe
  (bare, cloth, mail, plate), each pair once before any comes twice. **Fill gaps** reads your recording first and gives the time to
  the pairs it has least of, so later sessions fill what is missing.
- **Stages** are 75 s each, so 20 minutes is 16 stages. A banner at the top of the screen says the stage, the weapon, the foe and the
  time left, with a 5 s countdown before each.
- **Weapon:** at the countdown you are handed a weapon of the stage's type from the game's weapon generator. It replaces what is in
  your right hand, and a weapon in your left hand is dropped. If it cannot be given, the stage goes on with what you hold.
- **Foe:** one at a time, 5 m ahead, of the tier that tends to wear that armour (0 for bare, 2 cloth, 4 mail, 6 plate), dressed by
  the game from that tier's equipment budget. When one dies, the next comes 3 s later. Fetch does not choose the foe's pieces, so
  what it really wore is read and recorded, and that is what counts.
- **Your armour** stays as you chose it.
- **Recording** is on for the whole session, whatever `record=` says, and goes back to how it was at the end. Each stage is marked in
  the file (a Stage event), and `tools/hsmclone.py info` counts them.
- **At the end** the banner shows a summary for 20 s: stages done, hits landed and taken, and how much play the recording now has of
  the pairs the session asked for, before and after. `Fetch.log` has the same, and a line per stage. Enter on the row stops a session
  early, and so does joining a lobby or leaving the Free Mode fight. Dying does not: you get the stage's weapon again on respawn.

Then train as usual. The stages are free fights, not scripted moves: fight as you would, as the clone copies that.

Recordings now also hold what a person could see: both fighters' reach, wounds, stance and facing, the nearest dropped weapon, what
armour each wears, the game's weapon type in each hand of both fighters, and how much health each body part of both fighters lost
since the row before. Files are version 3. When `record=1` finds an older one it moves it to `<name>.hsmclone.old` and starts a new
file, and logs that it did. `clonetrain` and the mod refuse older files. Models are version 5, which hear the fight's events and
what both fighters wear (below). A model of version 4 or 3 still loads and plays as it did, without what it never heard. Older models
are refused and must be trained again. An older file can be remade from the `.hsmrec` recordings it came from, as below.

`tools/hsmclone.py build NAME OUT.hsmclone RECORDING_OR_DIR...` makes a `.hsmclone` from `.hsmrec` recordings, without a kit. `.hsmrec`
files do not hold armour, so those columns stay empty. They do not say what a forged weapon was made of either, so forged weapons in
them have no type.
`tools/hsmclone.py info FILE` prints what a recording holds, its fight events included.

### Fight events

A recording also holds what happened in the fight, each event stamped with the input sample it came before, so it lines up with
the input exactly. For both fighters:

- **Hit**: who struck the other, with which hand (what that hand holds, fists too) or the body (a kick), where it landed (head,
  upper body, lower body, an arm or a leg), and how hard. A weapon touching a body is a hit at most every 100 ms per hand.
- **Clash**: your weapon met theirs, which hand of each, and how hard.
- **Damage** and **Sever**: who lost health in which part and how much, and which part was cut off.
- **Thrust, Kick, StuckOn / StuckOff**: per hand, from the fighter's own flags. **ParryOn / ParryOff** come from a parrying flag
  the game never sets, so there are none; Parried below is the game's parry.
- **GrabOn / GrabOff**: per hand, and which part of the other fighter it holds. **GrabbedOn / GrabbedOff**: being held.
- **PickUp / Drop**: a weapon in a hand. **Fall / GetUp**, **Death**.
- **Strike** (pre11 on): how one of your blows met the foe: on bare skin, through a gap in the armour, beating the armour, or held by
  it (below), with the part, the surface struck, the damage and how much of it was a thrust.
- **Armour** (pre11 on): what a fighter wears changed (a new foe, a piece lost).
- **Stage** (pre11 on): a training session's stage began, with its weapon, tier and kind of foe, or the session ended.
- **Parried** (pre12 on): who caught the other's blade on guard. The game does this when a blade meets the weapon of a fighter who
  is guarding, wins a roll on their weapon skill, and is not falling or out on their feet: the blade's cut and push are weakened,
  and enough catches in a row stop its cut. The event has the hand of the blade caught, how fast it came, and how full the parry
  was, from 0 to 1 (1 stopped the cut). Catches a few ms apart are one parry. Half Sword's update of build 26568321 took this
  catch out of the game, so on that build there are no Parried events: blades that meet are Clashes ([Half Sword's builds](#half-swords-builds)).

They go in an Events block straight after the Play block they belong to. The file stays version 3: an older Fetch skips the block,
and this one reads a file without it. A block that is not well formed, or follows no Play block, is skipped alone. Every Play block
now gets one, empty if nothing happened, so play with no events is known to have had none. The rules are
`shared/clone/hsm_clone_events.h`.

Strike, Armour and Stage go in a More block right after the Events block, so that an older Fetch, which refuses an Events block with
kinds it does not know, still reads the rest. Every Play block from pre11 on gets one.

Parried, and any kind of event added after it, goes in a Later block after the Input block (pre12 on). A Fetch before pre12 skips
the block, and reads the file as it did. A Fetch reading a Later block passes over the kinds in it that are newer than itself and
keeps the rest, so the next new kind goes in the same block without a new one. A recording from before pre12 has no parries in it:
its parry count is unknown, not 0.

### Mouse sensitivity and every other key (pre12 on)

The mouse is recorded as your hand moved it, before the game scales it by its "Mouse Sensitivity". From pre12 a recording keeps
that sensitivity (and "Invert Mouse") too, and a clone plays the mouse at the recorder's sensitivity, whatever the player fighting
it has set. So a clone of someone at 2.5 swings as they did on a machine set to 0.5. Changing the setting mid-fight starts a new
block, so each stretch of play keeps its own. The camera is not affected either way: it turns by the recorded angles. A model
keeps none yet, so it always plays at yours.

A recording from before pre12 keeps none, but its own turns show it, and Fetch reads it off them when it loads the file: nothing in
the file is changed, so older Fetch versions read it as before. Off guard the game turns the camera by exactly the mouse movement x
the sensitivity x 0.9375, and while guarding by an 80th of that, so most of a recording's turns share one of those two ratios. The
one most turns share is the sensitivity, and which way the camera pitched on them says whether the mouse was inverted. All the
plays in a file that keep none get the one they show together. On the recordings we tried it came out at the setting to two
decimals. A file with too few mouse turns to tell (one built from `.hsmrec` recordings with no turning, say) still plays at yours.
`Fetch.log` says which: `its turns show 2.00: its mouse plays at that`, or `its turns do not show it`. `hsmclone.py info` shows
the same. The rule is `LookGuess` in `shared/clone/hsm_clone.h`.

Every key press that goes to something other than a fighter is recorded too: a key bound on a mod's gun, say, which the game calls
as an `InpActEvt_` function on that actor. Fetch keeps its class and function name and the sample it came before. When a clone's
move plays that moment, the press goes to the same class of thing the clone carries (its held and sheathed weapons, attached
actors), so a clone holding the same modded gun fires it as the player did. A key's pressed and released ends are paired: a
release plays only after its press, and a key still down is let go when the move ends, the clone turns to face you, or it goes
down. A key on something the clone does not carry is left out (`Fetch.log` says so once). The fighter's own keys are in the input
stream as before; Talk, the camera lock and the middle mouse button are still never played.

Not covered: models (replay only), mouse axes bound on a mod's actor (`InpAxisEvt_`), keys a mod reads itself (UE4SS Lua
keybinds, raw key polling), and keys on something the fighter does not carry (a mod's menu, a manager actor).

These go in an Input block after the More block. An older Fetch skips it. The rules are `shared/clone/hsm_clone_input.h`.

### Armour

Before pre11 the armour columns of a recording were wrong: Fetch looked for armour attached to the fighter, and the game does not
attach it there. Now it reads what the game itself put on each fighter, piece by piece, every couple of seconds. For each part (head,
face, neck, upper and lower body, each arm and leg) it keeps what a player can see on it: bare, cloth, mail or plate. And the
helmet: none, a soft hat, open-faced, half-open, or visored.

A piece is read from its own data first: its slot, whether the game calls it metal, whether it has a visor that opens, whether it
hides the jaw, and its hit sound (which tells mail from plate). The name of the class only comes after, so modded armour reads right
as long as it is built like the game's own. The first time Fetch meets each kind of piece it logs what it read from it and how it
counts it (`adapter: armour <class> slot=... metal=... visor=... -> ...`), so a modded piece that reads wrong can be found. `Fetch.log` has a line each
time what a fighter wears changes:

```
clone: armour: foe helm=visored head=plate face=plate neck=mail upper=plate lower=mail armR=plate ... (11 pieces)
```

Play recorded before pre11 has its armour columns cleared when read, so training and replay take them as not known rather than as
bare. Replay's search for the nearest moment also weighs the foe's armour when both moments know it, so it plays clips recorded
against a foe dressed alike.

**Strikes.** Fetch also watches the game's damage function and reads what each of your blows did. A blow that reached skin is
**bare** if that part had nothing on (or only cloth), and a **gap** if it had mail or plate, so it went between the pieces or through
a visor slit. A blow on the armour **beat** it if it still did damage, and was **held** if not. The menu's strike line counts these,
and `tools/hsmclone.py info` prints them as `your strikes:`. `strikes=0` turns this off.

**What the clone does with it.** Models of version 5 hear what both fighters wear. The clone does not get a goal of its own such as
"aim for the gap": it copies what you did against foes dressed like this one. If you thrust at visor slits, it can learn to; if you
never do, it never will. A habit only comes through once the recording holds enough of it, and training sessions are the way to
record more fights against each kind of foe.

### What training does with them

**The clone reacts as you do.** The nets also hear 13 numbers about the fight just before each moment: how long since you landed a
hit, took one, clashed, and since the foe last parried, thrusted or kicked (up to 3 s, then "long ago"), and whether you are held,
holding them, down, they are down, your blade is stuck, or they are parrying. If you back off after a blow or press in when they
fall, the clone learns that. In the fight the clone hears its own fight the same way: you are its foe. The foe's parrying comes from
ParryOn, which never comes, so those two always read "long ago" and "no". The nets do not hear Parried yet: the models were trained
without it, and a model that did would not load in an older Fetch.

**A move that worked counts more.** Each moment's weight in training goes by what came in the next 1.5 s, the nearer the more:
a hit you landed, damage you did and a kill count it up, a hit taken, damage taken and your death count it down. A weight is
between 0.4 and 3 times the usual, and all of them average 1. So the clone still copies all of your moves, but leans to the ones that
landed. `--outcome 0` turns this off; the trainer's own option is `Options::outcome`.

A recording from before pre6 has no events. Its moments read "not known", weigh the same, and train as before; the trainer says
`no fight events in this recording`. Otherwise it says how many moments had events and how many of those led to a hit landed.

## How it works

Averaging a player's input smears their swings, so the model never writes input. Training groups short moments of play into 1024
kinds of move. Three small nets, averaged, look at what the clone sees and its recent input, and say which kind of move comes next
and what the next 300 ms should do. The clone then plays a real recorded clip of that move, so a swing keeps the player's own shape.
It chooses the clip that starts from what it already holds and carries on the mouse, and eases its aim towards the foe over 200 ms.

A move only makes sense with the weapon it was recorded with, so training sorts moves by the game's own weapon types: Swords, Maces
and Hafted, each 1H or 2H, and Polearms. Fists are a group too. Each group gets its own kinds of move, in proportion to how much was
played with it, and the clone only plays clips from the group it holds. If the player never used that type, it plays the nearest one
they did: its own family first, then what swings alike, and fists last.

The game only gives a type to the weapons it builds at random. Premade weapons list which hand they take, so `clone/hsm_weapon_types.h`
names a type for each by hand: 72 classes. A forged weapon's type comes from its parts, the head for the family and the grip for the
hands. A weapon with no type goes by length, into two groups of its own (under 0.65 m, and under 1.3 m); anything longer counts as a
polearm. That covers shields, stools and every forged weapon in an old recording. A type with under 1 move in 16 also plays the
nearest of those two groups, which may well be the same weapon, but it does not borrow from another known type.

A clip can begin or end partway through a grab press. While the hand holds something, those cut presses are hidden, because in the
game a short press with a weapon in hand drops it, and a long press cut short would do the same. A whole press inside a clip is kept:
that is something the player really did.

The model runs in plain C++ with no ML runtime. Decisions come about five times a second. The clone is driven as a remote player
is, so it has the same body and rules as a person.

Without a model, the clone replays: it finds the nearest moment in the recording and plays what followed. That needs the whole
`.hsmclone`, which grows with play.

Replay leaves out what was not fighting. The file keeps all of it; the clone just never picks it:

- **Walking off a beaten foe.** When the foe goes down and stays down until the fight ends, every moment there further than
  2.5 m from it. Finishing it, closer in, stays.
- **Standing still.** Every moment in 3 s or more with no input at all: a pause, a menu, a wait. Holding a guard is input.
- **Someone else on the ground.** A moment where a different one of the two was fallen, knocked out or downed is taken only
  when no moment agrees. A standing foe is not struck as if it lay there, and a clone on its feet does not play getting up.

`Fetch.log` says how much of the recording that leaves: `clone: it copies 79% of the recording; left out: 7% walking off a beaten foe, 14% 3 s or more with no input`. In the recordings
measured, a beaten foe lay on the ground for a fifth to a quarter of the play, and 1 to 7% of it was walking off.

Replay also minds what happens to it, this fight only:

- **Pain.** How much health it lost in the last second, and whether the foe was in front or behind (more than 60° off)
  when it did, is part of what it compares. Hurt from behind, it picks a moment where the player was hurt from behind too.
- **What did not work.** A move it was playing while it got hit costs four times as much for 10 s, so it tries something else.
  Any move costs twice as much again for 3 s after it is picked, unless it is the one still playing or nothing else is near.
  Going on with the clip it is in is not a repeat.
- **Turning to face.** With the foe more than 75° to one side and no recorded moment that has it there (more than 60° from where
  the moment had it), or after getting hit from behind, or with the foe behind (over 120°) within 3 m, the clone stops and turns
  toward it at up to 360° a second, guard held, before it picks again. It gives up after 1.5 s and does not try again for 1 s.
  A foe on top of it (under 0.5 m) does not count.

In a test loop on two recordings, where the foe stands still, that cut picking the same move again within 3 s from 54% to 14%
(Dudie) and from 30% to 6% (m00). The longest time without input fell from 16 s to 3 s. Turned away, the clone faced the foe in
2.4 s rather than 3.2 s (Dudie) and in 0.6 s rather than 1 s (m00). When it goes down, `Fetch.log` says how often it did both:
`clone: down; this fight it turned to face you first N times, and was hurt playing M moves it then left alone`.

The rules are in `shared/clone/`: `hsm_clone.h` (recordings, replay), `hsm_model.h` (the file, the model, its brain),
`hsm_train.h` (the trainer), `hsm_clone_rules.h` (levels, the lobby rule), `hsm_clone_events.h` (fight events),
`hsm_clone_armor.h` (armour and strikes), `hsm_clone_drill.h` (training sessions, the clone's weapon), `hsm_fetch_menu.h` (the menu), and
`shared/interop/hsm_mod_link.h` (the link with HSMMod). The mod is `src/`, which holds its own copy of all the engine and fighter code it uses.

### Clones offline, recording anywhere

A clone fights only outside a Steam lobby, and so do training sessions and the clone-thinking overlay. Fetch asks Steam itself:
entering a lobby arrives as a callback, and from then on it asks once a second whether the lobby is still there. Joining a lobby
removes the clone and ends a training session. Recording goes on (pre14 on), unless `record_online=0`. Half Sword never starts
Steam itself, so Fetch waits 15 s for a mod that does, then starts it and runs its callbacks. If Steam will not start, there are no
clones and no recording until it does (the recording is named after your Steam name), and Fetch tries again every 10 s. Fetch hooks ProcessEvent 20 s after the game starts, so that it does not patch at
the same moment as anything else.

### Playing with HSMMod

With HSMMod in the same game, Fetch records your own play in its lobbies as in any fight (pre14 on): your keys, your fighter and
the nearest foe, the fight's events and your kit. Each player's Fetch records only that player. A teammate (co-op, or one on your
team) is never taken for the foe. `Fetch.log` says `clone: recording in a Steam lobby too (record_online=0 stops it)` once.

With HSMMod 0.8.0-pre44 or newer, the two mods also share a small link:

- The Fetch menu opens in a lobby (above). While it is open, or while you type a name in it, HSMMod leaves its own keys alone. While
  HSMMod's chat is open, `;` is only a semicolon there.
- Keys that HSMMod holds back and plays again a moment later (its input delay) are recorded once, as you pressed them.

`Fetch.log` says `fetch: HSMMod is here (sync mode ...)` when it sees one. With an older HSMMod, recording works the same, but the
menu stays shut in a lobby, and the log says so once. When you join a lobby, the log also says if `menu_key` or `mind_key` is one of
HSMMod's keys (T, Y, U, H, K, V, G or B): one press there may do both.

In a lobby the recording holds less than offline:

- Your foe's left-hand thrusts, kicks, parries and grabs are not seen there.
- How a blow of yours met the foe (a Strike event) is this machine's estimate. The other player's machine decides what it did.
- Clones stay offline: no clone joins a lobby, and no training session runs there.

### Half Sword's builds

Fetch runs on Half Sword's Steam builds 24185754 and 26568321 (the latest update), and `Fetch.log` names the one it
found (`engine: steam-26568321 ...`). Another build is left unmodified, and the log says so. On build 26568321:

- **Chain Attack** is a new key, and it is recorded and played like the others. A Fetch from before pre15 loads such a recording
  and plays every other key in it; it leaves Chain Attack out.
- **Guarding** is the game's new combat stance or either hand raised. What a recording keeps as guarding, and what a clone sees
  of your guard, follow it.
- **No Parried events**: the game no longer catches a blade on guard. Blades that meet are recorded as Clashes. `Fetch.log` says
  `clone: parries: this game build has no blade catch ...` once.
- **Clones start whole.** The game now keeps a fighter's wounds with its kit. A clone starts without the wounds its kit was
  saved with, and a recording's kit is kept without the game's saved blood and dirt.

Recordings and models are the same files on both builds, and each loads in every Fetch.

### Shared files

A shared file is untrusted. A model must pass all of these or it is refused whole, the log says why, and nothing crashes: size, version,
a checksum on every block, every part present and in order, layer shapes, finite values, no trailing bytes. What it outputs is clamped,
Talk and the camera lock are masked, and the file name cannot leave `Clones/`. A recording's keys are names of letters, digits and `_`
only, and are only ever called on what the clone itself carries, never on a fighter. A kit must pass `CheckPassportSave` or the clone wears
yours.

## What has been measured

All of this is offline, on one player's recordings (3.41 h usable). None of it shows how a fight feels in game.

The rule for making the model the default was fixed before the test, in `docs/measurements/RULE.md`. Recordings were split by
recording. Everything was tuned on the validation set, and the test set was run once, through the shipped code and the real file. The
saved output is `docs/measurements/TEST_RESULT.txt`. **The model passed every rule, so `auto` prefers it.**

Next 300 ms on the test set: 30 recordings, 0.72 h, 9789 moments.

| | Model | Replay | Repeat last 300 ms | Random |
|---|---|---|---|---|
| Change score J | .459 | .437 | 0 | .187 |
| Key F1 | .670 | .637 | .666 | .221 |
| Guard F1 | .925 | .918 | .929 | .266 |
| Mouse correlation | .432 | .374 | -.240 | .001 |

The J difference is +.021, with a 95% interval by recording of +.001 to +.035. That is a narrow pass on one training seed, and seed
noise on a single net was about that size. Swings stay sharp: mouse size is 91 and 104% of the player's by RMS, and 87 and 98% at the
99th percentile. On its own training rows the model scores J .507 and mouse .543, so it memorises moderately.

Closed loop on the same recordings, with the clone fed its own results. **The foe in this test does not react to the clone.**

| | Player | Replay | Model |
|---|---|---|---|
| Facing error, degrees | 20.5 | 19.0 | 13.4 |
| Closing speed, m/s | +.10 | +.06 | +.09 |
| Range, m | 2.5 | 3.4 | 3.4 |
| Weapon tip speed | 2.27 | 2.13 | 1.88 |
| Idle share | .23 | .28 | .32 |
| Presses per second | 1.17 | 1.09 | 1.07 |
| Mouse activity per second | 197 | 118 | 110 |
| Guard changes per second | .27 | .21 | .17 |
| Right / left guard on | .46 / .08 | .43 / .10 | .32 / .05 |

The model faces better than the player and closes about as fast. It stands further off, moves the mouse less, guards less and swings
slower than the player. Facing in this loop shifts with the clip bank, so read it as a rough figure.

Cost, built as the mod is, with no AVX:

| | Mean | p99 | Max |
|---|---|---|---|
| One decision | 82 µs | 170 µs | 227 µs |
| One frame | 6.7 µs | 11.4 µs | 113 µs |

Loading takes 25 ms on the mod's worker thread, never in a frame. The model takes about 17 MB of memory, by estimate.

Training on a 16-core Ryzen 9 5900XT:

| Play | Threads | CPU path | Time |
|---|---|---|---|
| 5.11 h (3.41 h real, part repeated) | 6 | AVX2 | 90 s |
| 5.11 h, same | 4 | AVX2 | 119 s |
| 3.41 h | 6 | no AVX2 | 210 s |
| 3.41 h | 8 | AVX2 | 52 s |

A slower CPU gets fewer passes rather than a late finish. The limit shrinks the schedule, and the trainer says so if it did.

### Weapon awareness, added after that test

The test set above was already used, so this version was not run on it again. These results are from the validation set and from 18
newer recordings (0.59 h) kept out of all training. **The foe in those 18 is the clone, not a normal opponent**, and they were scored
once, after the inputs were chosen. All are one player, offline, and mostly one seed.

| In the closed loop, validation | Player | Before | Now |
|---|---|---|---|
| Decisions playing a clip of the wrong weapon length | 0% | 59% | 0% |
| Short grab presses while armed, per minute | 0.28 | 1.56 | about 0.14 (three seeds) |

On the 18 newer recordings the model passed every point of `docs/measurements/RULE.md` against replay: J +.020, interval by
recording +.005 to +.036, key F1 .637 against .614, guard F1 .805 against .768. Median facing was 16.3 degrees, the player's 14.3,
replay's 11.8. This is not a repeat of the pre-registered test: the data and the foe differ.

Extra inputs for the nets were tried on validation, each a separate run. Only the foe's wounds went in, and only just: it was the one
set that kept the facing rule on both seeds scored. It is not a clear win. Seeds differ by about as much as the gain, and on the 18
newer recordings the same model without wounds predicted the player better (J +.049, key F1 .656, guard F1 .848) while missing the
facing rule by 0.7 degrees. All inputs together did worse on key presses (.659 to .666), the sign of learning fights rather than habits.
Armour was not tried, because no recording holds it yet. It is heard from model 5 on, without a measurement yet: no recording with
real armour columns exists until pre11 is played.

The recordings also show less to learn than hoped. With the foe down, or its back turned, the player's range and swing speed change
only a little, so a clone that copies them changes only a little too.

### The game's weapon types, added after that

Validation, three seeds each, closed loop for facing. Forged weapons in these recordings have no type, so this tests about half of
the armed play. Hours by group over all 179 recordings: no weapon 1.22, Maces 2H .51, Hafted 1H .52, Polearms .28, Hafted 2H .14,
Swords 1H .07, no type short .42, no type middling .83. Nothing was played as Swords 2H or Maces 1H.

| | Replay | By length | By type | By type, nets hear it |
|---|---|---|---|---|
| J | .459 | .476 | .472 | .472 |
| Key F1 | .647 | .681 | .677 | .669 |
| Guard F1 | .875 | .903 | .901 | .904 |
| Mouse correlation | .467 | .496 | .433 | .452 |
| Median facing | 14.7 | 17.7 | 17.0 | 15.7 |

Sorting by type did not make the clone predict the player better. It is the right thing for the clone to do with a weapon, not a
higher score, and mouse correlation fell. Letting the nets hear the fighter's own type won part of that back, and with it every seed
met all of `docs/measurements/RULE.md` except point 1, as by length also did: on validation alone the interval for J still crosses
0. Splits by type are small, and swing both ways between types: Hafted 2H rose from .12 to .50 mouse correlation, Polearms fell from
.34 to .15, on under 220 decisions each.

Also hearing the foe's weapon type gave J .476, key .676, guard .899, mouse .446, facing 16.4, within seed noise, and one seed in three
missed point 3. So it is recorded but the nets do not hear it. The hit columns are recorded and not used: nothing here reads them yet.
They are meant for learning what works, such as going for a weak spot.

Hits are read from the body, not from who struck: a part that lost health since the row before. On a sample of old recordings 178 of
178 damage events showed as a drop, and 179 of 181 drops had an event. A fall counts too. A new foe starts from nothing, so its old
wounds are not taken for a hit.

## Limits

It copies habits, not plans. It cannot do what the recordings never show, and it is a style, not an exact copy: the game is
physics-driven, so the same input does not give the same swing. It fights one foe, the local player.

Fighting without a weapon is its weakest part: on validation it stays further off and swings slower than the player does. It does not
go and fetch a dropped weapon, partly because the recordings show the player rarely doing that either.

The trainer is built for Windows. Its source also has a Linux path, which has not been built or run yet:

```
g++ -std=c++20 -O2 -static -pthread -Ishared -Ishared/protocol \
    tools/clonetrain.cpp -o clonetrain
```
