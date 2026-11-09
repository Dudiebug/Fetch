# Fetch

Fetch is a mod for Half Sword that records how you fight and lets you, or anyone you send the file to, fight that style offline as
a clone. It is a mod of its own (`dsound.dll`) and needs no other mod.

**Fetch and HSMMod don't mix.** HSMMod treats `dsound.dll` as another mod and won't let you play multiplayer while it is there.
Take `dsound.dll` out of the game folder (or rename it, say to `dsound.dll.off`) before you play HSMMod, and put it back to use
Fetch.

**The main, most tested way to use it is without training:** record, then fight the recording. Training a model is optional (see
[below](#optional-train-a-model)).

## 1. Install

1. Download `dsound.dll` from the latest [release](https://github.com/Dudiebug/Fetch/releases).
2. Put it in the game's `HalfswordUE5/Binaries/Win64` folder, next to `HalfswordUE5-Win64-Shipping.exe`.
   - Steam: right-click Half Sword, **Manage → Browse local files**, then open `HalfswordUE5/Binaries/Win64`.
   - On Linux (Proton), also set the game's launch options to `WINEDLLOVERRIDES="dsound=n,b" %command%`.
3. Start the game from Steam, with Steam running.

The first start writes `Fetch.ini` and `Fetch.log` in that same folder, and a `Clones` folder appears there when you record. Fetch
does nothing until you turn something on.

## 2. Record yourself

1. Start an offline fight (Free Mode, or any fight level).
2. Press `;` (the key right of L on a US keyboard). The Fetch menu opens.
3. On **Record my fighting**, press D so it says ON. Press `;` to close the menu.
4. Fight. Only play near a living foe is kept, so walking around menus or an empty arena adds nothing.

Your fighting goes into `Clones/<your Steam name>.hsmclone`, about 22 MB an hour, and every later session adds to the same file. The
more you fight, and the more weapons and foes you fight with, the better the clone. A **training session** (in the same menu) walks
you through every weapon type against every kind of foe for you; see [docs/CLONE.md](docs/CLONE.md#training-sessions).

Recording pauses while you fight a clone, and in a Steam lobby.

## 3. Fight a clone

1. Make sure the `.hsmclone` you want is in the `Clones` folder: your own, or one someone sent you.
2. In an offline fight, open the menu with `;`, go to **Clone to fight**, and press D until it shows that name.
3. Close the menu. About 3 s later the clone appears 5 m ahead of you, facing you, and Free Mode's own fighters leave. When you
   beat it, another comes.

The clone plays back real moments of the recorded player's input, picked to fit what is happening in your fight. It wears the kit the
player last recorded in.

If there is also a `.hsmmodel` of the same name, the clone uses that instead. To stay with the recording, set **Clone brain** to
`replay` in the menu.

## 4. Share

Send the `.hsmclone` file from your `Clones` folder. The other player puts it in their own `Clones` folder and picks it as in step 3.
Fetch uploads nothing itself.

## The menu (`;`)

W/S or the arrow keys choose a row, A/D change it, Enter runs it, and `;` or Esc closes the menu. It opens only offline, in a fight,
with your fighter alive. Every change is saved to `Fetch.ini`.

| Row | What it does |
|---|---|
| Record my fighting | Recording on or off. |
| Clone to fight | Which clone from `Clones/` joins your fights, or none. |
| Clone brain | `auto`, `model` or `replay`. |
| Clone's weapon | Its own kit, a random weapon type, the same type as yours, or one type. |
| One on one | Whether Free Mode's own fighters leave while a clone is in. |
| Bring the clone back | The clone leaves, and a new one comes. |
| Training session | Starts a guided session (Free Mode only): every weapon against every kind of foe, or what your recording lacks. |
| Session length | 5 to 60 minutes. |

All the settings are also in `Fetch.ini`, explained in [docs/CLONE.md](docs/CLONE.md).

## Optional: train a model

A recording grows with play. A trained model is a fixed 6 MB however long you played, which is handier to share, and it plays the
same real clips but picks them with a small neural network. This way has been tested less in game.

Put `clonetrain.exe` and `train.bat` from the release next to `dsound.dll`, double-click `train.bat`, pick your recording, and
press Enter at each question. In under 5 minutes it writes `Clones/<name>.hsmmodel`, which the clone then uses. It needs no GPU and
no network. The release's `all.hsmmodel` is a ready-made one to try.

## If something is wrong

- **No clone appears:** check it is an offline fight (not a Steam lobby), and that **Clone to fight** shows a name.
- **HSMMod won't let you play:** that is Fetch's `dsound.dll`. Take it out of the game folder while you play HSMMod.
- **The menu does not open:** it needs an offline fight with your fighter alive and the game's HUD showing.
- Anything else: look in `Fetch.log`, and send it with your report.

To remove Fetch, delete `dsound.dll` (and `Fetch.ini`, `Fetch.log` and `Clones` if you want).

## More

- [docs/CLONE.md](docs/CLONE.md): how it works, every setting, and what has been measured.
- [docs/HUMAN_TESTING.md](docs/HUMAN_TESTING.md): the in-game checks.

## Build

Cross-compiled with [llvm-mingw](https://github.com/mstorsjo/llvm-mingw):

```
LLVM_MINGW=/path/to/llvm-mingw ./build.sh
```

This writes `build/dsound.dll` and `build/clonetrain.exe`.

| Path | What |
|---|---|
| `src/` | The mod: hooks, the game adapter, recording, the clone, training sessions, the menu. |
| `shared/` | Game-independent logic, header-only C++20. `shared/clone/` is Fetch's own; the rest is shared with HSMMod. |
| `tools/` | `clonetrain.cpp` (the trainer), `train.bat`, and `hsmclone.py` for reading recordings. |
| `docs/` | `CLONE.md` (how it works), `HUMAN_TESTING.md` (in-game checks), `measurements/`. |
| `third_party/minhook/` | MinHook, for the hooks. |

## Licence

MIT, see [LICENSE](LICENSE). Third-party code and credits: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
