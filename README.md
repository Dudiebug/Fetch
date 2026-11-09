# Fetch

Fetch is a mod for Half Sword that lets you record how you fight, train a small model from it, share one file, and fight that
style offline. It is a mod of its own (`dsound.dll`): it needs no other mod and works beside HSMMod.

- **Record** your fighting, or play a **training session** that walks you through every weapon type against every kind of foe.
- **Train** with `train.bat` or `clonetrain`: no GPU, no network, under 5 minutes.
- **Fight** any clone in `Clones/`, offline. It plays real clips of the recorded player's input, picked by the model.

Download the latest release from [Releases](https://github.com/Dudiebug/Fetch/releases). How it all works, every setting and
what has been measured: [docs/CLONE.md](docs/CLONE.md). The in-game checks still to be done: [docs/HUMAN_TESTING.md](docs/HUMAN_TESTING.md).

## Install

Copy `dsound.dll` next to the game exe (`HalfswordUE5/Binaries/Win64`), with `clonetrain.exe` and `train.bat` beside it. Start the
game from Steam. On Proton, add `WINEDLLOVERRIDES="dsound=n,b" %command%` to the launch options. Press `;` in a fight for the menu.

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
