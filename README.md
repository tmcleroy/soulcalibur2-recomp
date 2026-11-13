# Soulcalibur II Recomp

**[Join me on Discord](https://discord.gg/FSC9GZ6nm)**

[![License: GPL-3.0](https://img.shields.io/badge/license-GPL--3.0-blue)](LICENSE)

![Link against Heihachi on VS Battle's character select](docs/screenshots/select-link-heihachi.jpg)

**All three console exclusives in one game: Spawn, Link and Heihachi.** Soulcalibur II came out on the Xbox with
Spawn, on the GameCube with Link and on the PlayStation 2 with Heihachi, and none of those releases had more than
one of them. This port has all three on the same select screen, in every mode, against each other.

**An unofficial native port of Soulcalibur II (Xbox) for macOS, Linux (Steam Deck and Steam Machine included),
Windows and Android.** It is a static recompilation of the Xbox game: its program is translated ahead of time into C and runs on
a native runtime, with no emulator. It adds a training mode, online play with rollback, a free camera with a pause,
widescreen, higher resolutions and shaders.

**No game files are included.** Your own Soulcalibur II Xbox disc image is the only one you need to play: the USA
disc, or the European one (Europe, Australia), which plays in English, German, French, Spanish or Italian (the
Language feature; for now on the Mac, and without the guests).
Link and Heihachi are optional: give the launcher your own GameCube disc, PlayStation 2 disc, or both, to add
them. This project cannot help you get any of them.

| | |
| --- | --- |
| ![Link against Heihachi](docs/screenshots/link-vs-heihachi.jpg) | ![Heihachi against Spawn](docs/screenshots/heihachi-vs-spawn.jpg) |
| ![Spawn against Link](docs/screenshots/spawn-vs-link.jpg) | ![Link against Talim in widescreen at 3x](docs/screenshots/link-fight.jpg) |

## Spawn, Link and Heihachi together

The Xbox game has Spawn. Link and Heihachi are converted on your machine from your GameCube and PlayStation 2
discs (models, moves, weapons, voices, select-screen art and endings) and stand beside him: in Arcade, VS, the
Extra modes, Weapon Master, and the Museum's profiles, theaters and Art Gallery.

| | |
| --- | --- |
| ![Link and Heihachi on VS Battle's character select](docs/screenshots/select-link-heihachi.jpg) | ![Heihachi and Spawn on VS Battle's character select](docs/screenshots/select-heihachi-spawn.jpg) |
| ![Weapon Master's select with the guests' column](docs/screenshots/weapon-master-guests.jpg) | ![The Art Gallery with the guests' pictures](docs/screenshots/art-gallery.jpg) |

## Training

**Hit boxes** (the game's own collision shapes: where each fighter can be hit, and its weapon and limbs, red while
an attack is live), **frame data** (startup, active, recovery, advantage on hit or guard) with a **frame meter**, an
**input display** and a **combo readout** down each fighter's side, **start over** (the round
as it began, one key), a **dummy that records and replays** what you show it (in Practice or VS Battle), health
refill and infinite time; with **save states**, pause and frame advance, and slow motion from the in-game menu. See
[docs/PLAYING.md](docs/PLAYING.md#training).

| | |
| --- | --- |
| ![Hit boxes: Talim's attack live, in red](docs/screenshots/hit-boxes.jpg) | ![Frame data and the frame meter](docs/screenshots/frame-data.jpg) |

## Online play

Two players, each on their own machine. Host from the launcher or from the in-game menu while you play, and give
the other player the match's code (or find each other in the lobby, or invite them from the launcher's waiting
room); join with the code. Fights use **rollback**;
the menus wait for both players. During a match: both players' names, the ping, and a chat (T). A dropped
connection waits for the other player to come back with the same code. See [docs/online.md](docs/online.md).

![The in-game menu's Online page](docs/screenshots/menu-online.png)

## Free camera and photo mode

Switched on in the launcher: \` (backquote) or a click of the right stick takes the game's camera and flies it,
in fights and menus. P (or Start while flying) **pauses** the fight while the camera goes on flying; . (or White)
steps a frame; H (or Black) hides the life bars and timer; F12 saves a screenshot.

| | |
| --- | --- |
| ![Link's swing held by the pause, the HUD hidden, seen from low down](docs/screenshots/paused-no-hud.jpg) | ![The free camera high above Link and Heihachi in a paused fight](docs/screenshots/free-camera.jpg) |

## And

- **Widescreen** (the game's own 16:9 mode) and **resolutions** up to 8x (5120x3840).
- **HD textures**: the GameCube version's HD texture pack for Dolphin, from your own download, matched to the Xbox's
  textures when you import it; or dump the game's textures and make your own.
- **Shaders**: RetroArch's CRT and scanline shaders (CRT Royale, CRT Guest, CRT Lottes and more) at the Xbox's 480
  lines, or your own preset.
- **An endless battle**: in any fight, the game plays both fighters for as long as you like (touch the controls to
  take one over).
- **An in-game menu** (Esc, F1 or a gamepad's Back): save states, speed, training, cheats (no damage, one-hit
  knockouts, everything unlocked), online, sound, quit.
- **Android**: the launcher and the game in one app for 64-bit phones and tablets (Android 10 or later, Vulkan),
  with touch controls (a stick, the four attack buttons, Start and the menu) beside any gamepad.
- **A launcher** that checks your disc, builds the guests, sets features and both players' keys and buttons, hosts
  and joins online matches, and keeps itself up to date.

| | |
| --- | --- |
| ![The launcher's Guest page](docs/screenshots/launcher-guests.png) | ![The launcher's Features page](docs/screenshots/launcher-features.png) |

## Getting started

1. Run the launcher (`Soulcalibur II Recomp.app`, `soulcalibur2-launcher` on Linux, `soulcalibur2-launcher.exe` on
   Windows, the app on Android: install `Soulcalibur2Recomp-android.apk`).
2. On its **Game** page choose your Soulcalibur II (USA) Xbox disc image (`.iso`), or the `Default.xbe` among the
   disc's files. It is checked, and only ever read (on Android it is copied into the app's folder first). The check is of the disc's `Default.xbe` (image hashes differ
   with the dumping tool): SHA-256 `d7c2e1fc993350821e3f7e13cfd1c9d4ba6a7bf91011005bdd1f843899efb730` as on the
   disc, or `7ce12851850ab46766f77c612b251afe7c8bd81f879e11b4836bdf766be542c6` with extract-xiso's media patch; the
   European disc's `074463537bf0a2753c72aa6ef1b438851b281954a7ec0cc7919f2464477b5292`, or
   `4ad4e124a1773a6f78acf46397073affe9415cde576fe6e242e3260a0e41a1ec` patched.
3. For the guests, choose your GameCube and/or PlayStation 2 disc on the **Guest** page; the launcher builds them
   once (a few minutes).
4. Press **Play**.

How to play, the controls and every page and menu are in [docs/PLAYING.md](docs/PLAYING.md). Settings and saves stay
in your user folder (`~/Library/Application Support/Soulcalibur II` on macOS, `~/.local/share/Soulcalibur II` on
Linux), not beside the game's files.

Status: Arcade, VS, Weapon Master, the Extra modes, the Museum and online play work, tested mostly on macOS; the
Linux build runs on a Steam Machine. Windows builds but is untested. Android runs on a Pixel 9 and in the emulator.

## Building (macOS)

```sh
git submodule update --init --recursive
brew install sdl3 shaderc spirv-cross ninja cmake
python3 -m venv private/venv && private/venv/bin/pip install capstone==5.0.7
# put your disc's files in private/disc/ (Default.xbe, root.olk, movie.afs, bgm.afs)
private/venv/bin/python tools/build_game.py all
# the European disc too: its files in private/disc-pal/, then
private/venv/bin/python tools/build_game.py --pal lift
private/venv/bin/python tools/build_game.py app   # build/Soulcalibur II Recomp.app
```

Linux and Windows builds: `tools/release/linux.sh` and `tools/release/windows.sh` (in Docker); Android:
`tools/release/android.sh` (the Android SDK and NDK; see the script). Generated code and
everything taken from the disc stay in `private/`, which git ignores. How the port works is in [docs](docs/README.md).

## Credits

The runtime is NoRain211's DOA runtime (doa3-re, doaxvb-re, doa2u-re); the lifter is sp00nznet's xboxrecomp, in
NoRain211's fork. XDK library functions are found with Cxbx-Reloaded's XbSymbolDatabase signatures. The x87 math the
game's code uses (sin, cos, atan2, log2, expm1) is musl's (MIT, runtime/x87_math/COPYRIGHT), so it is the same on
every system.

Not affiliated with Bandai Namco, Nintendo or Todd McFarlane Productions. Everything else this project uses or ships, with its license, is in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Contributing

Bug reports and pull requests are welcome: see [CONTRIBUTING.md](CONTRIBUTING.md) first (above all: nothing from the games goes in the repository or in issues).

## License

GPL-3.0-or-later.
