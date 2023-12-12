#!/usr/bin/env bash
# Copyright 2020 YugaByte, Inc. and Contributors

set -euo pipefail

#Installation information.
INSTALL_USER=""
INSTALL_USER_HOME=""
NODE_AGENT_HOME=""
NODE_AGENT_PKG_DIR=""
NODE_AGENT_RELEASE_DIR=""
NODE_AGENT_PKG_TGZ_PATH=""
NODE_AGENT_CONFIG_FILEPATH=""

# Yugabyte Anywhere SSL cert verification option.
SKIP_VERIFY_CERT=""
#Disable node to Yugabyte Anywhere connection.
DISABLE_EGRESS="false"
SILENT_INSTALL="false"
AIRGAP_INSTALL="false"
CERT_DIR=""
CUSTOMER_ID=""
NODE_NAME=""
NODE_IP=""
NODE_PORT=""
API_TOKEN=""
PLATFORM_URL=""
PROVIDER_ID=""
INSTANCE_TYPE=""
ZONE_NAME=""
COMMAND=""
VERSION=""
NODE_AGENT_BASE_URL=""
NODE_AGRNT_CERT_PATH=""
NODE_AGENT_DOWNLOAD_URL=""
NODE_AGENT_ID=""
NODE_AGENT_PKG_TGZ="node-agent.tgz"
API_TOKEN_HEADER="X-AUTH-YW-API-TOKEN"
INSTALLER_NAME="node-agent-installer.sh"
SYSTEMD_DIR="/etc/systemd/system"
SERVICE_NAME="yb-node-agent.service"
SERVICE_RESTART_INTERVAL_SEC=2
SESSION_INFO_URL=""

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

setup_node_agent_dir() {
  pushd "$INSTALL_USER_HOME"
  echo "* Creating Node Agent Directory."
  #Create node-agent directory.
  mkdir -p "$NODE_AGENT_HOME"
  #Change permissions.
  chmod 755 "$NODE_AGENT_HOME"
  echo "* Changing directory to node agent."
  #Change directory.
  pushd "$NODE_AGENT_HOME"
  echo "* Creating Sub Directories."
  mkdir -p cert config logs release
  chmod -R 755 .
  popd
  add_path "$NODE_AGENT_PKG_DIR/bin"
  popd
}

set_log_dir_permission() {
  #Change directory.
  pushd "$NODE_AGENT_HOME"
  chmod 755 logs
  popd
}

