#!/usr/bin/env bash
# Copyright 2020 YugaByte, Inc. and Contributors

set -euo pipefail

#Installation information.
INSTALL_USER=""
INSTALL_USER_HOME=""
NODE_AGENT_HOME=""
NODE_AGENT_PKG_DIR=""
NODE_AGENT_RELEASE_DIR=""

# Yugabyte Anywhere SSL cert verification option.
SKIP_VERIFY_CERT=""
#Skip the interactive installation or not.
SKIP_CONFIGURE="false"
#Unregister (if any) and register again.
FORCE_INSTALL="false"
CUSTOMER_ID=""
NODE_IP=""
NODE_PORT="9070"
API_TOKEN=""
PLATFORM_URL=""
TYPE=""
VERSION=""
JWT=""
NODE_AGENT_BASE_URL=""
NODE_AGENT_DOWNLOAD_URL=""
API_TOKEN_HEADER="X-AUTH-YW-API-TOKEN"
JWT_HEADER="X-AUTH-YW-API-JWT"
INSTALLER_NAME="node-agent-installer.sh"
SYSTEMD_DIR="/etc/systemd/system"
SERVICE_NAME="yb-node-agent.service"
SERVICE_RESTART_INTERVAL_SEC=2

ARCH=$(uname -m)
OS=$(uname -s)

pushd () {
  command pushd "$@" > /dev/null
}

popd () {
  command popd > /dev/null
}

add_path() {
  if [[ ":$PATH:" != *":$1:"* ]]; then
    PATH="$1${PATH:+":$PATH"}"
    echo "PATH=$PATH" >> "$INSTALL_USER_HOME"/.bashrc
    export PATH
  fi
}

node_agent_dir_setup() {
  echo "* Creating Node Agent Directory."
  #Create node-agent directory.
  mkdir -p "$NODE_AGENT_HOME"
  #Copy installer script to the node-agent directory.
  cp "$0" "$NODE_AGENT_HOME/$INSTALLER_NAME"
  #Change permissions.
  chmod 754 "$NODE_AGENT_HOME"
  echo "* Changing directory to node agent."
  #Change directory.
  pushd "$NODE_AGENT_HOME"
  echo "* Creating Sub Directories."
  mkdir -p cert config logs release
  chmod -R 754 .
  popd
  add_path "$NODE_AGENT_PKG_DIR/bin"
}

