#!/bin/sh
set -eu

REPO="${CS_REPO:-}"
if [ -z "$REPO" ]; then
  if [ -n "${CS_REPO_OWNER:-}" ] && [ -n "${CS_REPO_NAME:-}" ]; then
    REPO="${CS_REPO_OWNER}/${CS_REPO_NAME}"
  fi
fi

if [ -z "$REPO" ] || [ "$REPO" = "/" ]; then
  echo "Set CS_REPO=owner/repo (or CS_REPO_OWNER + CS_REPO_NAME)" >&2
  exit 1
fi

OS="$(uname -s)"
ARCH="$(uname -m)"

case "$OS" in
  Linux) OS="linux" ;;
  Darwin) OS="darwin" ;;
esac

case "$ARCH" in
  x86_64) ARCH="amd64" ;;
  aarch64|arm64) ARCH="arm64" ;;
esac

ASSET="cs-${OS}-${ARCH}"
CHECKSUM="${ASSET}.sha256"
API="https://api.github.com/repos/${REPO}/releases/latest"

API_URL="$API"
TOKEN="${GITHUB_TOKEN:-${GH_TOKEN:-}}"

SCRIPT_URL="https://raw.githubusercontent.com/${REPO}/main/scripts/find-release-asset.py"

fetch_url() {
  NAME="$1"
  if [ -n "$TOKEN" ]; then
    GITHUB_TOKEN="$TOKEN" curl -fsSL "$SCRIPT_URL" | GITHUB_TOKEN="$TOKEN" python3 - "$API_URL" "$NAME"
  else
    curl -fsSL "$SCRIPT_URL" | python3 - "$API_URL" "$NAME"
  fi
}

if command -v python3 >/dev/null 2>&1; then
  URL="$(fetch_url "$ASSET")"
  CHECKSUM_URL="$(fetch_url "$CHECKSUM")"
else
  echo "python3 is required for install" >&2
  exit 1
fi

if [ -z "$URL" ] || [ -z "$CHECKSUM_URL" ]; then
  echo "Release asset not found for ${ASSET}" >&2
  exit 1
fi

TMP_BIN="$(mktemp)"
TMP_SUM="$(mktemp)"
curl -fsSL -o "$TMP_BIN" "$URL"
curl -fsSL -o "$TMP_SUM" "$CHECKSUM_URL"

EXPECTED="$(printf "%s" "$(cat "$TMP_SUM")" | awk '{print $1}')"
if command -v sha256sum >/dev/null 2>&1; then
  ACTUAL="$(sha256sum "$TMP_BIN" | awk '{print $1}')"
elif command -v shasum >/dev/null 2>&1; then
  ACTUAL="$(shasum -a 256 "$TMP_BIN" | awk '{print $1}')"
else
  echo "Missing sha256sum/shasum" >&2
  exit 1
fi

if [ "$EXPECTED" != "$ACTUAL" ]; then
  echo "Checksum mismatch" >&2
  exit 1
fi

INSTALL_DIR="${CS_INSTALL_DIR:-$HOME/.local/bin}"
mkdir -p "$INSTALL_DIR"
chmod +x "$TMP_BIN"
mv "$TMP_BIN" "$INSTALL_DIR/cs"

rm -f "$TMP_SUM"
echo "Installed cs to $INSTALL_DIR/cs"