set_node_agent_base_url() {
  local RESPONSE_FILE="/tmp/session_info_${INSTALL_USER}.json"
  local STATUS_CODE=""
  STATUS_CODE=$(curl -s ${SKIP_VERIFY_CERT:+ "-k"} -w "%{http_code}" -L --request GET \
    "$SESSION_INFO_URL" --header "$HEADER: $HEADER_VAL" --output "$RESPONSE_FILE"
    )
  if [ "$STATUS_CODE" != "200" ]; then
    rm -rf "$RESPONSE_FILE"
    echo "Fail to get session info. Status code $STATUS_CODE"
    exit 1
  fi
  set +e
  CUSTOMER_ID="$(grep -o '"customerUUID":"[^"]*"' "$RESPONSE_FILE" | cut -d: -f2 | tr -d '"')"
  NODE_AGENT_BASE_URL="$PLATFORM_URL/api/v1/customers/$CUSTOMER_ID/node_agents"
  rm -rf "$RESPONSE_FILE"
  set -e
}

uninstall_node_agent() {
  local RESPONSE_FILE="/tmp/node_agent_${INSTALL_USER}.json"
  local STATUS_CODE=""
  STATUS_CODE=$(curl -s ${SKIP_VERIFY_CERT:+ "-k"} -w "%{http_code}" -L --request GET \
    "$NODE_AGENT_BASE_URL?nodeIp=$NODE_IP" --header "$HEADER: $HEADER_VAL" \
    --output "$RESPONSE_FILE"
    )
  if [ "$STATUS_CODE" != "200" ]; then
    rm -rf "$RESPONSE_FILE"
    echo "Fail to check existing node agent. Status code $STATUS_CODE"
    exit 1
  fi
  # Command jq is not available.
  # Continue after pipefail.
  set +e
  local NODE_AGENT_UUID=""
  NODE_AGENT_UUID="$(grep -o '"uuid":"[^"]*"' "$RESPONSE_FILE" | cut -d: -f2 | tr -d '"')"
  rm -rf "$RESPONSE_FILE"
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

download_package() {
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
    mkdir -p "$NODE_AGENT_RELEASE_DIR"
    pushd "$NODE_AGENT_RELEASE_DIR"
    local RESPONSE_CODE=""
    RESPONSE_CODE=$(curl -s ${SKIP_VERIFY_CERT:+ "-k"} -w "%{http_code}" --location --request GET \
    "$NODE_AGENT_DOWNLOAD_URL?downloadType=package&os=$OS&arch=$GO_ARCH_TYPE" \
    --header "$HEADER: $HEADER_VAL" --output "$NODE_AGENT_PKG_TGZ")
    popd
    if [ "$RESPONSE_CODE" -ne 200 ]; then
      echo "x Error while downloading the node agent build package"
      exit 1
    fi
}

extract_package() {
    #Get the version from the tar.
    #Note: This method of fetching the version from the tar depends on the packaging.
    #This might break if the packaging changes in the future.
    #Expected tar dir structure is as follows:
    #./
    #./<version>/
    #./<version>/*
    pushd "$NODE_AGENT_RELEASE_DIR"
    set +o pipefail
    VERSION=$(tar -tzf "$NODE_AGENT_PKG_TGZ" | awk -F '/' '$2{print $2; exit}')
    set -o pipefail

    echo "* Downloaded Version - $VERSION"
    #Untar the package.
    echo "* Extracting the build package"
    #This will extract the build files to a directory named $VERSION.
    #Packaging should take care of this.
    tar --no-same-owner -zxf "$NODE_AGENT_PKG_TGZ"
    #Delete the installer tar file.
    rm -rf "$NODE_AGENT_PKG_TGZ"
    popd
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
  if sudo -n pwd >/dev/null 2>&1; then
    SUDO_ACCESS="true"
  fi
  if [ "$OS" = "Linux" ]; then
    SE_LINUX_STATUS=$(getenforce 2>/dev/null)
  fi
  set -e
}

modify_firewall() {
  set +e
  if command -v firewall-cmd >/dev/null 2>&1; then
    is_running=$(sudo firewall-cmd --state 2> /dev/null)
    if [ "$is_running" = "running" ]; then
        sudo firewall-cmd --add-port=${NODE_PORT}/tcp --permanent \
        && sudo systemctl restart firewalld
    fi
  fi
  set -e
}

modify_selinux() {
  set +e
  if ! command -v semanage >/dev/null 2>&1; then
    if [ "$AIRGAP_INSTALL" = "true" ]; then
      # The changes made with chcon are temporary in the sense that the context of the file
      # altered with chcon goes back to default when restorecon is run.
      # It should not even try to reach out to the repo.
      sudo chcon -R -t bin_t "$NODE_AGENT_HOME"
    else
      if command -v yum >/dev/null 2>&1; then
        sudo yum install -y policycoreutils-python-utils
      elif command -v apt-get >/dev/null 2>&1; then
        sudo apt-get update -y
        sudo apt-get install -y semanage-utils
      fi
    fi
  fi
  # Check if semanage was installed in the previous steps.
  if command -v semanage >/dev/null 2>&1; then
    sudo semanage port -lC | grep -F "$NODE_PORT" >/dev/null 2>&1
    if [ "$?" -ne 0 ]; then
      sudo semanage port -a -t http_port_t -p tcp "$NODE_PORT"
    fi
    sudo semanage fcontext -lC | grep -F "$NODE_AGENT_HOME(/.*)?" >/dev/null 2>&1
    if [ "$?" -ne 0 ]; then
      sudo semanage fcontext -a -t bin_t "$NODE_AGENT_HOME(/.*)?"
    fi
    sudo restorecon -ir "$NODE_AGENT_HOME"
  fi
  set -e
}

install_systemd_service() {
  if [ "$SE_LINUX_STATUS" = "Enforcing" ]; then
    modify_selinux
  fi
  modify_firewall
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
  sudo systemctl restart yb-node-agent
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
  -c, --command (REQUIRED)
    Command to run. Must be in ['install', 'uninstall', 'install_service'].
  -u, --url (REQUIRED)
    Yugabyte Anywhere URL.
  -t, --api_token (REQUIRED with install command)
    Api token to download the build files.
  -p, --node_port (OPTIONAL with install command)
    Server port.
  --user (REQUIRED only for install_service command)
    Username of the installation. A sudo user can install service for a non-sudo user.
  --skip_verify_cert (OPTIONAL)
    Specify to skip Yugabyte Anywhere server cert verification during install.
  --airgap (OPTIONAL)
    Specify to skip installing semanage utility.
  -h, --help
    Show usage.
EOT
}

err_msg() {
  echo "$@" >&2
}

#Main entry function.
main() {
  echo "* Starting YB Node Agent $COMMAND."
  if [ "$COMMAND" = "install_service" ]; then
    if [ "$SUDO_ACCESS" = "false" ]; then
      echo "SUDO access is required."
      exit 1
    fi
    install_systemd_service
  elif [ "$COMMAND" = "upgrade" ]; then
    extract_package > /dev/null
    setup_symlink > /dev/null
    set_log_dir_permission > /dev/null
  elif [ "$COMMAND" = "install" ]; then
    local NODE_AGENT_CONFIG_ARGS=()
    if [ "$DISABLE_EGRESS" = "false" ]; then
      #Node agent can initiate connection to Yugabyte Anywhere.
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
      #For non-silent, the following inputs are read interactively.
      if [ "$SILENT_INSTALL" = "true" ]; then
        # This mode is hidden from usage.
        if [ -z "$NODE_NAME" ]; then
          echo "Node name is required."
          exit 1
        fi
        if [ -z "$PROVIDER_ID" ]; then
          echo "Provider ID is required."
          exit 1
        fi
        if [ -z "$INSTANCE_TYPE" ]; then
          echo "Instance type is required."
          exit 1
        fi
        if [ -z "$ZONE_NAME" ]; then
          echo "Zone name is required."
          exit 1
        fi
      fi
      download_package
      NODE_AGENT_CONFIG_ARGS+=(--api_token "$API_TOKEN" --url "$PLATFORM_URL" \
      --node_port "$NODE_PORT" "${SKIP_VERIFY_CERT:+ "--skip_verify_cert"}")
      if [ "$SILENT_INSTALL" = "true" ]; then
        NODE_AGENT_CONFIG_ARGS+=(--silent --node_name "$NODE_NAME" --node_ip "$NODE_IP" \
        --provider_id "$PROVIDER_ID" --instance_type "$INSTANCE_TYPE" --zone_name "$ZONE_NAME")
      fi
    else
        # This path is hidden from usage.
      if [ -z "$CUSTOMER_ID" ]; then
        echo "Customer ID is required."
        exit 1
      fi
      if [ -z "$NODE_NAME" ]; then
        echo "Node name is required."
        exit 1
      fi
      if [ -z "$NODE_IP" ]; then
        echo "Node IP is required."
        exit 1
      fi
      if [ -z "$CERT_DIR" ]; then
        echo "Cert directory is required."
        exit 1
      fi
      if [ -z "$NODE_AGENT_ID" ]; then
        echo "Cert directory is required."
        exit 1
      fi
      if [ ! -f "$NODE_AGENT_PKG_TGZ_PATH" ]; then
        echo "$NODE_AGENT_PKG_TGZ_PATH is not found."
        exit 1
      fi
      if [ ! -d "$NODE_AGRNT_CERT_PATH" ]; then
        echo "$NODE_AGRNT_CERT_PATH is not found."
        exit 1
      fi
      if [ "$SUDO_ACCESS" = "true" ]; then
        # Disable existing node-agent if sudo access is available.
        local RUNNING=""
        set +e
        RUNNING=$(systemctl list-units | grep -F yb-node-agent.service)
        if [ -n "$RUNNING" ]; then
          sudo systemctl stop yb-node-agent
          sudo systemctl disable yb-node-agent
        fi
        set -e
      fi
      NODE_AGENT_CONFIG_ARGS+=(--disable_egress --id "$NODE_AGENT_ID" --customer_id "$CUSTOMER_ID" \
      --cert_dir "$CERT_DIR" --node_name "$NODE_NAME" --node_ip "$NODE_IP" \
      --node_port "$NODE_PORT" "${SKIP_VERIFY_CERT:+ "--skip_verify_cert"}")
    fi
    setup_node_agent_dir
    extract_package
    setup_symlink
    node-agent node configure ${NODE_AGENT_CONFIG_ARGS[@]}
    if [ $? -ne 0 ]; then
      echo "Node agent setup failed."
      exit 1
    fi
    echo "Source ~/.bashrc to make node-agent available in the PATH."
  elif [ "$COMMAND" = "uninstall" ]; then
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
    if [ -z "$NODE_IP" ]; then
      echo "Node IP is required."
      show_usage >&2
      exit 1
    fi
    if [ "$SUDO_ACCESS" = "false" ]; then
      echo "SUDO access is required."
      show_usage >&2
      exit 1
    fi
    set_node_agent_base_url
    uninstall_node_agent
  else
    err_msg "Invalid option: $COMMAND. Must be one of ['install, uninstall, \
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
    -c|--command)
      COMMAND="$2"
      shift
    ;;
    --user)
      INSTALL_USER="$2"
      shift
    ;;
    --skip_verify_cert)
      SKIP_VERIFY_CERT="true"
    ;;
    --disable_egress)
      DISABLE_EGRESS="true"
    ;;
    --silent)
      SILENT_INSTALL="true"
    ;;
    --airgap)
      AIRGAP_INSTALL="true"
    ;;
    --node_name)
      NODE_NAME="$2"
      shift
    ;;
    --provider_id)
      PROVIDER_ID="$2"
      shift
    ;;
    --instance_type)
      INSTANCE_TYPE="$2"
      shift
    ;;
    --zone_name)
      ZONE_NAME="$2"
      shift
    ;;
    --id)
      NODE_AGENT_ID="$2"
      shift
    ;;
    --cert_dir)
      CERT_DIR="$2"
      shift
    ;;
    -c|--customer_id)
      CUSTOMER_ID="$2"
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
    -t|--api_token)
      API_TOKEN="$2"
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

