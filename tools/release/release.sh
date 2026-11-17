#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# A whole release, from main to the published GitHub release, in one command (docs/RELEASING.md):
#
#   tools/release/release.sh minor          0.5.0-beta.1 -> 0.6.0-beta.1
#   tools/release/release.sh patch          0.5.0-beta.1 -> 0.5.1-beta.1
#   tools/release/release.sh 1.0.0          any version
#
# Options:
#   --draft       stop at the draft, with its files uploaded (publish it later: --skip-build VERSION)
#   --yes         ask nothing: no pause to play the builds, no question before publishing
#   --skip-build  the archives in build/ are already this version's (made by an earlier run that stopped)
#
# Run it on a Mac, in a checkout of main with the game's files in private/ (README.md, Building). It stops at the
# first thing that fails and can be run again with the same version: every step already done is skipped.
#
#   1. checks: the tools, a clean main up to date with GitHub, the discs, the Android key, the changelog
#   2. the version: VERSION, and CHANGELOG.md's "## Unreleased" renamed to "## <version>"; committed, not pushed
#   3. toolchains: shaderc for Linux and Windows, llvm-mingw, the Docker image (each once)
#   4. the lifts (US and European discs), redone only if out of date
#   5. the archives: macOS, Linux and Windows (in Docker) and Android, with their update manifests; SHA256SUMS.txt
#   6. a pause to play each build
#   7. main pushed and its CI waited for; the tag pushed and the Release workflow's draft waited for
#   8. the files uploaded to the draft, and the draft published
set -eu
root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$root"
repo=tmcleroy/soulcalibur2-recomp
draft= yes= skip_build= want=
for arg in "$@"; do
    case "$arg" in
        --draft) draft=1 ;;
        --yes) yes=1 ;;
        --skip-build) skip_build=1 ;;
        -h|--help) sed -n '3,24p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        -*) echo "error: unknown option $arg" >&2; exit 2 ;;
        *) [ -z "$want" ] || { echo "error: one version only" >&2; exit 2; }; want=$arg ;;
    esac
done
[ -n "$want" ] || { echo "usage: tools/release/release.sh [--draft] [--yes] [--skip-build] minor|patch|VERSION" >&2; exit 2; }

step() { printf '\n\033[1m== %s\033[0m\n' "$*"; }
fail() { echo "error: $*" >&2; exit 1; }
ask() { [ -n "$yes" ] && return 0; printf '%s [y/N] ' "$1"; read -r answer; [ "$answer" = y ] || [ "$answer" = Y ]; }

# --- 1. checks -------------------------------------------------------------------------------------------------------
step "Checks"
[ "$(uname -s)" = Darwin ] || fail "a release is made on a Mac (the macOS app and the lifts are built here)"
for tool in git gh docker python3 shasum curl xz zstd codesign ditto; do
    command -v "$tool" >/dev/null || fail "$tool is not installed"
done
gh auth status >/dev/null 2>&1 || fail "gh is not logged in (gh auth login)"
docker info >/dev/null 2>&1 || fail "Docker is not running"
[ -x private/venv/bin/python ] || fail "no private/venv (README.md, Building)"
[ -f private/disc/Default.xbe ] || fail "no private/disc/Default.xbe (the US disc's files, README.md)"
[ -f private/disc-pal/Default.xbe ] || fail "no private/disc-pal/Default.xbe (the European disc's files, README.md)"
signing="${SIGNING:-$HOME/.config/soulcalibur2-recomp/android-signing.env}"
[ -f "$signing" ] || fail "no Android release key ($signing; docs/RELEASING.md)"
[ "$(git rev-parse --abbrev-ref HEAD)" = main ] || fail "not on main"
[ -z "$(git status --porcelain --untracked-files=no)" ] || fail "uncommitted changes"
git fetch -q origin main --tags
git submodule update --init --recursive -q

