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

# zlib.net and similar hosts can 200 an HTML interstitial (~12KB) instead of
# the tarball. Reject HTML/text so make does not treat that as a source archive.
is_source_archive() {
  path=$1
  mime=$(file -b --mime-type "$path" 2>/dev/null || echo unknown)
  case "$mime" in
    text/html|text/xml|application/xhtml+xml|text/plain)
      echo "wget-source: rejecting $path ($mime, not an archive)" >&2
      return 1
      ;;
  esac
  # Magic fallback when `file` is generic (application/octet-stream).
  sig=$(od -An -tx1 -N 6 "$path" 2>/dev/null | tr -s ' ' | sed 's/^ //')
  case "$sig" in
    1f\ 8b*|fd\ 37\ 7a\ 58\ 5a\ 00*|42\ 5a\ 68*|50\ 4b\ 03\ 04*)
      return 0
      ;;
  esac
  case "$mime" in
    application/gzip|application/x-gzip|application/x-xz|application/x-bzip2|application/zip|application/x-tar|application/octet-stream)
      return 0
      ;;
  esac
  echo "wget-source: rejecting $path (mime=$mime sig=$sig)" >&2
  return 1
}

try_wget() {
  src=$1
  echo "wget-source: fetching $src"
  rm -f "$DEST"
  if $WGET -O "$DEST" "$src" && [ -s "$DEST" ] && is_source_archive "$DEST"; then
    return 0
  fi
  rm -f "$DEST"
  return 1
}

if try_wget "$URL"; then
  exit 0
fi

# Optional extra URLs (space-separated), e.g. zlib GitHub + fossils.
for extra in ${SOURCE_URL_FALLBACKS:-}; do
  if [ "$extra" = "$URL" ]; then
    continue
  fi
  if try_wget "$extra"; then
    exit 0
  fi
done

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