if [ -z "$COMMAND" ]; then
  show_usage >&2
  exit 1
fi

CURRENT_USER=$(id -u -n)

if [ -z "$INSTALL_USER" ]; then
  if [ "$COMMAND" = "install_service" ]; then
    echo "Install user is required."
    show_usage >&2
    exit 1
  fi
  INSTALL_USER="$CURRENT_USER"
elif [ "$INSTALL_USER" != "$CURRENT_USER" ] && [ "$COMMAND" != "install_service" ]; then
  show_usage >&2
  echo "Different user can be passed only for installing service."
  exit 1
fi

INSTALL_USER_HOME=$(eval cd ~"$INSTALL_USER" && pwd)
NODE_AGENT_HOME="$INSTALL_USER_HOME/node-agent"
NODE_AGENT_PKG_DIR="$NODE_AGENT_HOME/pkg"
NODE_AGENT_RELEASE_DIR="$NODE_AGENT_HOME/release"
NODE_AGENT_PKG_TGZ_PATH="$NODE_AGENT_RELEASE_DIR/$NODE_AGENT_PKG_TGZ"
NODE_AGRNT_CERT_PATH="$NODE_AGENT_HOME/cert/$CERT_DIR"
NODE_AGENT_CONFIG_PATH="$NODE_AGENT_HOME/config/config.yml"

