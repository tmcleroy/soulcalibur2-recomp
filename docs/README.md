# Documentation

For players:

- [PLAYING.md](PLAYING.md): the launcher, the controls, the in-game menu (save states, speed, pause), training, the
  features, the free camera, the guests and online play.

How the port works:

- [bring-up.md](bring-up.md): the disc and executable, the lift, the library functions, Direct3D and DirectSound,
  boot, input and saves, the test aids and the scripted runs.
- [runtime.md](runtime.md): the runtime it shares with the DOA ports, what this port puts in place of its files, and
  the rules for changing it.
- [guests.md](guests.md): the three discs' containers, how Link and Heihachi are converted from the GameCube and
  PlayStation 2 discs, and where the game takes them in (select, Arcade, endings, weapons, Weapon Master, the Museum).
- [online.md](online.md): connecting, rollback and lockstep, the in-game Online page, coming back after a drop, and
  what had to be made deterministic for two machines to play alike.
- [RELEASING.md](RELEASING.md): CI, and the one command that builds and publishes a release (`tools/release/release.sh`).

The pictures are in [screenshots](screenshots).
