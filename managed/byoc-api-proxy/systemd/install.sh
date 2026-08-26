#!/usr/bin/env bash
set -euo pipefail

# Installs the BYOC API proxy systemd unit on a host, or validates its
# configuration without touching the system.
#
# Usage:
#   sudo ./install.sh [path/to/byoc-api-proxy.jar]
#   ./install.sh --validate [--app-config <application.yaml>] [--env-file <env-file>]
#                [--java-home <dir>]
#
# Validate mode (also: --dry-run) runs the packaged app's own --validate-config
# dry run, loading configuration exactly as the service would at runtime
# (application.yaml overlay, env file, bundled defaults) and reporting all
# problems at once. It does not require root and is used by yba-installer
# before installing, upgrading or reconfiguring the service. Required fields
# are defined by the app's validation constraints, so they always match the
# packaged version.
# Exit codes: 0 = valid, 1 = invalid (problems listed on stderr), 64 = usage.
#
# Environment overrides (install mode):
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

validate=false
app_config=""
env_file=""
java_home=""
jar_arg=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --validate|--dry-run)
      validate=true
      shift
      ;;
    --app-config)
      [[ $# -ge 2 ]] || { echo "$1 requires a value" >&2; exit 64; }
      app_config="$2"
      shift 2
      ;;
    --env-file)
      [[ $# -ge 2 ]] || { echo "$1 requires a value" >&2; exit 64; }
      env_file="$2"
      shift 2
      ;;
    --java-home)
      [[ $# -ge 2 ]] || { echo "$1 requires a value" >&2; exit 64; }
      java_home="$2"
      shift 2
      ;;
    -h|--help)
      sed -n '4,29p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
      exit 0
      ;;
    --*)
      echo "unknown argument: $1" >&2
      exit 64
      ;;
    *)
      jar_arg="$1"
      shift
      ;;
  esac
done

resolve_default_jar() {
  local packaged_jar="${PACKAGE_VERSION_DIR}/bin/byoc-api-proxy.jar"
  if [[ -f "${packaged_jar}" ]]; then
    echo "${packaged_jar}"
    return
  fi
  find "${PACKAGE_VERSION_DIR}/../build/libs" -maxdepth 1 -name '*.jar' \
    ! -name '*-plain.jar' -print -quit 2>/dev/null || true
}

if [[ "${validate}" == "true" ]]; then
  # Standalone default: validate the config this script would install against.
  if [[ -z "${app_config}" && -f "${CONFIG_DIR}/application.yaml" ]]; then
    app_config="${CONFIG_DIR}/application.yaml"
  fi
  if [[ -z "${env_file}" && -f "${CONFIG_DIR}/byoc-api-proxy.env" ]]; then
    env_file="${CONFIG_DIR}/byoc-api-proxy.env"
  fi
  jar="${jar_arg:-$(resolve_default_jar)}"
  if [[ ! -f "${jar}" ]]; then
    echo "Jar not found: ${jar}" >&2
    exit 1
  fi

  # Feed the app its configuration the same way the systemd unit does at
  # runtime, then let its --validate-config dry run judge it.
  if [[ -n "${env_file}" && -f "${env_file}" ]]; then
    set -a
    # shellcheck disable=SC1090
    . "${env_file}"
    set +a
  fi
  if [[ -n "${app_config}" && -f "${app_config}" ]]; then
    export SPRING_CONFIG_ADDITIONAL_LOCATION="optional:file:${app_config}"
  fi

  java_bin="java"
  if [[ -n "${java_home}" ]]; then
    java_bin="${java_home}/bin/java"
  elif [[ -n "${JAVA_HOME:-}" ]]; then
    java_bin="${JAVA_HOME}/bin/java"
  fi
  if ! command -v "${java_bin}" >/dev/null; then
    echo "java not found (${java_bin}); set --java-home or JAVA_HOME" >&2
    exit 1
  fi

  cd "${PACKAGE_VERSION_DIR}"
  exec "${java_bin}" -jar "${jar}" --validate-config
fi

JAR_SOURCE="${jar_arg:-$(resolve_default_jar)}"

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
