#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

usage() {
  cat <<EOF
Usage: NEW_VERSION=X.Y.Z ./scripts/release.sh
       ./scripts/release.sh X.Y.Z

Requires clean git tree. Writes VERSION, regenerates version.h, runs tests,
commits, and tags vX.Y.Z. Does not push.
EOF
}

version="${NEW_VERSION:-${1:-}}"
if [[ -z "$version" ]]; then
  usage
  exit 1
fi

if [[ -n "$(git status --porcelain)" ]]; then
  echo "Refusing: git tree is dirty." >&2
  exit 1
fi

if git rev-parse -q --verify "refs/tags/v$version" >/dev/null; then
  echo "Tag v$version already exists." >&2
  exit 1
fi

echo "$version" > VERSION

# Build with override to avoid hash suffix during release.
VERSION_OVERRIDE="$version" make clean all test

git add VERSION
git commit -m "Release v$version"
git tag -a "v$version" -m "v$version"

cat <<EOF
Release prepared:
  version: $version
  commit: $(git rev-parse --short HEAD)
  tag: v$version

Next steps (manual):
  git push
  git push --tags
EOF