if [ -z "$NODE_PORT" ]; then
  if [ -f "$NODE_AGENT_CONFIG_PATH" ]; then
    NODE_PORT=$(cat "$NODE_AGENT_CONFIG_PATH" | awk -F":" '/port/{gsub(/[" ]/, "", $2); print $2}')
  fi
fi

if [ -z "$NODE_PORT" ]; then
  NODE_PORT=9070
fi

echo "Using node agent port $NODE_PORT."

HEADER="$API_TOKEN_HEADER"
HEADER_VAL="$API_TOKEN"

#Trim leading and trailing whitespaces.
PLATFORM_URL=$(echo "$PLATFORM_URL" | xargs)
API_TOKEN=$(echo "$API_TOKEN" | xargs)

NODE_AGENT_DOWNLOAD_URL="$PLATFORM_URL/api/v1/node_agents/download"
SESSION_INFO_URL="$PLATFORM_URL/api/v1/session_info"

check_sudo_access
main

if [ "$?" -eq 0 ] && [ "$COMMAND" = "install" ]; then
  if [ "$SUDO_ACCESS" = "false" ]; then
    echo "You can install a systemd service on linux machines\
 by running $INSTALLER_NAME -c install_service --user yugabyte (Requires sudo access)."
  else
     install_systemd_service
  fi
fi
