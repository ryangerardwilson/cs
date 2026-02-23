#!/bin/sh
set -eu

SPINNER_PID=""

start_spinner() {
  msg="$1"
  spinner='-|/'
  i=1
  printf "%s " "$msg"
  (
    while :; do
      char=$(printf "%s" "$spinner" | cut -c "$i")
      printf "\r%s %s" "$msg" "$char"
      i=$((i + 1))
      if [ "$i" -gt 3 ]; then i=1; fi
      sleep 0.1
    done
  ) &
  SPINNER_PID=$!
}

stop_spinner() {
  msg="$1"
  if [ -n "$SPINNER_PID" ]; then
    kill "$SPINNER_PID" >/dev/null 2>&1 || true
    wait "$SPINNER_PID" 2>/dev/null || true
    SPINNER_PID=""
  fi
  printf "\r%s\n" "$msg"
}

run_step() {
  msg="$1"
  shift
  log="$(mktemp)"
  start_spinner "$msg"
  if "$@" >"$log" 2>&1; then
    stop_spinner "$msg ... done"
    rm -f "$log"
    return 0
  fi
  stop_spinner "$msg ... failed"
  cat "$log" >&2
  rm -f "$log"
  return 1
}

run_step_capture() {
  msg="$1"
  out_var="$2"
  shift 2
  log="$(mktemp)"
  err="$(mktemp)"
  start_spinner "$msg"
  if "$@" >"$log" 2>"$err"; then
    stop_spinner "$msg ... done"
    if [ -s "$err" ]; then
      cat "$err" >&2
    fi
    eval "$out_var=\"$(cat "$log")\""
    rm -f "$log" "$err"
    return 0
  fi
  stop_spinner "$msg ... failed"
  cat "$err" >&2
  rm -f "$log" "$err"
  return 1
}

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

if command -v python3 >/dev/null 2>&1; then
  if [ -n "$TOKEN" ]; then
    run_step_capture "Fetching release metadata" URLS sh -c "GITHUB_TOKEN=\"$TOKEN\" curl -fsSL \"$SCRIPT_URL\" | GITHUB_TOKEN=\"$TOKEN\" python3 - \"$API_URL\" \"$ASSET\" \"$CHECKSUM\"" || exit 1
  else
    run_step_capture "Fetching release metadata" URLS sh -c "curl -fsSL \"$SCRIPT_URL\" | python3 - \"$API_URL\" \"$ASSET\" \"$CHECKSUM\"" || exit 1
  fi
  URL=$(printf "%s" "$URLS" | sed -n '1p')
  CHECKSUM_URL=$(printf "%s" "$URLS" | sed -n '2p')
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
run_step "Downloading cs" curl -fsSL -o "$TMP_BIN" "$URL" || exit 1
run_step "Downloading checksum" curl -fsSL -o "$TMP_SUM" "$CHECKSUM_URL" || exit 1

EXPECTED="$(printf "%s" "$(cat "$TMP_SUM")" | awk '{print $1}')"
if command -v sha256sum >/dev/null 2>&1; then
  ACTUAL="$(sha256sum "$TMP_BIN" | awk '{print $1}')"
elif command -v shasum >/dev/null 2>&1; then
  ACTUAL="$(shasum -a 256 "$TMP_BIN" | awk '{print $1}')"
else
  echo "Missing sha256sum/shasum" >&2
  exit 1
fi

run_step "Verifying checksum" sh -c "[ \"$EXPECTED\" = \"$ACTUAL\" ]" || { echo "Checksum mismatch" >&2; exit 1; }

INSTALL_DIR="${CS_INSTALL_DIR:-$HOME/.local/bin}"
run_step "Installing cs" sh -c "mkdir -p \"$INSTALL_DIR\" && chmod +x \"$TMP_BIN\" && mv \"$TMP_BIN\" \"$INSTALL_DIR/cs\"" || exit 1

rm -f "$TMP_SUM"
echo "Installed cs to $INSTALL_DIR/cs"
