#!/usr/bin/env bash

# Copyright (c) YugabyteDB, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
# in compliance with the License. You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software distributed under the License
# is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
# or implied. See the License for the specific language governing permissions and limitations
# under the License.
#

# This script configures chronyd and clockbound for AWS EC2 instances.
# Supported operating systems: AlmaLinux, Red Hat, and Ubuntu.
# Requires internet access and root privileges during configuration.
# No internet or root access necesary for validation.

# shellcheck disable=SC2086

# Exit on errors.
set -euo pipefail

# Default values
VERBOSE=0
VALIDATE_ONLY=0
OS_RELEASE=""
CHRONY_CONF=""
CHRONY_USER=""
CLOUD_PROVIDER="unknown"

# Parse arguments
while [[ $# -gt 0 ]]; do
  case "$1" in
    --verbose)
      VERBOSE=1
      ;;
    --validate)
      VALIDATE_ONLY=1
      ;;
    *)
      echo "Usage: $0 [--verbose] [--validate]"
      exit 1
      ;;
  esac
  shift
done

# Log functions.
log_empty_line() {
  echo >&2
}

log_warn() {
  local _log_level="warn"
  log "$@"
}

log_error() {
  local _log_level="error"
  log "$@"
}

get_timestamp() {
  date +%Y-%m-%d_%H_%M_%S
}

# This just logs to stderr.
log() {
  BEGIN_COLOR='\033[0;32m'
  END_COLOR='\033[0m'

  case ${_log_level:-info} in
    error)
      BEGIN_COLOR='\033[0;31m'
      shift
      ;;
    warn)
      BEGIN_COLOR='\033[0;33m'
      shift
      ;;
  esac
  echo -e "${BEGIN_COLOR}[$( get_timestamp ) ${BASH_SOURCE[1]##*/}:${BASH_LINENO[0]} \
    ${FUNCNAME[1]}]${END_COLOR}" "$@" >&2

}

fatal() {
  log "$@"
  exit 1
}

# Prints messages only in verbose mode.
log_verbose() {
  if [[ "${VERBOSE}" -eq 1 ]]; then
    log "$@"
  fi
}

# Detects OS and stores in OS_RELEASE variable.
detect_os() {
  ETC_RELEASE_FILE=/etc/os-release
  if [[ -f "${ETC_RELEASE_FILE}" ]]; then
    OS_RELEASE=$(grep '^ID=' "${ETC_RELEASE_FILE}" | cut -d '=' -f 2 | tr -d '"')
  else
    fatal "${ETC_RELEASE_FILE} does not exist."
  fi
}

# Checks script arguments and user permissions.
prechecks() {
  # Enable verbose mode if requested
  if [[ "${VERBOSE}" -eq 1 ]]; then
    set -x
  fi

  # Ensure that the OS is supported.
  case "${OS_RELEASE}" in
    almalinux|centos|rhel|ubuntu)
      log_verbose "Detected OS: ${OS_RELEASE}"
      ;;
    *)
      fatal "Unsupported operating system: ${OS_RELEASE}"
      ;;
  esac

  # Check if the script is run as root when --validate is NOT specified.
  if [[ "${VALIDATE_ONLY}" -ne 1 ]] && [[ "$(id -u)" -ne 0 ]]; then
    fatal "Configuration requires root access. Run the script with sudo?"
  fi
}

# Restarts a systemd service, whose name is passed as first argument.
# Assumes that the service is already configured.
restart_service() {
  local service_name="$1"

  log_verbose "Reloading systemd daemon and enabling ${service_name} service..."

  if ! systemctl daemon-reload; then
    fatal "Failed to reload systemd."
  fi

  if ! systemctl is-enabled --quiet "${service_name}"; then
    if ! systemctl enable "${service_name}"; then
      fatal "Failed to enable ${service_name}. Please check the service status for details."
    fi
  fi

  if ! systemctl start "${service_name}"; then
    fatal "Failed to start ${service_name}. Please check the service status for details."
  fi

  if ! systemctl is-active --quiet "${service_name}"; then
    fatal "${service_name} failed to start. Please check the service status for details."
  fi

  log_verbose "${service_name} service has been configured and started."
}

