# Releasing

A release's archives hold the game's program, translated from the player's own `Default.xbe`, so they are
built on a contributor's Mac, never by CI. CI (`.github/workflows/ci.yml`) proves on every push that the rest
builds; a tag makes a draft release with its text and no files (`release.yml`). One command does all of it:

```sh
tools/release/release.sh minor     # 0.5.0-beta.1 -> 0.6.0-beta.1 (new features)
tools/release/release.sh patch     # 0.5.0-beta.1 -> 0.5.1-beta.1 (fixes only)
tools/release/release.sh 1.0.0     # any version
```

## Before the first release on a Mac

- **The build**, as in the README's *Building*: Xcode's command-line tools, `brew install sdl3 shaderc spirv-cross
  ninja cmake gh xz zstd`, `private/venv`, the US disc's files in `private/disc/` and the European disc's in
  `private/disc-pal/`. `gh auth login` with push rights to the repository.
- **Docker** (Docker Desktop, running). The script builds its image from `tools/release/Dockerfile` and fetches
  shaderc and llvm-mingw into `private/toolchains` the first time.
- **Android**: `brew install openjdk@17 android-commandlinetools`, then `sdkmanager "platforms;android-34"
  "build-tools;34.0.0" "ndk;27.2.12479018"`.
- **The Android release key**, in `~/.config/soulcalibur2-recomp/android-signing.env` (mode 600, outside the
  repository), lines of `KEYSTORE=`, `KEYSTORE_PASSWORD=`, `KEY_ALIAS=`, `KEY_PASSWORD=`. A published APK must carry
  the same key every time or it won't install over the one before, so the keystore is backed up and never made anew;
  only a maintainer who has it can release.

## Each release

1. **The changelog.** Changes go under `## Unreleased` at the top of `CHANGELOG.md` as they are merged. Before
   releasing, give that section its opening paragraph (above the list): what the release is, in a sentence or two;
   it is the top of the release's page. `tools/release/notes.sh Unreleased` shows the text the release will have.
   Commit it to main.
2. **Run** `tools/release/release.sh minor` (or `patch`, or a version) on main. It:
   - checks the tools, that main is clean and level with GitHub, the discs, the key and the changelog;
   - sets `VERSION` (online play tells the other side this, so both players need the same) and renames
     `## Unreleased` to `## <version>`, as a commit `Soulcalibur II Recomp <version>`, not pushed yet;
   - lifts the US and European executables again if the lifter or `runtime/` changed since the last lift;
   - builds the four downloads in `build/`: `Soulcalibur2Recomp-macOS.zip`, `-linux-x86_64.tar.gz` and
     `-windows-x64.zip` (both in Docker) and `-android.apk` (release-signed), the three `update-*.txt` the launcher
     updates itself from (Android has none: its launcher hands the installer the newest release's APK), and
     `SHA256SUMS.txt`;
   - waits while you **play each build** (it lists where they are) and asks whether they work;
   - pushes main and waits for CI, pushes the tag `v<version>` and waits for the Release workflow's draft (it
     checks the tag is `VERSION` and the changelog has it);
   - uploads the files to the draft and asks before publishing it. The launcher's update looks at published
     releases only (pre-releases to pre-release versions).

Nothing is public until you answer the question after playing. If a step fails, fix it and run the same command
with the version it printed (`tools/release/release.sh 0.6.0-beta.1`): steps already done are skipped.
`--skip-build` uses the archives already in `build/`; `--draft` stops at the draft; `--yes` asks nothing.

## By hand

The script runs these, which also work alone (each says what it needs at its top):
`tools/build_game.py current` and `--pal current` (the lifts), `tools/release/macos.sh`, `linux.sh` and
`windows.sh` (in Docker, see the Dockerfile), `RELEASE=1 tools/release/android.sh`, and `tools/release/notes.sh
<version>` (the release's text).