current=$(head -1 VERSION)
case "$want" in
    minor|patch)
        # the pre-release part stays (0.5.0-beta.1 -> 0.6.0-beta.1) until a version is given by hand
        core=${current%%-*} suffix=
        [ "$core" = "$current" ] || suffix=-$(echo "${current#*-}" | sed 's/[0-9][0-9]*$/1/')
        IFS=. read -r major minor patch <<EOF
$core
EOF
        if [ "$want" = minor ]; then version=$major.$((minor + 1)).0$suffix; else version=$major.$minor.$((patch + 1))$suffix; fi ;;
    *) version=$want ;;
esac
echo "$version" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.]+)?$' || fail "$version is not a version (1.2.3 or 1.2.3-beta.1)"
tag=v$version

# Already the release commit (an earlier run that stopped)? Then main may be one commit behind it, or level with it.
bumped=
if [ "$current" = "$version" ] && [ "$(git log -1 --format=%s)" = "Soulcalibur II Recomp $version" ]; then
    bumped=1
    [ "$(git rev-parse HEAD)" = "$(git rev-parse origin/main)" ] || [ "$(git rev-parse HEAD^)" = "$(git rev-parse origin/main)" ] ||
        fail "main has moved on from the release commit; reset it and run again"
else
    [ "$current" != "$version" ] || fail "VERSION is already $version but HEAD isn't its release commit"
    [ "$(git rev-parse HEAD)" = "$(git rev-parse origin/main)" ] || fail "main is not level with origin/main (pull or push first)"
fi
if published=$(gh release view "$tag" -R "$repo" --json isDraft --jq .isDraft 2>/dev/null) && [ "$published" = false ]; then
    fail "$tag is already published"
fi
echo "releasing $version (from $current)"

# --- 2. the version --------------------------------------------------------------------------------------------------
step "Version $version"
if [ -z "$bumped" ]; then
    if ! grep -qx "## $version" CHANGELOG.md; then
        grep -qx '## Unreleased' CHANGELOG.md || fail "CHANGELOG.md has no \"## Unreleased\" section (nor \"## $version\")"
        awk -v v="## $version" '!done && $0 == "## Unreleased" { print v; done = 1; next } { print }' CHANGELOG.md > CHANGELOG.md.new
        mv CHANGELOG.md.new CHANGELOG.md
    fi
    if ! tools/release/notes.sh "$version" > /dev/null; then
        git checkout -q CHANGELOG.md
        fail "CHANGELOG.md's section needs a paragraph above its list saying what the release is (docs/RELEASING.md)"
    fi
    echo "$version" > VERSION
    git commit -q -m "Soulcalibur II Recomp $version" VERSION CHANGELOG.md
    echo "committed $(git log -1 --format='%h %s') (not pushed yet)"
else
    echo "already committed: $(git log -1 --format='%h %s')"
fi

