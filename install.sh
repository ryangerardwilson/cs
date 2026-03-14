#!/usr/bin/env bash
set -euo pipefail

APP="cs"
DEFAULT_REPO="ryangerardwilson/${APP}"
REPO="${CS_REPO:-$DEFAULT_REPO}"
if [[ -z "${CS_REPO:-}" && -n "${CS_REPO_OWNER:-}" && -n "${CS_REPO_NAME:-}" ]]; then
  REPO="${CS_REPO_OWNER}/${CS_REPO_NAME}"
fi
INSTALL_DIR="${CS_INSTALL_DIR:-$HOME/.local/bin}"

usage() {
  cat <<EOF
${APP} Installer

Usage: install.sh [options]

Options:
  -h                         Show this help and exit
  -v [<version>]             Install a specific release (e.g. 0.1.0 or v0.1.0)
                             Without an argument, print the latest release version and exit
  -u                         Upgrade to the latest release only when newer
  -b, --binary <path>        Install from a local binary instead of downloading
      --no-modify-path       Skip editing shell rc files
EOF
}

die() {
  echo "install.sh: $*" >&2
  exit 1
}

requested_version=""
show_latest=false
upgrade=false
binary_path=""
no_modify_path=false
latest_version_cache=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help)
      usage
      exit 0
      ;;
    -v|--version)
      if [[ -n "${2:-}" && "${2:0:1}" != "-" ]]; then
        requested_version="${2#v}"
        shift 2
      else
        show_latest=true
        shift
      fi
      ;;
    -u|--upgrade)
      upgrade=true
      shift
      ;;
    -b|--binary)
      [[ -n "${2:-}" ]] || die "-b/--binary requires a path"
      binary_path="$2"
      shift 2
      ;;
    --no-modify-path)
      no_modify_path=true
      shift
      ;;
    *)
      die "unknown option: $1"
      ;;
  esac
done

require_command() {
  command -v "$1" >/dev/null 2>&1 || die "'$1' is required"
}

get_latest_version() {
  require_command curl
  if [[ -z "$latest_version_cache" ]]; then
    latest_version_cache="$(
      curl -fsSL "https://api.github.com/repos/${REPO}/releases/latest" \
        | sed -n 's/.*"tag_name": *"v\{0,1\}\([^"]*\)".*/\1/p'
    )"
    [[ -n "$latest_version_cache" ]] || die "unable to determine latest release"
  fi
  printf '%s\n' "$latest_version_cache"
}

resolve_asset_name() {
  local os arch
  os="$(uname -s)"
  arch="$(uname -m)"

  case "$os" in
    Linux) os="linux" ;;
    Darwin) os="darwin" ;;
    *) die "unsupported OS: $os" ;;
  esac

  case "$arch" in
    x86_64) arch="amd64" ;;
    aarch64|arm64) arch="arm64" ;;
    *) die "unsupported architecture: $arch" ;;
  esac

  printf '%s-%s-%s\n' "$APP" "$os" "$arch"
}

verify_checksum() {
  local file_path="$1"
  local checksum_path="$2"
  local expected actual

  expected="$(awk '{print $1}' "$checksum_path")"
  [[ -n "$expected" ]] || die "checksum file was empty"

  if command -v sha256sum >/dev/null 2>&1; then
    actual="$(sha256sum "$file_path" | awk '{print $1}')"
  elif command -v shasum >/dev/null 2>&1; then
    actual="$(shasum -a 256 "$file_path" | awk '{print $1}')"
  else
    die "missing sha256sum or shasum"
  fi

  [[ "$expected" == "$actual" ]] || die "checksum mismatch for downloaded binary"
}

add_to_path() {
  local config_file="$1"
  local command="$2"

  if grep -Fxq "$command" "$config_file" 2>/dev/null; then
    return 0
  fi
  {
    echo ""
    echo "# ${APP}"
    echo "$command"
  } >> "$config_file"
}