validate_chrony_config() {
  # Check if chrony.conf exists in either location
  if [[ -f /etc/chrony.conf ]]; then
    CHRONY_CONF="/etc/chrony.conf"
  elif [[ -f /etc/chrony/chrony.conf ]]; then
    CHRONY_CONF="/etc/chrony/chrony.conf"
  else
    fatal "chrony.conf not found."
  fi

  if ! grep -q '^maxclockerror 50' "${CHRONY_CONF}"; then
    fatal "${CHRONY_CONF} does not have 'maxclockerror 50' set."
  fi

  if ! systemctl is-active --quiet chronyd; then
    fatal "chronyd service is not running."
  fi
}

# Configures and restarts chronyd.
# Assumes chronyd is installed and running.
configure_chrony() {
  # Check if chrony.conf exists in either location
  if [[ -f /etc/chrony.conf ]]; then
    CHRONY_CONF="/etc/chrony.conf"
  elif [[ -f /etc/chrony/chrony.conf ]]; then
    CHRONY_CONF="/etc/chrony/chrony.conf"
  else
    fatal "chrony.conf does not exist in /etc/chrony.conf or /etc/chrony/chrony.conf."
  fi

  if ! grep -q '^maxclockerror' "${CHRONY_CONF}"; then
    echo "maxclockerror 50" >> "${CHRONY_CONF}"
    log_verbose "maxclockerror 50 has been added to /etc/chrony.conf."
    log_verbose "Restarting chronyd service to apply configuration changes..."
  fi

  if ! grep -q '^maxclockerror 50' "${CHRONY_CONF}"; then
    fatal "${CHRONY_CONF} has 'maxclockerror' set to a value other than 50."
  fi

  restart_service "chronyd"
}

validate_clockbound_config() {
  if ! systemctl is-active --quiet clockbound; then
    fatal "clockbound is not enabled."
  fi

  if ! pgrep -f 'clockbound --max-drift-rate 50' > /dev/null; then
    fatal "clockbound is not configured with --max-drift-rate 50."
  fi
}

# Installs a C linker.
# Rust does not install the linker on its own.
install_C_linker() {
  log_verbose "Installing C linker on ${OS_RELEASE}..."
  case $OS_RELEASE in
    almalinux|centos|rhel)
      dnf update -y
      dnf install -y gcc
      ;;
    ubuntu)
      apt-get update -y
      apt-get install -y gcc
      ;;
    *)
      echo "Unsupported operating system ${OS_RELEASE}."
      exit 1
      ;;
  esac
}

# Installs rust
install_rust() {
  # Check if Rust is installed
  if ! command -v rustc >/dev/null 2>&1; then
    log_verbose "Rust is not installed. Installing Rust..."

    # Install Rust using rustup
    install_C_linker
    curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y

    # Add Rust to the current shell session's PATH
    # shellcheck disable=SC1090,SC1091
    source "$HOME/.cargo/env"

    log_verbose "Rust installation complete."
  else
    # Add Rust to the current shell session's PATH
    # shellcheck disable=SC1090,SC1091
    source "$HOME/.cargo/env"

    log_verbose "Rust is already installed."
  fi
}

ensure_selinux_perms() {
  # Explicit check to ensure that the selinux type is not unexpected.
  # shellcheck disable=SC2012
  selinux_type=$(ls -Z "/usr/local/bin/clockbound" | awk '{print $4}')
  if [[ "$selinux_type" == *"user_home_t"* ]]; then
    fatal "Incorrect selinux type for clockbound."
  fi

  cat <<EOF > chrony_uds_access.te
module chrony_uds_access 1.0;

require {
    type bin_t;
    type chronyd_t;
    type unconfined_service_t;
    class unix_dgram_socket { sendto };
}

allow bin_t chronyd_t:unix_dgram_socket sendto;
allow chronyd_t unconfined_service_t:unix_dgram_socket sendto;
EOF
  dnf install policycoreutils-devel -y
  checkmodule -M -m -o chrony_uds_access.mod chrony_uds_access.te
  semodule_package -o chrony_uds_access.pp -m chrony_uds_access.mod
  semodule -i chrony_uds_access.pp
}