if [ -z "$skip_build" ]; then
    # --- 3. toolchains -----------------------------------------------------------------------------------------------
    step "Toolchains"
    tools/release/fetch_shaderc_linux.sh
    tools/release/fetch_shaderc_windows.sh
    arch=$(docker info --format '{{.Architecture}}')
    tools/release/fetch_llvm_mingw.sh "$arch"
    docker build -q --platform linux/amd64 -t soulcalibur2-build:amd64 tools/release
    docker build -q -t soulcalibur2-build:native tools/release

    # --- 4. the lifts ------------------------------------------------------------------------------------------------
    step "Lifts"
    private/venv/bin/python tools/build_game.py current
    private/venv/bin/python tools/build_game.py --pal current

    # --- 5. the archives ---------------------------------------------------------------------------------------------
    rm -f build/Soulcalibur2Recomp-* build/update-*.txt build/SHA256SUMS.txt
    # Docker sees the checkout, and where private/'s links lead (a worktree's private/ often links to another's)
    mounts="-v $root:$root"
    for link in private/*; do
        [ -L "$link" ] || continue
        target=$(cd "$link" 2>/dev/null && pwd -P) || continue
        case "$target" in "$root"/*) ;; *) mounts="$mounts -v $target:$target" ;; esac
    done
    step "macOS"
    tools/release/macos.sh
    step "Linux (Docker, x86_64)"
    docker run --rm --platform linux/amd64 $mounts -w "$root" soulcalibur2-build:amd64 tools/release/linux.sh
    step "Windows (Docker, llvm-mingw)"
    docker run --rm $mounts -w "$root" soulcalibur2-build:native tools/release/windows.sh
    step "Android"
    RELEASE=1 SIGNING="$signing" tools/release/android.sh
fi

step "Archives"
files="Soulcalibur2Recomp-macOS.zip Soulcalibur2Recomp-linux-x86_64.tar.gz Soulcalibur2Recomp-windows-x64.zip
Soulcalibur2Recomp-android.apk update-macos.txt update-linux-x86_64.txt update-windows-x64.txt"
for f in $files; do [ -f "build/$f" ] || fail "build/$f is missing"; done
for f in build/update-*.txt; do
    grep -qx "version $version" "$f" || fail "$f is for $(sed -n 's/^version //p' "$f"), not $version (build again)"
done
(cd build && shasum -a 256 Soulcalibur2Recomp-macOS.zip Soulcalibur2Recomp-linux-x86_64.tar.gz \
    Soulcalibur2Recomp-windows-x64.zip Soulcalibur2Recomp-android.apk > SHA256SUMS.txt)
cat build/SHA256SUMS.txt

# --- 6. play them ----------------------------------------------------------------------------------------------------
if [ -z "$yes" ]; then
    step "Play each build before it goes out"
    echo "  macOS:   build/release-macos/Soulcalibur II Recomp.app"
    echo "  Linux:   build/Soulcalibur2Recomp-linux-x86_64.tar.gz (a Steam Deck or Linux PC)"
    echo "  Windows: build/Soulcalibur2Recomp-windows-x64.zip"
    echo "  Android: adb install -r build/Soulcalibur2Recomp-android.apk"
    ask "Do they all start and play?" || fail "stopped before pushing; nothing is public yet (run again when ready)"
fi

# --- 7. push, tag, the draft -----------------------------------------------------------------------------------------
# wait_run WORKFLOW REF SHA: wait for the workflow's run on that commit to start, then for it to pass
wait_run() {
    id=
    for _ in $(seq 60); do
        id=$(gh run list -R "$repo" --workflow "$1" --commit "$3" --json databaseId,headBranch \
            --jq "[.[] | select(.headBranch == \"$2\")][0].databaseId // empty")
        [ -n "$id" ] && break
        sleep 5
    done
    [ -n "$id" ] || fail "no $1 run for $2 started on GitHub"
    gh run watch -R "$repo" "$id" --exit-status --interval 30 > /dev/null || fail "$1 failed for $2: gh run view -R $repo $id --log-failed"
    echo "$1 passed for $2"
}
sha=$(git rev-parse HEAD)
step "Push main"
if [ "$(git rev-parse origin/main)" != "$sha" ]; then
    git push -q origin HEAD:main
fi
wait_run CI main "$sha"

step "Tag $tag"
if ! git ls-remote --exit-code --tags origin "refs/tags/$tag" >/dev/null; then
    git tag -f "$tag" "$sha" >/dev/null
    git push -q origin "$tag"
fi
gh release view "$tag" -R "$repo" >/dev/null 2>&1 || wait_run Release "$tag" "$sha"

# --- 8. upload and publish -------------------------------------------------------------------------------------------
step "Upload"
gh release upload "$tag" -R "$repo" --clobber $(for f in $files SHA256SUMS.txt; do echo "build/$f"; done)
url=$(gh release view "$tag" -R "$repo" --json url --jq .url)
if [ -n "$draft" ]; then
    echo "the draft is ready: $url (publish it with: tools/release/release.sh --skip-build $version)"
    exit 0
fi
step "Publish"
ask "Publish $tag for everyone (the launchers update to it)?" || { echo "left as a draft: $url"; exit 0; }
gh release edit "$tag" -R "$repo" --draft=false > /dev/null
echo "published $url"