if $show_latest; then
  [[ "$upgrade" == false && -z "$binary_path" && -z "$requested_version" ]] || \
    die "-v (no arg) cannot be combined with other options"
  get_latest_version
  exit 0
fi

if $upgrade; then
  [[ -z "$binary_path" ]] || die "-u cannot be used with -b/--binary"
  [[ -z "$requested_version" ]] || die "-u cannot be combined with -v <version>"
  latest="$(get_latest_version)"
  if command -v "$APP" >/dev/null 2>&1; then
    installed="$("$APP" -v 2>/dev/null || true)"
    installed="${installed#v}"
    if [[ -n "$installed" && "$installed" == "$latest" ]]; then
      exit 0
    fi
  fi
  requested_version="$latest"
fi

if [[ -z "$binary_path" && -z "$requested_version" ]]; then
  requested_version="$(get_latest_version)"
fi

if [[ -n "$requested_version" && -z "$binary_path" ]] && command -v "$APP" >/dev/null 2>&1; then
  installed="$("$APP" -v 2>/dev/null || true)"
  installed="${installed#v}"
  if [[ -n "$installed" && "$installed" == "${requested_version#v}" ]]; then
    exit 0
  fi
fi

mkdir -p "$INSTALL_DIR"
tmp_dir="$(mktemp -d "${TMPDIR:-/tmp}/${APP}.XXXXXX")"
trap 'rm -rf "$tmp_dir"' EXIT

install_source=""
if [[ -n "$binary_path" ]]; then
  [[ -f "$binary_path" ]] || die "binary not found: $binary_path"
  install_source="$binary_path"
else
  require_command curl
  asset_name="$(resolve_asset_name)"
  checksum_name="${asset_name}.sha256"
  requested_version="${requested_version#v}"
  http_status="$(curl -sI -o /dev/null -w "%{http_code}" "https://github.com/${REPO}/releases/tag/v${requested_version}")"
  [[ "$http_status" != "404" ]] || die "release v${requested_version} not found"
  curl -# -L -o "${tmp_dir}/${asset_name}" \
    "https://github.com/${REPO}/releases/download/v${requested_version}/${asset_name}"
  curl -fsSL -o "${tmp_dir}/${checksum_name}" \
    "https://github.com/${REPO}/releases/download/v${requested_version}/${checksum_name}"
  verify_checksum "${tmp_dir}/${asset_name}" "${tmp_dir}/${checksum_name}"
  install_source="${tmp_dir}/${asset_name}"
fi

cp "$install_source" "${INSTALL_DIR}/${APP}"
chmod 755 "${INSTALL_DIR}/${APP}"

if [[ "$no_modify_path" != "true" && ":$PATH:" != *":$INSTALL_DIR:"* ]]; then
  XDG_CONFIG_HOME="${XDG_CONFIG_HOME:-$HOME/.config}"
  current_shell="$(basename "${SHELL:-bash}")"
  case "$current_shell" in
    zsh) config_candidates=("$HOME/.zshrc" "$HOME/.zshenv" "$XDG_CONFIG_HOME/zsh/.zshrc") ;;
    bash) config_candidates=("$HOME/.bashrc" "$HOME/.bash_profile" "$HOME/.profile") ;;
    fish) config_candidates=("$HOME/.config/fish/config.fish") ;;
    *) config_candidates=("$HOME/.profile" "$HOME/.bashrc") ;;
  esac

  config_file=""
  for f in "${config_candidates[@]}"; do
    if [[ -f "$f" ]]; then
      config_file="$f"
      break
    fi
  done

  if [[ -n "$config_file" ]]; then
    if [[ "$current_shell" == "fish" ]]; then
      add_to_path "$config_file" "fish_add_path $INSTALL_DIR"
    else
      add_to_path "$config_file" "export PATH=$INSTALL_DIR:\$PATH"
    fi
  else
    echo "export PATH=$INSTALL_DIR:\$PATH"
  fi
fi

echo "installed: ${INSTALL_DIR}/${APP}"