unregister_node_agent() {
  local RESPONSE_FILE="/tmp/node_agent_${INSTALL_USER}.json"
  local STATUS_CODE=""
  STATUS_CODE=$(curl -s ${SKIP_VERIFY_CERT:+ "-k"} -w "%{http_code}" -L --request GET \
    "$NODE_AGENT_BASE_URL?nodeIp=$NODE_IP" --header "$HEADER: $HEADER_VAL" \
    --output "$RESPONSE_FILE"
    )
  if [ "$STATUS_CODE" != "200" ]; then
    echo "Fail to check existing node agent. Status code $STATUS_CODE"
    exit 1
  fi
  # Command jq is not available.
  # Continue after pipefail.
  set +e
  local NODE_AGENT_UUID=""
  NODE_AGENT_UUID="$(grep -o '"uuid":"[^"]*"' "$RESPONSE_FILE" | cut -d: -f2 | tr -d '"')"
  local RUNNING=""
  RUNNING=$(systemctl list-units | grep -F yb-node-agent.service)
  if [ -n "$RUNNING" ]; then
    sudo systemctl stop yb-node-agent
    sudo systemctl disable yb-node-agent
  fi
  set -e
  if [ -n "$NODE_AGENT_UUID" ]; then
    local STATUS_CODE=""
    STATUS_CODE=$(curl -s ${SKIP_VERIFY_CERT:+ "-k"} -w "%{http_code}" -L --request DELETE \
    "$NODE_AGENT_BASE_URL/$NODE_AGENT_UUID" --header "$HEADER: $HEADER_VAL" --output /dev/null
    )
    if [ "$STATUS_CODE" != "200" ]; then
      echo "Failed to unregister node agent $NODE_AGENT_UUID. Status code $STATUS_CODE"
      exit 1
    fi
  fi
}

download_extract_package() {
    #Change to home directory.
    pushd "$INSTALL_USER_HOME"
    echo "* Starting YB Node Agent $TYPE."
    if [ "$TYPE" = "install" ]; then
      node_agent_dir_setup
    fi
    pushd "$NODE_AGENT_RELEASE_DIR"
    echo "* Downloading YB Node Agent build package."
    #Get OS version and Hardware info.

    #Change x86_64 to amd64.
    local GO_ARCH_TYPE=$ARCH
    if [ "$GO_ARCH_TYPE" = "x86_64" ]; then
      GO_ARCH_TYPE="amd64"
    elif [ "$GO_ARCH_TYPE" = "aarch64" ]; then
      GO_ARCH_TYPE="arm64"
    fi
    echo "* Getting $OS/$GO_ARCH_TYPE package"
    local BUILD_TAR="node-agent.tgz"
    local RESPONSE_CODE=""
    RESPONSE_CODE=$(curl -s ${SKIP_VERIFY_CERT:+ "-k"} -w "%{http_code}" --location --request GET \
    "$NODE_AGENT_DOWNLOAD_URL?downloadType=package&os=$OS&arch=$GO_ARCH_TYPE" \
    --header "$HEADER: $HEADER_VAL" --output $BUILD_TAR)

    if [ "$RESPONSE_CODE" -ne 200 ]; then
      echo "x Error while downloading the node agent build package"
      exit 1
    fi

    #Get the version from the tar.
    #Note: This method of fetching the version from the tar depends on the packaging.
    #This might break if the packaging changes in the future.
    #Expected tar dir structure is as follows:
    #./
    #./<version>/
    #./<version>/*
    set +o pipefail
    VERSION=$(tar -tzf $BUILD_TAR | awk -F '/' '$2{print $2; exit}')
    set -o pipefail

    echo "* Downloaded Version - $VERSION"
    #Untar the package.
    echo "* Extracting the build package"
    #This will extract the build files to a directory named $VERSION.
    #Packaging should take care of this.
    tar --no-same-owner -zxf $BUILD_TAR
    #Delete the installer tar file.
    rm -rf $BUILD_TAR
}

setup_symlink() {
  #Remove the previous symlinks if they exist.
  if [ -L "$NODE_AGENT_PKG_DIR" ]; then
    unlink "$NODE_AGENT_PKG_DIR"
  fi
  #Create a new symlink between node-agent/pkg -> node-agent/release/<version>.
  ln -s -f "$NODE_AGENT_RELEASE_DIR/$VERSION" "$NODE_AGENT_PKG_DIR"
}

check_sudo_access() {
  SUDO_ACCESS="false"
  set +e
  sudo -n pwd >/dev/null 2>&1
  if sudo -n pwd >/dev/null 2>&1; then
    SUDO_ACCESS="true"
  fi
  if [ "$OS" = "Linux" ]; then
    SE_LINUX_STATUS=$(getenforce 2>/dev/null)
  fi
  set -e
}

modify_selinux() {
  set +e
  if ! command -v semanage >/dev/null 2>&1; then
    if command -v yum >/dev/null 2>&1; then
      sudo yum install -y policycoreutils-python-utils
    elif command -v apt-get >/dev/null 2>&1; then
      sudo apt-get update -y
      sudo apt-get install -y semanage-utils
    fi
  fi
  sudo semanage port -lC | grep -F "$NODE_PORT" >/dev/null 2>&1
  if [ "$?" -ne 0 ]; then
    sudo semanage port -a -t http_port_t -p tcp "$NODE_PORT"
  fi
  sudo semanage fcontext -lC | grep -F "$NODE_AGENT_HOME(/.*)?" >/dev/null 2>&1
  if [ "$?" -ne 0 ]; then
    sudo semanage fcontext -a -t bin_t "$NODE_AGENT_HOME(/.*)?"
  fi
  set -e
  sudo restorecon -ir "$NODE_AGENT_HOME"
}

install_systemd_service() {
  if [ "$SE_LINUX_STATUS" = "Enforcing" ]; then
    modify_selinux
  fi
  echo "* Installing Node Agent Systemd Service"
  sudo tee "$SYSTEMD_DIR/$SERVICE_NAME"  <<-EOF
  [Unit]
  Description=YB Anywhere Node Agent
  After=network-online.target

  [Service]
  User=$INSTALL_USER
  WorkingDirectory=$NODE_AGENT_HOME
  LimitCORE=infinity
  LimitNOFILE=1048576
  LimitNPROC=12000
  ExecStart=$NODE_AGENT_PKG_DIR/bin/node-agent server start
  Restart=always
  RestartSec=$SERVICE_RESTART_INTERVAL_SEC

  [Install]
  WantedBy=multi-user.target
EOF
  echo "* Starting the systemd service"
  sudo systemctl daemon-reload
  #To enable the node-agent service on reboot.
  sudo systemctl enable yb-node-agent
  sudo systemctl start yb-node-agent
  echo "* Started the systemd service"
  echo "* Run 'systemctl status yb-node-agent' to check\
 the status of the yb-node-agent"
  echo "* Run 'sudo systemctl stop yb-node-agent' to stop\
 the yb-node-agent service"
}

#The usage shows only the ones available to end users.
show_usage() {
  cat <<-EOT
Usage: ${0##*/} [<options>]

Options:
  -t, --type (REQUIRED)
    Type of install to perform. Must be in ['install', 'install_service' (Requires sudo access)].
  -u, --url (REQUIRED)
    Yugabyte Anywhere URL.
  -at, --api_token (REQUIRED with install type)
    Api token to download the build files.
  --user (REQUIRED only for install_service type)
    Username of the installation. A sudo user can install service for a non-sudo user.
  --skip_verify_cert (OPTIONAL)
    Specify to skip Yugabyte Anywhere server cert verification during install.
  -h, --help
    Show usage.
EOT
}

err_msg() {
  echo "$@" >&2
}

#Main entry function.
main() {
  if [ "$TYPE" = "install_service" ]; then
    if [ "$SUDO_ACCESS" = "false" ]; then
      echo "SUDO access is required."
      exit 1
    fi
    install_systemd_service
  elif [ "$TYPE" = "download_package" ]; then
    if [ -z "$JWT" ]; then
      echo "JWT is required."
      show_usage >&2
      exit 1
    fi
    download_extract_package >/dev/null
    echo "$VERSION"
  elif [ "$TYPE" = "upgrade" ]; then
    if [ -z "$VERSION" ]; then
      echo "Version is required."
      exit 1
    fi
    setup_symlink
  elif [ "$TYPE" = "install" ]; then
    if [ -z "$PLATFORM_URL" ]; then
      echo "Yugabyte Anywhere URL is required."
      show_usage >&2
      exit 1
    fi
    if [ -z "$API_TOKEN" ]; then
      echo "API token is required."
      show_usage >&2
      exit 1
    fi
    if [ "$FORCE_INSTALL" = "true" ]; then
      if [ -z "$CUSTOMER_ID" ]; then
        echo "Customer ID is required."
        exit 1
      fi
      if [ -z "$NODE_IP" ]; then
        echo "Node IP is required."
        exit 1
      fi
      unregister_node_agent
    fi
    download_extract_package
    setup_symlink
    if [ "$SKIP_CONFIGURE" = "true" ]; then
      if [ -z "$NODE_IP" ]; then
        echo "Node IP is required."
        show_usage >&2
        exit 1
      fi
      node-agent node register --api_token "$API_TOKEN" --url "$PLATFORM_URL" --node_ip "$NODE_IP" \
      --node_port "$NODE_PORT" ${SKIP_VERIFY_CERT:+ "--skip_verify_cert"}
    else
      node-agent node configure --api_token "$API_TOKEN" --url "$PLATFORM_URL" \
      ${SKIP_VERIFY_CERT:+ "--skip_verify_cert"}
    fi
    if [ $? -ne 0 ]; then
      echo "Node agent setup failed."
      exit 1
    fi
  else
    err_msg "Invalid option: $TYPE. Must be one of ['install [--force] [--skip_verify_cert]', \
'install_service'].\n"
    show_usage >&2
    exit 1
  fi
}

if [[ ! $# -gt 0 ]]; then
  show_usage
  exit 1
fi

while [[ $# -gt 0 ]]; do
  case $1 in
    -t|--type)
      TYPE="$2"
      shift
    ;;
    --user)
      INSTALL_USER="$2"
      shift
    ;;
    --skip_verify_cert)
      SKIP_VERIFY_CERT="true"
    ;;
    --skip_configure)
      SKIP_CONFIGURE="true"
    ;;
    --force)
      FORCE_INSTALL="true"
    ;;
    -v|--version)
      VERSION=$2
      shift
    ;;
    -c|--customer_id)
      CUSTOMER_ID=$2
      shift
    ;;
    -u|--url)
      PLATFORM_URL="$2"
      shift
    ;;
    -ip|--node_ip)
      NODE_IP=$2
      shift
    ;;
    -p|--node_port)
      NODE_PORT=$2
      shift
      ;;
    -at|--api_token)
      API_TOKEN="$2"
      shift
    ;;
    --jwt)
      JWT="$2"
      shift
    ;;
    --cleanup)
      trap 'rm -- $0' EXIT
    ;;
    -h|--help)
      show_usage >&2
      exit 1
    ;;
    *)
      err_msg "x Invalid option: $1\n"
      show_usage >&2
      exit 1
  esac
  shift
