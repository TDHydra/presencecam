#!/bin/bash
# Builds every firmware image and the web flasher site, and with --publish puts them on GitHub
# from this machine (GitHub Actions isn't used, so it costs no Actions minutes).
#
#   tools/build.sh             tests, every firmware image, then the site in _site/
#   tools/build.sh --publish   the same, then: push main, tag v<VERSION>, create/update the
#                              GitHub release with the firmware, deploy _site/ to GitHub Pages
#                              (the gh-pages branch). Needs a clean, committed tree and `gh` logged in.
#   ARDUINO2_CORE_DIR=/path tools/build.sh   core 2 packages elsewhere (default <core>-arduino2)
#
# The core 3 envs build with `pio run`, then the core 2 envs in their own PlatformIO core and
# build directory (the two platforms' packages clash).
set -e
cd "$(dirname "$0")/.."

publish=0
for a in "$@"; do
  case "$a" in
    --publish) publish=1 ;;
    *) echo "unknown option $a (see the top of $0)"; exit 2 ;;
  esac
done

version="$(tr -d '[:space:]' < VERSION)"
tag="v$version"

if [ $publish = 1 ]; then
  # Check before an hour of building, not after
  git diff --quiet && git diff --cached --quiet || { echo "publish: commit your changes first"; exit 1; }
  [ "$(git rev-parse --abbrev-ref HEAD)" = main ] || { echo "publish: not on main"; exit 1; }
  gh auth status >/dev/null 2>&1 || { echo "publish: gh is not logged in (GH_CONFIG_DIR?)"; exit 1; }
  if git rev-parse -q --verify "refs/tags/$tag" >/dev/null && [ "$(git rev-parse "$tag^{commit}")" != "$(git rev-parse HEAD)" ]; then
    echo "publish: $tag already tags another commit; bump VERSION first"; exit 1
  fi
fi

node --test site/test/*.test.mjs
pio run
PLATFORMIO_CORE_DIR="${ARDUINO2_CORE_DIR:-${PLATFORMIO_CORE_DIR:-$HOME/.platformio}-arduino2}" \
  PLATFORMIO_BUILD_DIR=.pio/build-core2 \
  pio run -e esp32-core2 -e esp32s3-core2 -e esp32c3-core2
python3 tools/make_site.py .pio/build _site

[ $publish = 1 ] || exit 0

repo="$(gh repo view --json nameWithOwner -q .nameWithOwner)"
echo "publish: $tag to $repo"

git push origin main
git rev-parse -q --verify "refs/tags/$tag" >/dev/null || git tag -a "$tag" -m "PresenceCam $version"
git push origin "$tag"

if gh release view "$tag" >/dev/null 2>&1; then
  gh release upload "$tag" _site/firmware/*.bin --clobber
else
  gh release create "$tag" _site/firmware/*.bin --title "PresenceCam $version" --generate-notes
fi

# Pages: the site goes to the gh-pages branch as a single commit (no history to grow)
pages="$(mktemp -d)"
trap 'rm -rf "$pages"' EXIT
cp -r _site/. "$pages"
touch "$pages/.nojekyll"  # serve the files as they are
git -C "$pages" init -q -b gh-pages
git -C "$pages" add -A
git -C "$pages" -c user.name="$(git config user.name)" -c user.email="$(git config user.email)" \
  commit -qm "Web flasher for $tag"
git -C "$pages" push -qf "$(git remote get-url origin)" gh-pages

# Point Pages at that branch (once; it was deployed by the Actions workflow before)
if [ "$(gh api "repos/$repo/pages" -q '.build_type + " " + .source.branch')" != "legacy gh-pages" ]; then
  gh api -X PUT "repos/$repo/pages" -f build_type=legacy -f 'source[branch]=gh-pages' -f 'source[path]=/' >/dev/null
fi
echo "publish: done. Release https://github.com/$repo/releases/tag/$tag, site $(gh api "repos/$repo/pages" -q .html_url)"
