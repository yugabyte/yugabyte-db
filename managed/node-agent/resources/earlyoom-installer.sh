#!/usr/bin/env bash
# Copyright 2020 YugabyteDB, Inc. and Contributors

set -euo pipefail

to_lower() {
  out=$(awk '{print tolower($0)}' <<< "$1")
  echo "$out"
}

readonly build_arch=$(to_lower "$(uname -m)")

main() {
  echo "* Installing Earlyoom Service"
  if [ "$SUDO_ACCESS" = "true" ]; then
    # Setting Memlock limits for earlyoom
    # Check and set DefaultLimitMEMLOCK in /etc/systemd/system.conf
    if ! sudo grep -q "^DefaultLimitMEMLOCK=500000" /etc/systemd/system.conf; then
        echo 'DefaultLimitMEMLOCK=500000' | sudo tee -a /etc/systemd/system.conf
    fi
    # Check and set DefaultLimitMEMLOCK in /etc/systemd/user.conf
    if ! sudo grep -q "^DefaultLimitMEMLOCK=500000" /etc/systemd/user.conf; then
        echo 'DefaultLimitMEMLOCK=500000' | sudo tee -a /etc/systemd/user.conf
    fi
  fi

  SYSTEMD_DIR="${YB_HOME_DIR}/.config/systemd/user"
  SERVICE_FILE="${SYSTEMD_DIR}/earlyoom.service"
  TMP_DIR="/tmp"
  TMP_FILE="${TMP_DIR}/earlyoom.service.tmp"

  # Configure the systemd unit file
  cat > "${TMP_FILE}" <<-EOF
  [Unit]
  Description=Early OOM Daemon
  Documentation=man:earlyoom(1) https://github.com/rfjakob/earlyoom

  [Service]
  EnvironmentFile=${BIN_DIR}/earlyoom.config
  ExecStart=${BIN_DIR}/earlyoom \$EARLYOOM_ARGS

  # Give priority to our process
  #Nice=-20
  # Avoid getting killed by OOM
  OOMScoreAdjust=-100
  # earlyoom never exits on it's own, so have systemd
  # restart it should it get killed for some reason.
  Restart=always
  # set memory limits and max tasks number
  TasksMax=10
  MemoryMax=50M

  [Install]
  WantedBy=default.target
EOF
  # Set the permissions after file creation. This is needed so that the service file
  # is executable during restart of systemd unit.
  chmod 755 "$TMP_FILE"
  if [ "$SUDO_ACCESS" = "true" ]; then\
    chown yugabyte:yugabyte $TMP_FILE
    su - yugabyte -c "mv $TMP_FILE $SERVICE_FILE"
  else
    mv $TMP_FILE $SERVICE_FILE
  fi

  SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

  ARTIFACT="earlyoom-linux-amd64.tar.gz"
  if [ "$build_arch" = "arm64" ]; then
      ARTIFACT="earlyoom-linux-arm64.tar.gz"
  fi
  mkdir -p "${TMP_DIR}/earlyoom"

  # node-agent/thirdparty directory.
  THIRDPARTY_DIR="${SCRIPT_DIR}/../thirdparty"
  if [ ! -d "$THIRDPARTY_DIR" ]; then
    # For backward compatibility when backported.
    # Remove me later.
    THIRDPARTY_DIR="${SCRIPT_DIR}/thirdparty"
  fi
  tar -xzf "${THIRDPARTY_DIR}/${ARTIFACT}" -C "${TMP_DIR}/earlyoom/" --no-same-owner
  EXTRACTED_DIR=$(ls -d ${TMP_DIR}/earlyoom/*/ | head -n 1)

  if [ "$SUDO_ACCESS" = "true" ]; then\
    cp $SCRIPT_DIR/configure_earlyoom_service.sh $TMP_DIR/
    sudo chown yugabyte:yugabyte $TMP_DIR/configure_earlyoom_service.sh
    sudo chmod 755 $TMP_DIR/configure_earlyoom_service.sh
    sudo chown yugabyte:yugabyte $EXTRACTED_DIR/earlyoom
    sudo chmod 755 $EXTRACTED_DIR/earlyoom
    mv $TMP_DIR/configure_earlyoom_service.sh $BIN_DIR/
  else
    cp $SCRIPT_DIR/configure_earlyoom_service.sh $BIN_DIR/
    chmod 755 $BIN_DIR/configure_earlyoom_service.sh
    chmod 755 $EXTRACTED_DIR/earlyoom
  fi
  mv $EXTRACTED_DIR/earlyoom $BIN_DIR/

  rm -rf "${TMP_DIR}/earlyoom"

  if [ -z "${EARLYOOM_ARGS}" ]; then
    EARLYOOM_ARGS="-m 5 -r 240 --prefer 'postgres'"
  fi

  ACTION="configure"
  if [ "$ENABLE_EARLYOOM" = "true" ]; then\
    ACTION="enable"
  fi

  if [ "$SUDO_ACCESS" = "true" ]; then\
    su - yugabyte -c "$BIN_DIR/configure_earlyoom_service.sh -a $ACTION -c \"$EARLYOOM_ARGS\""
  else
    $BIN_DIR/configure_earlyoom_service.sh -a $ACTION -c "$EARLYOOM_ARGS"
  fi
}

#The usage shows only the ones available to end users.
show_usage() {
  cat <<-EOT

Usage: ${0##*/} [<options>]

Options:
  --enable (OPTIONAL) Whether to enable by default.
  --yb_home_dir (REQUIRED) Home directory for user.
  --earlyoom_args (OPTIONAL) Args for earlyoom service.
  -h, --help
    Show usage.
EOT
}

if [[ ! $# -gt 0 ]]; then
  show_usage
  exit 1
fi

ENABLE_EARLYOOM="false"

while [[ $# -gt 0 ]]; do
  case $1 in
    --enable)
      ENABLE_EARLYOOM="true"
    ;;
    --earlyoom_args)
      EARLYOOM_ARGS="$2"
      shift
    ;;
    --yb_home_dir)
      YB_HOME_DIR="$2"
      shift
    ;;
    -h|--help)
      show_usage >&2
      exit 1
    ;;
    *)
      echo "Invalid option: $1\n" >&2
      show_usage >&2
      exit 1
  esac
  shift
done

cleanup() {
  if [ "$SUDO_ACCESS" = "true" ]; then\
    su - yugabyte -c "rm $BIN_DIR/configure_earlyoom_service.sh"
  else
    rm $BIN_DIR/configure_earlyoom_service.sh
  fi
}

if [ -z "$YB_HOME_DIR" ]; then
  echo "Home dir is required"
  show_usage >&2
  exit 1
fi

SUDO_ACCESS="false"
set +e
if [ $(id -u) = 0 ]; then
  SUDO_ACCESS="true"
elif sudo -n pwd >/dev/null 2>&1; then
  SUDO_ACCESS="true"
fi
set -Eeuo pipefail

BIN_DIR="${YB_HOME_DIR}/bin"

main
trap cleanup ERR
