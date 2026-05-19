#!/usr/bin/env bash
set -Eeuo pipefail

SERVICE_NAME="captive-portal-fast"
BIN_PATH="/usr/local/bin/captive_portal_fast"
DOWNLOAD_URL="${DOWNLOAD_URL:-https://github.com/Lavan1874/captive_portal_fast/releases/latest/download/captive_portal_fast}"
SERVICE_FILE="/etc/systemd/system/${SERVICE_NAME}.service"
TEST_URL="http://127.0.0.1:44380/generate_204"
SUCCESS_URL="http://127.0.0.1:44380/success.html"

log() {
  printf '[%s] %s\n' "$SERVICE_NAME" "$*"
}

die() {
  printf '[%s] ERROR: %s\n' "$SERVICE_NAME" "$*" >&2
  exit 1
}

require_root() {
  [ "${EUID:-$(id -u)}" -eq 0 ] || die "please run as root, for example: sudo $0"
}

require_linux_amd64() {
  case "$(uname -m)" in
    x86_64|amd64) ;;
    *) die "the published binary is Linux amd64, but this host is $(uname -m)" ;;
  esac
}

require_systemd() {
  command -v systemctl >/dev/null 2>&1 || die "systemctl not found"
  [ -d /run/systemd/system ] || die "this host does not appear to be running systemd"
}

install_dependencies() {
  command -v apt-get >/dev/null 2>&1 || die "apt-get not found; this script targets Debian/Ubuntu"

  log "installing download dependencies"
  export DEBIAN_FRONTEND=noninteractive
  apt-get update
  apt-get install -y ca-certificates curl
}

download_binary() {
  tmp="$(mktemp)"
  trap 'rm -f "$tmp" "${BIN_PATH}.new"' EXIT

  log "downloading latest binary"
  curl -fL --retry 3 --connect-timeout 10 --output "$tmp" "$DOWNLOAD_URL"

  magic="$(od -An -N4 -tx1 "$tmp" | tr -d ' \n')"
  [ "$magic" = "7f454c46" ] || die "downloaded file is not an ELF binary"

  chmod 0755 "$tmp"
  install -m 0755 "$tmp" "${BIN_PATH}.new"
  mv -f "${BIN_PATH}.new" "$BIN_PATH"
}

write_service() {
  log "writing ${SERVICE_FILE}"
  cat >"$SERVICE_FILE" <<EOF
[Unit]
Description=Fast local captive portal detection endpoint
After=network.target

[Service]
Type=simple
ExecStart=${BIN_PATH}
Restart=always
RestartSec=1
User=nobody
Group=nogroup
NoNewPrivileges=true
PrivateTmp=true
ProtectHome=true
ProtectSystem=strict

[Install]
WantedBy=multi-user.target
EOF

  chmod 0644 "$SERVICE_FILE"
}

start_service() {
  log "starting service"
  systemctl daemon-reload
  systemctl enable "$SERVICE_NAME"

  if ! systemctl restart "$SERVICE_NAME"; then
    journalctl -u "$SERVICE_NAME" -n 30 --no-pager || true
    die "failed to start ${SERVICE_NAME}.service"
  fi
}

self_test() {
  log "testing ${TEST_URL}"

  for _ in $(seq 1 20); do
    code="$(curl --noproxy '*' -sS -o /dev/null -w '%{http_code}' "$TEST_URL" || true)"
    [ "$code" = "204" ] && break
    sleep 0.2
  done

  [ "${code:-}" = "204" ] || die "/generate_204 returned HTTP ${code:-none}"

  body="$(curl --noproxy '*' -fsS "$SUCCESS_URL")"
  [ "$body" = "<html><body>Success</body></html>" ] ||
    die "/success.html returned unexpected body: $body"

  log "self test passed"
}

main() {
  require_root
  require_linux_amd64
  require_systemd
  install_dependencies
  download_binary
  write_service
  start_service
  self_test

  log "installed ${BIN_PATH}"
  log "service: systemctl status ${SERVICE_NAME} --no-pager"
  log "endpoint: ${TEST_URL}"
}

main "$@"
