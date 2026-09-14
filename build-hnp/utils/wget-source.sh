#!/bin/sh
# Download DEST from URL. If the URL is a GNU /gnu/<pkg>/<file> tarball and
# the first host fails (ftpmirror.gnu.org/gnu/... 302s to <mirror>/gnu//gnu/
# and 404s), retry known-good hosts that serve the same layout.
set -eu

if [ "$#" -lt 2 ]; then
  echo "usage: wget-source.sh DEST URL" >&2
  exit 2
fi

DEST=$1
URL=$2
# Word-splitting of WGET is intentional (command + flags).
# shellcheck disable=SC2086
WGET="${WGET:-wget --retry-connrefused --read-timeout=20 --timeout=15}"
# Hosts that publish https://<host>/gnu/<pkg>/<file>
FALLBACKS="${GNU_MIRROR_FALLBACKS:-https://ftp.gnu.org https://mirrors.kernel.org}"

try_wget() {
  src=$1
  echo "wget-source: fetching $src"
  rm -f "$DEST"
  if $WGET -O "$DEST" "$src" && [ -s "$DEST" ]; then
    return 0
  fi
  rm -f "$DEST"
  return 1
}

if try_wget "$URL"; then
  exit 0
fi

# Keep /gnu/<pkg>/<file> so ftp.gnu.org and kernel.org layouts match.
gnu_path=$(printf '%s' "$URL" | sed -n 's|^[a-z][a-z0-9+.-]*://[^/]*/gnu/|/gnu/|p')
if [ -n "$gnu_path" ]; then
  for base in $FALLBACKS; do
    candidate="$base$gnu_path"
    if [ "$candidate" = "$URL" ]; then
      continue
    fi
    if try_wget "$candidate"; then
      exit 0
    fi
  done
  # ftpmirror.gnu.org expects /<pkg>/<file>, not /gnu/<pkg>/<file>.
  ftpmirror_path=${gnu_path#/gnu}
  if try_wget "https://ftpmirror.gnu.org$ftpmirror_path"; then
    exit 0
  fi
fi

echo "wget-source: all mirrors failed for $URL" >&2
exit 1
