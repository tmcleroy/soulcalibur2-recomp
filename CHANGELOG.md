# Changelog

## Unreleased

- **Shader preparation (SDL GPU)**: prepare known graphics before playing, reuse cached shaders, and compile missing shaders in parallel. Choose when to prepare them in the launcher or feature menu.

- **Animated effects**: apply texture movement and scaling so effects such as Necrid's flames animate instead of showing a static tiled texture.

## 0.2.1-beta.1

Online play on Windows works: hosting no longer closes the game when the other player comes. Both players need
this version to play each other online.

- **Online on Windows**: hosting closed the game when the other player came, and no match began. The host's game
  starts over for the match, and on Windows the new copy was given its arguments without quotes, so a path with a
  space in it (the game's own save folder has one) came apart and the copy never started. It now starts with each
  argument quoted, and waits a moment for the copy before it to let go of the save.

## 0.2.0-beta.1

Soulcalibur II on Android phones and tablets, sharper pictures for 4K screens (up to 8x the Xbox's resolution),
steadier frames on 120 Hz Macs, an FPS counter, the character select laid out around the guests, and *Unlock
everything* now kept in your save. Both players need this version to play each other online.

- **Unlock everything is kept** ([#5](https://github.com/tmcleroy/soulcalibur2-recomp/issues/5)): the cheat
  unlocked everything only until the game was closed. The game saves its unlocks from a packed copy, which the
  cheat left as it was; it now updates that too, so the next save keeps everything.
- **Resolutions up to 8x**: 5x (3200x2400), 6x (3840x2880) and 8x (5120x3840) on the Graphics page, for 4K
  screens and fast graphics cards.

  ![Link and Heihachi at 8x (5120x3840), a part of the picture at full size](https://raw.githubusercontent.com/tmcleroy/soulcalibur2-recomp/v0.2.0-beta.1/docs/screenshots/resolution-8x.jpg)
- **Steadier on 120 Hz screens (Mac)**: the game's 60 frames a second now follow the display's refreshes, so on a
  120 Hz (or 60, 180, 240 Hz) screen each frame stays up for the same number of refreshes. Before, about one frame in
  thirty was shown for one refresh and the next for three, a small hitch every second or two.
- **FPS counter**: frames shown each second in the top right corner, on the Graphics page and in the in-game menu
  (off at first).

  ![The FPS counter in the top right corner of a fight](https://raw.githubusercontent.com/tmcleroy/soulcalibur2-recomp/v0.2.0-beta.1/docs/screenshots/fps-counter.jpg)
- **Android**: the launcher and the game in one app (`Soulcalibur2Recomp-android.apk`, 64-bit, Android 10 or later),
  with touch controls that are player 1's pad and work the in-game menu; Android's Back opens the menu. The discs
  you choose are copied into the app's folder.

  ![Soulcalibur II on Android with the touch controls](https://raw.githubusercontent.com/tmcleroy/soulcalibur2-recomp/v0.2.0-beta.1/docs/screenshots/android-touch.jpg)
- **Character select centred with the guests**: the grid, which the guests' fifth column made half a column
  wider on the right, is centred again, clear of player 2's model.
- **Character select in rows of five** ([#6](https://github.com/tmcleroy/soulcalibur2-recomp/issues/6)): with the
  guests, the 25 characters fill five rows of five, Link and Heihachi beside Spawn, and Random sits alone under the
  middle column. Down from the bottom of any column, or Up from the top row, goes to Random.

  ![Link and Heihachi on VS Battle's character select, the grid in rows of five](https://raw.githubusercontent.com/tmcleroy/soulcalibur2-recomp/v0.2.0-beta.1/docs/screenshots/select-link-heihachi.jpg)

## 0.1.0-beta.2

The first public release. Online play is smoother and easier to start: fights run at full speed on a Steam Deck, a host keeps playing while waiting for someone to join, and joining explains itself when the two players' guests differ. Also new: RetroArch's CRT and scanline shaders. Both players need this version to play each other online, and guests built by beta.1 should be rebuilt (the launcher's Guest page asks).

- **Licenses in every download**: each archive carries `THIRD_PARTY_NOTICES.md` and the third parties' license texts.
- **Online: a host plays while waiting**: hosting (from the launcher or the menu) no longer takes the controls away
  until someone joins; the game starts over for the match when they do.
- **Online: guests that differ are named**: joining a match whose guest characters were built differently (or on one
  machine only) says so, instead of "different builds".
- **Online: full speed in fights**: a fight saved the whole game's memory every frame for rollback, which held a Steam
  Deck (and its opponent) to about 48 frames a second; it is now saved only on frames played on a guess of the other
  player's controls.
- **Online: T opens the chat again**: the key was taken as already held before it was looked at.
- **Guests: a version stamp**: the guests' file records which release's converter built it, and the Guest page asks
  for a Rebuild when it is from an earlier one (online play needs both players' guests from the same).
- **Shaders**: RetroArch's CRT, scanline and edge-smoothing shaders (CRT Royale, CRT Guest, CRT Lottes and more),
  or your own preset, on the Graphics page and in the in-game menu.
- **Run-ahead removed**: it cost too much speed, even on fast machines. A `run_ahead` line left in `settings.ini`
  does nothing.

## 0.1.0-beta.1

The first release: Soulcalibur II (Xbox) as a native program for macOS and Linux (Steam Deck and Steam Machine
included), with the GameCube's and PlayStation 2's guests beside Spawn, a training mode, online play with
rollback, a free camera and widescreen.

- **Guests**: Link (from your GameCube disc) and Heihachi (from your PlayStation 2 disc), converted on your machine,
  stand beside Spawn on the select screens, in Arcade (to their own endings), VS, the Extra modes, Weapon Master and
  the Museum, with their profiles unlocked by clearing Arcade.
- **Training**: hit boxes, frame data with a frame meter, an input display, a combo readout, start over, a dummy
  that records and replays, health refill and infinite time; save states, pause, frame advance and speeds from 1/8
  to 8x.
- **Online play**: host from the launcher or the in-game menu, join with a code, from the lobby or by invitation
  from the launcher's waiting room; rollback in fights, the players' names, ping, chat (T), win streaks, and
  reconnecting after a drop.
- **Run-ahead** (1 or 2 frames): fights shown sooner after a press.
- **Free camera and photo mode**: fly the camera in fights and menus, pause and step the fight, hide the HUD (the
  picture then fills the screen) and take screenshots.
- **Widescreen** (the game's own 16:9) and resolutions up to 4x; an endless CPU battle; cheats; an in-game menu; a
  launcher that checks your disc, builds the guests, sets features and controls, and updates itself.
