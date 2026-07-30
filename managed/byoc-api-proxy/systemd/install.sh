#!/usr/bin/env bash
set -euo pipefail

# Installs the BYOC API proxy systemd unit on a host.
#
# Usage:
#   sudo ./install.sh [path/to/byoc-api-proxy.jar]
#
# Environment overrides:
#   INSTALL_ROOT   default: /opt/yugabyte/byoc-api-proxy
#   CONFIG_DIR     default: /etc/yugabyte/byoc-api-proxy
#   SERVICE_USER   default: yugabyte
#   SYSTEMD_DIR    default: /etc/systemd/system
#
# When run from a release tarball, defaults to:
#   jar:     <version>/bin/byoc-api-proxy.jar
#   systemd: <version>/systemd/

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PACKAGE_VERSION_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

INSTALL_ROOT="${INSTALL_ROOT:-/opt/yugabyte/byoc-api-proxy}"
CONFIG_DIR="${CONFIG_DIR:-/etc/yugabyte/byoc-api-proxy}"
SERVICE_NAME="byoc-api-proxy.service"
SERVICE_USER="${SERVICE_USER:-yugabyte}"
SYSTEMD_DIR="${SYSTEMD_DIR:-/etc/systemd/system}"

resolve_default_jar() {
  local packaged_jar="${PACKAGE_VERSION_DIR}/bin/byoc-api-proxy.jar"
  if [[ -f "${packaged_jar}" ]]; then
    echo "${packaged_jar}"
    return
  fi
  find "${PACKAGE_VERSION_DIR}/../build/libs" -maxdepth 1 -name '*.jar' \
    ! -name '*-plain.jar' -print -quit 2>/dev/null || true
}

JAR_SOURCE="${1:-$(resolve_default_jar)}"

if [[ "${EUID}" -ne 0 ]]; then
  echo "Run as root (sudo)." >&2
  exit 1
fi

if [[ ! -f "${JAR_SOURCE}" ]]; then
  echo "Jar not found: ${JAR_SOURCE}" >&2
  exit 1
fi

if ! id "${SERVICE_USER}" >/dev/null 2>&1; then
  echo "User ${SERVICE_USER} does not exist. Create it or set SERVICE_USER." >&2
  exit 1
fi

install -d -m 0755 "${INSTALL_ROOT}"
install -m 0644 "${JAR_SOURCE}" "${INSTALL_ROOT}/byoc-api-proxy.jar"

install -d -m 0750 "${CONFIG_DIR}"
if [[ ! -f "${CONFIG_DIR}/byoc-api-proxy.env" ]]; then
  install -m 0600 "${SCRIPT_DIR}/byoc-api-proxy.env.example" "${CONFIG_DIR}/byoc-api-proxy.env"
  echo "Created ${CONFIG_DIR}/byoc-api-proxy.env from example - edit before starting."
else
  echo "Keeping existing ${CONFIG_DIR}/byoc-api-proxy.env"
fi
if [[ ! -f "${CONFIG_DIR}/application.yaml" ]]; then
  install -m 0640 "${SCRIPT_DIR}/application.yaml.example" "${CONFIG_DIR}/application.yaml"
else
  echo "Keeping existing ${CONFIG_DIR}/application.yaml"
fi
chown -R "${SERVICE_USER}:${SERVICE_USER}" "${INSTALL_ROOT}" "${CONFIG_DIR}"

install -m 0644 "${SCRIPT_DIR}/${SERVICE_NAME}" "${SYSTEMD_DIR}/${SERVICE_NAME}"
systemctl daemon-reload
systemctl enable "${SERVICE_NAME}"

echo "Installed ${SERVICE_NAME}."
echo "  1. Edit ${CONFIG_DIR}/byoc-api-proxy.env"
echo "  2. Optionally edit ${CONFIG_DIR}/application.yaml (SSL bundles)"
echo "  3. systemctl start ${SERVICE_NAME}"
echo "  4. journalctl -u ${SERVICE_NAME} -f"
