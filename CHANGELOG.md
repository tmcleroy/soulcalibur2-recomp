# Changelog

## 0.7.0-beta.1

Fixes and additions from players' reports: Link and Heihachi in Survival's and Time Attack's guest battles,
widescreen in its right shape, the other consoles' title screens, a choice of which gamepad plays which player, and
clearer messages when the guests can't be built. Build the guests again after updating (the launcher asks): the
other consoles' title screens come from them.

- **Which gamepad is which player's**: the launcher's Controllers page gives each connected gamepad Automatic,
  Player 1, Player 2 or Not used, and the game plays them so. A Steam Deck's own controls can be Not used, so two
  gamepads plugged into it play each other. With nothing chosen the gamepads play in the order connected, as before
  (recomp-common [#99](https://github.com/tmcleroy/recomp-common/pull/99)).
- **Link and Heihachi on their own stage in Survival** ([#112](https://github.com/tmcleroy/soulcalibur2-recomp/issues/112)):
  Extra Survival's courses fight Spawn at battles 10 and 40, on his stage, where the GameCube's fight Link and the
  PlayStation 2's Heihachi. Those battles are now one of the three, one not met yet in the run first, on that stage.
  As on the consoles, they are not drawn as Survival's other opponents, so the guests are met only in those
  battles, in the Chapel ([#112](https://github.com/tmcleroy/soulcalibur2-recomp/issues/112)).
- **Link and Heihachi on the Survival results** ([#112](https://github.com/tmcleroy/soulcalibur2-recomp/issues/112)):
  the results screen's beaten characters now show them when they were beaten.
- **Link and Heihachi count in the results' wins**: a run's total of wins on the results screen (Survival's and the
  others') left out the wins over Link and Heihachi, so 8 wins with one over Link showed 7.
- **Link and Heihachi in Extra Time Attack**: two of its courses fight Spawn at one battle, where the GameCube's
  fight Link and the PlayStation 2's Heihachi. That battle is now one of the three, as in Extra Survival.
- **Building the guests says what is wrong with a disc**: a GameCube or PlayStation 2 disc image that is another
  version, changed by a tool (trimmed, NKit, re-packed) or cut short now gets a message saying so (and, for a
  GameCube image that fails to convert, how it differs from the retail disc), where it stopped with "not a VGT bank"
  or "olnk entry cut short". On Android, a disc the system could not read whole is no longer taken as copied.
- **Widescreen in its right shape**: in the game's 16:9 mode the menus, screens and the fight's HUD were stretched by
  a third, and the fights about 6% too wide. The 2D now keeps its 4:3 shape in the middle of the picture (backgrounds
  and fades still fill it), and fights are shown at true 16:9.
- **The GameCube's and PlayStation 2's title screens**: with the guests built from those discs, the title screen
  shows the Xbox's, the GameCube's or the PS2's version of its picture at random, each with its own logo and
  copyright lines. Which of the three pictures it is still follows your save, as on every console. The guests need
  building again for it (the launcher asks).

## 0.6.0-beta.1

Fixes from players' reports.

- **No more "code 2" saving in Weapon Master** ([#103](https://github.com/tmcleroy/soulcalibur2-recomp/issues/103)):
  the game could stop with `kernel:user-apc-queue-full` as it saved. A part of the game that reads files kept up to 64
  finished reads waiting to be handled, and stopped at the 65th; like the Xbox, it now keeps as many as come.
- **The mouse cursor hides** ([#102](https://github.com/tmcleroy/soulcalibur2-recomp/issues/102)) over the game's window
  once the mouse has been still for 2 seconds, and comes back when you move it.
- **A Discord link** at the top of the launcher's Online page: where to find players for a match, tell us what to fix
  or add, and hear about new versions first.
- **The Museum's Character Ending and the bonus fighters** ([#91](https://github.com/tmcleroy/soulcalibur2-recomp/issues/91)):
  Lizardman, Assassin and Berserker have no endings of their own, but with Heihachi's last weapons had (or everything
  unlocked from the menu) the Museum offered Lizardman's, and choosing it showed nothing. Clearing Arcade with Lizardman also no longer gives Heihachi a weapon.
- **The bonus fighters in the Museum** ([#96](https://github.com/tmcleroy/soulcalibur2-recomp/issues/96)):
  Lizardman, Assassin and Berserker are left out of Character Profiles' and Character Ending's character select (their
  cells empty, as on the other consoles). They have neither a profile nor an ending; Character Ending showed them
  greyed, as if still to be unlocked, and Character Profiles let them be chosen.
- **Link's and Heihachi's pictures in the Art Gallery** ([#96](https://github.com/tmcleroy/soulcalibur2-recomp/issues/96)):
  Heihachi's two illustrations are in Illustrations (a tenth column), as on the PlayStation 2, not in Hi-Res. Special
  has a third row with the GameCube's and the PlayStation 2's own versions of the two black-and-white pictures with
  Soul Edge and Soul Calibur (Link's and Heihachi's, where the Xbox has Spawn's). **Build the guests again** on the
  launcher's Guest page to get them.
- **Inferno against Link and Heihachi** ([#90](https://github.com/tmcleroy/soulcalibur2-recomp/issues/90)): the
  final boss fought both guests in the style it has for Spawn. It now takes the form each guest's own disc has for
  them (the GameCube's for Link, the PlayStation 2's for Heihachi). **Build the guests again** on the launcher's
  Guest page to get it; until then it fights as before. Online, both players need the rebuilt file.
- **The European disc stopping in fights and in Records**: on the European (PAL) disc, Arcade and Survival stopped with
  "unresolved indirect call to 0x0014baef" as fighters attacked, and so did opening Options > Records. The C
  library's memmove, whose last bytes go through a jump table that the European program's translation had one arm
  short of, is now copied natively on both discs.
- **Weapon Master's character select on the European disc** ([#97](https://github.com/tmcleroy/soulcalibur2-recomp/issues/97)):
  the 1P frame stayed on the original fighters with Link or Heihachi chosen, and opened on Raphael when one of them
  was the character already. The select's sprites were looked for at the US program's place; on the European disc
  that also wrote into memory nearby.
- **Total play time in Records** ([#97](https://github.com/tmcleroy/soulcalibur2-recomp/issues/97)) counts again. It
  stayed at 0:00:00: the game's count of vertical blanks, its clock, was kept by a callback the Xbox's graphics chip
  ran and the port did not. The effects that blink or cycle by that clock move with it.
- **Unlock everything** ([#97](https://github.com/tmcleroy/soulcalibur2-recomp/issues/97)) now also unlocks the
  characters' endings in the Museum's Character Ending.
- **Fades between screens** ([#97](https://github.com/tmcleroy/soulcalibur2-recomp/issues/97)): leaving a screen, the
  game keeps its last picture up and fades it to black while the next one loads. Until the first fight the port had
  no copy of that picture to keep, so leaving the title screen (and the other screens before a fight) cut to the next
  screen's first, half-drawn frame (the title's "PRESS START" alone on black) and faded that. Not yet on Windows.
- **Sounds fading out instead of cutting off** ([#97](https://github.com/tmcleroy/soulcalibur2-recomp/issues/97)):
  most of the game's sounds, voices among them (Inferno's death cry was the one noticed, its tail heard as reverb),
  are stopped by letting them fade over about two thirds of a second, the Xbox sound chip's volume envelope. The port
  had no envelope and cut them off at once. The envelope's other shapes (a slow fade to a lower level) are heard too.
- **The launcher's "Rebuild the guests" notice** now looks only at the guests built for the Xbox disc chosen. A file
  left from the other release's disc, chosen before, kept asking for a rebuild after this one was rebuilt.
- **Inferno's moves against Link and Heihachi** ([#90](https://github.com/tmcleroy/soulcalibur2-recomp/issues/90)):
  as against Spawn and Necrid, Inferno now fights them with Nightmare's moves, as it does on the GameCube and the
  PlayStation 2. It took each guest's own moves.

## 0.5.0-beta.1

The European disc on Windows and Linux and with the guests, online play with a chosen input delay and country flags,
stages drawn sharp and moving as on the Xbox, the shine on metal, the Art Gallery's pictures as files, and fixes for
Link's and Heihachi's weapons, the bonus fighters' endings and the European disc. Both players need this version to
play each other online.

- **The European disc on Windows and Linux, and with the guests** ([#60](https://github.com/tmcleroy/soulcalibur2-recomp/issues/60)):
  the PAL disc plays on Windows and Linux as on the Mac (it stopped with "soulcalibur2-pal isn't here"), and Link
  and Heihachi can be built for it too. Not yet on Android.
- **Android: the guest discs' copy** shows its progress, and says why when it fails.
- **Input delay for the matches you host**: the launcher's Online page has an **Input delay** choice under Host (0 to
  8 frames; 2 unless chosen), which the one who joins plays with too.
- **Country flags in the waiting room**: choose **Your country** on the Online page (none unless you do), and the
  others there see its flag and name beside yours. Flags from flag-icons (MIT).
- **Export pictures** ([#82](https://github.com/tmcleroy/soulcalibur2-recomp/issues/82)): the menu's new Pictures
  page writes the Art Gallery's pictures (all three galleries, the guests' too) as JPEG files to `pictures/` in your
  user folder, and opens that folder.
- **Stage textures stretched and blurry** ([#23](https://github.com/tmcleroy/soulcalibur2-recomp/issues/23)): floors,
  walls, grass and water on almost every stage were drawn with one copy of their texture pulled across the whole
  surface, where the game repeats it many times. They are now tiled, placed and scrolled as on the Xbox, so the
  stages are as sharp as the original's and their water moves.
- **A thin strip along the bottom of the select and versus screens**: above the Xbox's resolution the last
  half line of the picture showed the fighters' portraits without the screen's frame and fade over them. In a
  match the black bars above and below the picture had a thin line of the stage's backdrop colour along their
  edges. Both are gone.
- **The shine on metal** ([#23](https://github.com/tmcleroy/soulcalibur2-recomp/issues/23)): armour, blades,
  shields and other polished surfaces have their reflection highlights, which follow the camera. They were drawn
  flat, the reflection picture stuck to the surface like paint.

Fixes from the European disc and Linux test builds ([#77](https://github.com/tmcleroy/soulcalibur2-recomp/issues/77)).

- **Link's and Heihachi's weapons in Weapon Master's shops and the Museum's Weapon Gallery**: each of their weapons
  is for sale beside Spawn's and has its row in the gallery. They are now their own: a weapon bought for one guest
  is no longer had by the other two, so **weapons Link and Heihachi had through Spawn's have to be bought again**
  (or use *Unlock everything*). What is bought is kept in the game's own save.
- **Weapon Master's character select**: its frame takes Link and Heihachi in, where they stood outside it.
- **A weapon effect icon drawn as noise** (European disc, with the guests): one of the effect icons on the Extra
  modes' weapon choice was a square of static for every character whose weapon has it.
- **"Press B to delete unusable data" ended in a black screen**: the game left for the Xbox Dashboard, which is not
  there. The save it cannot read (the other release's disc's) is now moved to a `saves set aside` folder beside the
  saves and the game starts over.
- **The in-game menu with a keyboard**: each key worked once, then not again until the window lost and regained the
  keyboard (seen on Linux; the same on every system). A gamepad was not affected.
- **Weapon Master above full speed**: starting a mission with the speed above Full stopped the game.
- **Arcade endings for Lizardman, Assassin and Berserker** ([#65](https://github.com/tmcleroy/soulcalibur2-recomp/issues/65)):
  all three got Spawn's ending. As on the GameCube and PlayStation 2, Lizardman and Assassin now have none (the game
  goes on to Game Over and the name entry), and Berserker gets Astaroth's.

## 0.4.0-beta.1

The bonus fighters unlock as on the Xbox, the menus' lettering in HD from the texture pack, weapon swing sounds, and
fixes for stuttering and repeating sound. Both players need this version to play each other online.

- **Assassin, Berserker and Lizardman** ([#47](https://github.com/tmcleroy/soulcalibur2-recomp/issues/47)): once
  unlocked (in Weapon Master), they can be picked in every mode, and their Arcade plays to the end. The character
  select screen's small portraits no longer show a seam and a doubled step beside Random in widescreen.
- **Sound stuttering** ([#54](https://github.com/tmcleroy/soulcalibur2-recomp/issues/54)): sound could stick on one
  short piece and repeat it for several seconds (most often after a pause, in busy fights). It plays on now.
- **HD menu lettering** ([#56](https://github.com/tmcleroy/soulcalibur2-recomp/issues/56)): the HD texture pack
  import now covers the menus' lettering (the titles, choices and help lines on every screen), in all 16 of its
  colours: 9 from the pack, the rest made from the pack's grey lettering in the game's own colours, read from your
  Xbox disc. Import the pack again on the Textures page to get it.
- **Weapon swing sounds**: the whoosh of a swing is heard again. Its pitch moves every frame, and each change started
  the sound over on silence, so it never played (on every system).
- **Music repeating a phrase** ([#32](https://github.com/tmcleroy/soulcalibur2-recomp/issues/32),
  [#34](https://github.com/tmcleroy/soulcalibur2-recomp/issues/34)): when the game was held up for a moment (the
  Steam overlay opening, a slow card, the system taking a while to hand back the window's picture), the music went on
  from part of the track it had already played and repeated it. Now the music waits and goes on from where it was.
- **Link's hair**: the locks framing his face are drawn again, fixing the dark gaps near his temples in character select and fights. The guests' file is rebuilt once.

## 0.3.0-beta.1

The GameCube HD texture pack, the European disc in its five languages on the Mac, and fixes: Linux starts without
shaderc installed, Heihachi's hair, and a Museum freeze. Both players need this version to play each other online.

- **HD textures** ([#11](https://github.com/tmcleroy/soulcalibur2-recomp/issues/11)): the GameCube version's HD
  texture pack for Dolphin (*Soulcalibur 2 Complete HD Textures*), imported from your own download on the launcher's
  new Textures page and matched to the Xbox's textures (about 4,300 of them: characters, Link included, stages,
  menus), drawn while **HD textures** is on (it also switches in the in-game menu, while playing). **Dump textures**
  writes each texture the game draws to a folder, for making your own pack.
- **The European disc** ([#17](https://github.com/tmcleroy/soulcalibur2-recomp/issues/17)): the launcher also takes
  the PAL (Europe, Australia) disc, which plays in English, German, French, Spanish or Italian (the **Language**
  feature). For now on the Mac only, and without the guests.
- **Linux: the game starts without shaderc installed** ([#40](https://github.com/tmcleroy/soulcalibur2-recomp/issues/40)):
  on a system without the shaderc library (Debian and Ubuntu without `libshaderc1`, Arch without `shaderc`) the
  game stopped at once with "code 2". The Linux folder now carries its own `libshaderc_shared.so.1`.
- **Heihachi's hair**: his white hair no longer shows teal and yellow highlights (the PlayStation 2 disc's material
  colours were read from the wrong place). The launcher asks to rebuild the guests' file once. Thanks to Unitghit
  ([#24](https://github.com/tmcleroy/soulcalibur2-recomp/pull/24)).
- **No more freeze in the Museum on a new save**: with the guest characters loaded, Mode Select showed Museum
  before anything in it was unlocked, and pressing Down in its empty menu froze the game. The Museum now shows once
  something in it is unlocked, as on the Xbox.

## 0.2.2-beta.1

A fix for the game closing with "The game stopped with code 2" as a fight loads, and clearer messages when the game
stops: what happened, in plain words, which log to send and where. Both players need this version to play each
other online.

- **No more "code 2" as a fight loads** ([#20](https://github.com/tmcleroy/soulcalibur2-recomp/issues/20),
  [#22](https://github.com/tmcleroy/soulcalibur2-recomp/issues/22)): the game closed itself when a sound reached a
  piece of data it couldn't decode (seen before Inferno in Arcade and in Weapon Master). That piece now plays as
  silence, as on the Xbox, and the game goes on.
- **When the game stops, the launcher says why and what to do**: what the code means in plain words (a part of the
  game this version can't handle yet, or a crash), the log of that run kept as `logs/stopped-<date-time>.log` (the
  game's log is written over at every start), **Show the log**, and **Report on GitHub**, which opens a new issue
  with its title filled in. Started straight from a Steam shortcut, the game shows the same in a box.

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