install_clockbound() {
  # Check if clockbound is installed
  if [[ ! -f "/usr/local/bin/clockbound" ]]; then
    log_verbose "clockbound is not installed. Installing clockbound..."

    install_rust
    cargo install clock-bound-d
    # Always copy the file instead of moving the file.
    # Moving the file will retain the selinux type, usually that
    # of the home directory. This will prevent systemd from starting
    # the service because of incorrect selinux type.
    cp "${HOME}/.cargo/bin/clockbound" /usr/local/bin/
    chown "${CHRONY_USER}:${CHRONY_USER}" /usr/local/bin/clockbound
    case "${OS_RELEASE}" in
      almalinux|centos|rhel)
        ensure_selinux_perms
        ;;
      ubuntu)
        ;;
    esac

      log_verbose "clockbound installation complete."
  else
   log_verbose "clockbound is already installed."
  fi
}

# Function to detect cloud provider based on the chrony configuration
retrieve_cloud_provider() {
  # Check if AWS PTP server is configured
  if grep -q "server\s*169.254.169.123" "${CHRONY_CONF}"; then
    CLOUD_PROVIDER="aws"
  fi
}

configure_clockbound() {
  if ! systemctl is-active --quiet clockbound; then
    # Configure and start clockbound
    log_verbose "clockbound is not enabled. Configuring and starting clockbound service..."

    # Check systemd version
    SYSTEMD_VERSION=$(systemctl --version | head -n 1 | awk '{print $2}')

    if id "chrony" &>/dev/null; then
      CHRONY_USER="chrony"
    elif id "_chrony" &>/dev/null; then
      CHRONY_USER="_chrony"
    else
      fatal "Neither 'chrony' nor '_chrony' user exists. Exiting."
    fi

    EXTRA_ARGS=""
    # Check for PTP only on AWS instances.
    retrieve_cloud_provider
    if [[ "${CLOUD_PROVIDER}" == "aws" ]]; then
      if chronyc sources | grep "#.\s*PHC" > /dev/null 2>&1; then
        # Pick ETH_DEVICE as the first non-loopback device.
        for iface in /sys/class/net/*; do
          iface=$(basename "$iface")
          if [[ "${iface}" != "lo" ]]; then
            ETH_DEVICE="${iface}"
            break
          fi
        done

        # Check if PHC is available on ETH_DEVICE.
        if ethtool -T "${ETH_DEVICE}" | grep -q "PTP Hardware Clock: none"; then
          fatal "PHC is not available on ${ETH_DEVICE}."
        fi

        # Check whether a PHC source is selected.
        if ! chronyc sources | grep "#\*\s*PHC" > /dev/null 2>&1; then
          fatal "PHC source is not selected as the clock soruce."
        fi

        PHC_ID=$(chronyc sources | grep "#\*\s*PHC" | awk '{print $2}')
        EXTRA_ARGS="-r ${PHC_ID} -i ${ETH_DEVICE}"
      fi
    fi

    # Create the clockbound service file based on systemd version
    if [[ "${SYSTEMD_VERSION}" -ge 235 ]]; then
      cat <<EOF > /usr/lib/systemd/system/clockbound.service
[Unit]
Description=ClockBound

[Service]
Type=simple
Restart=always
RestartSec=10
ExecStart=/usr/local/bin/clockbound --max-drift-rate 50 ${EXTRA_ARGS}
RuntimeDirectory=clockbound
RuntimeDirectoryPreserve=yes
WorkingDirectory=/run/clockbound
User=${CHRONY_USER}
Group=${CHRONY_USER}

[Install]
WantedBy=multi-user.target
EOF
    else
      cat <<EOF > /usr/lib/systemd/system/clockbound.service
[Unit]
Description=ClockBound

[Service]
Type=simple
Restart=always
RestartSec=10
PermissionsStartOnly=true
ExecStartPre=/bin/mkdir -p /run/clockbound
ExecStartPre=/bin/chmod 775 /run/clockbound
ExecStartPre=/bin/chown ${CHRONY_USER}:${CHRONY_USER} /run/clockbound
ExecStartPre=/bin/cd /run/clockbound
ExecStart=/usr/local/bin/clockbound --max-drift-rate 50 ${EXTRA_ARGS}
User=${CHRONY_USER}
Group=${CHRONY_USER}

[Install]
WantedBy=multi-user.target
EOF
    fi
  fi

  restart_service "clockbound"
}

detect_os
prechecks

if [[ "${VALIDATE_ONLY}" -ne 1 ]]; then
  configure_chrony
fi
validate_chrony_config

if [[ "${VALIDATE_ONLY}" -ne 1 ]]; then
  install_clockbound
  configure_clockbound
fi
validate_clockbound_config
