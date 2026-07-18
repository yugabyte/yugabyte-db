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

# Compile network driver ena with PTP and install it.

PTP_DEVICE=""
OS_RELEASE=""

set -euo pipefail

# Log functions.
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

detect_os() {
  ETC_RELEASE_FILE=/etc/os-release
  if [[ -f "${ETC_RELEASE_FILE}" ]]; then
    OS_RELEASE=$(grep '^ID=' "${ETC_RELEASE_FILE}" | cut -d '=' -f 2 | tr -d '"')
  else
    fatal "${ETC_RELEASE_FILE} does not exist."
  fi
}

prechecks() {
  if [[ "$(id -u)" -ne 0 ]]; then
    fatal "Configuration requires root access. Run the script with sudo?"
  fi

  # Ensure that the OS is supported.
  case "${OS_RELEASE}" in
    almalinux|centos|rhel|ubuntu)
      log "Detected OS: ${OS_RELEASE}"
      ;;
    *)
      fatal "Unsupported operating system: ${OS_RELEASE}"
      ;;
  esac

  if ! grep -w '^CONFIG_PTP_1588_CLOCK=[y]' "/boot/config-$(uname -r)" \
      > /dev/null 2>&1; then
    fatal "CONFIG_PTP_1588_CLOCK is not enabled in the kernel."
  fi
}

install_prereqs() {
  case "${OS_RELEASE}" in
    almalinux|centos|rhel)
      dnf update -y
      dnf install "kernel-devel-$(uname -r)" git -y
      ;;
    ubuntu)
      apt update -y
      apt install "linux-headers-$(uname -r)" -y
      apt install make gcc -y
      ;;
  esac
}

compile_ena() {
  git clone https://github.com/amzn/amzn-drivers.git
  cd amzn-drivers
  cd kernel/linux/ena
  ENA_PHC_INCLUDE=1 make
}

reload_ena() {
  rmmod ena
  insmod ena.ko phc_enable=1
  PTP_DEVICE=$(find /sys/class/ptp -mindepth 1 -maxdepth 1 | \
               head -n 1 | xargs basename)
}

configure_chrony() {
  # Check if chrony.conf exists in either location
  if [[ -f /etc/chrony.conf ]]; then
    CHRONY_CONF="/etc/chrony.conf"
  elif [[ -f /etc/chrony/chrony.conf ]]; then
    CHRONY_CONF="/etc/chrony/chrony.conf"
  else
    fatal "chrony.conf not found."
  fi

  if ! grep -q "^refclock PHC /dev/${PTP_DEVICE} poll 0 delay 0.000010 prefer" \
      "${CHRONY_CONF}"; then
    echo "refclock PHC /dev/${PTP_DEVICE} poll 0 delay 0.000010 prefer" \
        >> "${CHRONY_CONF}"
  fi

  if ! systemctl daemon-reload; then
    fatal "Failed to reload systemd."
  fi

  if ! systemctl restart chronyd; then
    fatal "Failed to restart chronyd."
  fi
}

validate() {
  if ! ls /dev/"${PTP_DEVICE}" > /dev/null 2>&1; then
    fatal "/dev/${PTP_DEVICE} does not exist."
  fi

  PTP_ID=${PTP_DEVICE##ptp}
  for iface in /sys/class/net/*; do
    iface=$(basename "$iface")
    if [[ "${iface}" != "lo" ]]; then
      ETH_DEVICE="${iface}"
      break
    fi
  done
  if ! ethtool -T "${ETH_DEVICE}" | grep -q "PTP Hardware Clock: ${PTP_ID}"; then
    fatal "${ETH_DEVICE} does not support /dev/${PTP_DEVICE}."
  fi

  if ! systemctl is-active --quiet chronyd; then
    fatal "chronyd service is not running."
  fi

  if ! chronyc sources | grep -q '#.\s*PHC' > /dev/null 2>&1; then
    fatal "PHC source not configured in chrony."
  fi
}

detect_os
prechecks
install_prereqs
compile_ena
reload_ena
configure_chrony
validate