done

if [ -z "$TYPE" ]; then
  show_usage >&2
  exit 1
fi

CURRENT_USER=$(id -u -n)

if [ -z "$INSTALL_USER" ]; then
  if [ "$TYPE" = "install_service" ]; then
    echo "Install user is required."
    show_usage >&2
    exit 1
  fi
  INSTALL_USER="$CURRENT_USER"
elif [ "$INSTALL_USER" != "$CURRENT_USER" ] && [ "$TYPE" != "install_service" ]; then
  echo "Different user can be passed only for installing service."
  exit 1
fi

INSTALL_USER_HOME=$(eval cd ~"$INSTALL_USER" && pwd)
NODE_AGENT_HOME="$INSTALL_USER_HOME/node-agent"
NODE_AGENT_PKG_DIR="$NODE_AGENT_HOME/pkg"
NODE_AGENT_RELEASE_DIR="$NODE_AGENT_HOME/release"

if [ "$TYPE" = "install" ]; then
  HEADER="$API_TOKEN_HEADER"
  HEADER_VAL="$API_TOKEN"
else
  HEADER="$JWT_HEADER"
  HEADER_VAL="$JWT"
fi

#Trim leading and trailing whitespaces.
PLATFORM_URL=$(echo "$PLATFORM_URL" | xargs)
API_TOKEN=$(echo "$API_TOKEN" | xargs)
JWT=$(echo "$JWT" | xargs)

NODE_AGENT_BASE_URL="$PLATFORM_URL/api/v1/customers/$CUSTOMER_ID/node_agents"
NODE_AGENT_DOWNLOAD_URL="$PLATFORM_URL/api/node_agents/download"

check_sudo_access
main

if [ "$?" -eq 0 ] && [ "$TYPE" = "install" ]; then
  if [ "$SUDO_ACCESS" = "false" ]; then
    echo "You can install a systemd service on linux machines\
 by running $INSTALLER_NAME -t install_service (Requires sudo access)."
  else
     install_systemd_service
  fi
fi
