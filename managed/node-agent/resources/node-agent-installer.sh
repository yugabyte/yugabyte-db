#!/usr/bin/env bash
# Copyright 2020 YugabyteDB, Inc. and Contributors

set -euo pipefail

#Installation information.
INSTALL_USER=""
INSTALL_USER_HOME=""
INSTALL_PATH=""
NODE_AGENT_HOME=""
NODE_AGENT_PKG_PATH=""
NODE_AGENT_RELEASE_PATH=""
NODE_AGENT_PKG_TGZ_PATH=""
NODE_AGENT_CONFIG_PATH=""
NODE_AGENT_REGISTRY_PATH=""

# Yugabyte Anywhere SSL cert verification option.
SKIP_VERIFY_CERT=""
#Disable node to Yugabyte Anywhere connection.
DISABLE_EGRESS="false"
SILENT_INSTALL="false"
AIRGAP_INSTALL="false"
SKIP_PACKAGE_DOWNLOAD="false"
CERT_DIR=""
CUSTOMER_ID=""
NODE_NAME=""
NODE_IP=""
BIND_IP=""
NODE_PORT=""
API_TOKEN=""
PLATFORM_URL=""
PROVIDER_ID=""
INSTANCE_TYPE=""
REGION_NAME=""
ZONE_NAME=""
COMMAND=""
VERSION=""
NODE_AGENT_BASE_URL=""
NODE_AGENT_CERT_PATH=""
NODE_AGENT_DOWNLOAD_URL=""
NODE_AGENT_ID=""
NODE_AGENT_PKG_TGZ="node-agent.tgz"
API_TOKEN_HEADER="X-AUTH-YW-API-TOKEN"
INSTALLER_NAME="node-agent-installer.sh"
SYSTEMD_PATH="/etc/systemd/system"
SERVICE_NAME="yb-node-agent.service"
SERVICE_RESTART_INTERVAL_SEC=2
SERVICE_TIMEOUT_STOP_SEC=10
SESSION_INFO_URL=""
EXISTING_SYSTEMD_USER_ID=-1

ARCH=$(uname -m)
OS=$(uname -s)

CURRENT_USER=$(id -u -n)
CURRENT_USER_ID=$(id -u)

pushd () {
  command pushd "$@" > /dev/null
}

popd () {
  command popd > /dev/null
}

run_as_super_user() {
  if [ $(id -u) = 0 ]; then
    "$@"
  else
    sudo "$@"
  fi
  return $?
}

check_run_as_super_user() {
  run_as_super_user "$@"
  exit_code=$?
  if [ "$exit_code" -ne 0 ]; then
    echo "Command failed with exit code $exit_code - $@"
    exit $exit_code
  fi
}

# Function to run systemd commands as the target user
run_as_target_user() {
  XDG_RUNTIME_DIR=/run/user/$(id -u $INSTALL_USER) "$@"
}

export_path() {
  set +euo pipefail
  source "$INSTALL_USER_HOME"/.bashrc >/dev/null 2>&1
  if [[ ":$PATH:" != *":$1:"* ]]; then
    NEW_PATH="$1":\$PATH
    PATH="$1":$PATH
    echo "PATH=$NEW_PATH" >> "$INSTALL_USER_HOME"/.bashrc
    echo "export PATH" >> "$INSTALL_USER_HOME"/.bashrc
    export PATH
  fi
  set -euo pipefail
}

save_node_agent_home() {
  local node_agent_registry_path=$(dirname "$NODE_AGENT_REGISTRY_PATH")
  mkdir -p "$node_agent_registry_path"
  echo "node_agent_home: $NODE_AGENT_HOME" > "$NODE_AGENT_REGISTRY_PATH"
  chmod 600 "$NODE_AGENT_REGISTRY_PATH"
}

setup_node_agent_dir() {
  pushd "$INSTALL_PATH"
  echo "* Creating Node Agent Directory."
  #Create node-agent directory.
  mkdir -m 755 -p "$NODE_AGENT_HOME"
  echo "* Changing directory to node agent."
  #Change directory.
  pushd "$NODE_AGENT_HOME"
  echo "* Creating Sub Directories."
  mkdir -m 755 -p cert config logs release
  popd
  save_node_agent_home
  export_path "$NODE_AGENT_PKG_PATH/bin"
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
  set +e
  STATUS_CODE=$(curl -s ${SKIP_VERIFY_CERT:+ "-k"} -w "%{http_code}" -L --request GET \
    "$SESSION_INFO_URL" --header "$HEADER: $HEADER_VAL" --output "$RESPONSE_FILE"
    )
  if [ "$STATUS_CODE" != "200" ]; then
    rm -rf "$RESPONSE_FILE"
    echo "Fail to get session info. Status code $STATUS_CODE"
    exit 1
  fi
  CUSTOMER_ID="$(grep -o '"customerUUID":"[^"]*"' "$RESPONSE_FILE" | cut -d: -f2 | tr -d '"')"
  NODE_AGENT_BASE_URL="$PLATFORM_URL/api/v1/customers/$CUSTOMER_ID/node_agents"
  rm -rf "$RESPONSE_FILE"
  set -e
}

uninstall_node_agent() {
  local RESPONSE_FILE="/tmp/node_agent_${INSTALL_USER}.json"
  local STATUS_CODE=""
  set +e
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
  local NODE_AGENT_UUID=""
  NODE_AGENT_UUID="$(grep -o '"uuid":"[^"]*"' "$RESPONSE_FILE" | cut -d: -f2 | tr -d '"')"
  rm -rf "$RESPONSE_FILE"
  stop_systemd_service
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
  set -e
  echo "Node agent uninstalled successfully. Node agent directory can be removed manually."
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
    mkdir -p "$NODE_AGENT_RELEASE_PATH"
    pushd "$NODE_AGENT_RELEASE_PATH"
    local RESPONSE_CODE=""
    set +e
    RESPONSE_CODE=$(curl -s ${SKIP_VERIFY_CERT:+ "-k"} -w "%{http_code}" --location --request GET \
    "$NODE_AGENT_DOWNLOAD_URL?downloadType=package&os=$OS&arch=$GO_ARCH_TYPE" \
    --header "$HEADER: $HEADER_VAL" --output "$NODE_AGENT_PKG_TGZ")
    set -e
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
    pushd "$NODE_AGENT_RELEASE_PATH"
    set +o pipefail
    # Look for the folder containing the version file.
    VERSION=$(tar -tzf "$NODE_AGENT_PKG_TGZ" | grep "/version_metadata.json" | awk -F '/' \
    '$2{print $(NF-1);exit}')
    set -o pipefail
    if [ -z "$VERSION" ]; then
      echo "Node agent version cannot be determined"
      exit 1
    fi
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
  if [ -L "$NODE_AGENT_PKG_PATH" ]; then
    unlink "$NODE_AGENT_PKG_PATH"
  fi
  #Create a new symlink between node-agent/pkg -> node-agent/release/<version>.
  ln -s -f "$NODE_AGENT_RELEASE_PATH/$VERSION" "$NODE_AGENT_PKG_PATH"
}

modify_firewall() {
  set +e
  if command -v firewall-cmd >/dev/null 2>&1; then
    is_running=$(run_as_super_user firewall-cmd --state 2> /dev/null)
    if [ "$is_running" = "running" ]; then
        run_as_super_user firewall-cmd --add-port=${NODE_PORT}/tcp --permanent
        run_as_super_user systemctl restart firewalld
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
      run_as_super_user chcon -R -t bin_t "$NODE_AGENT_HOME"
    elif command -v yum >/dev/null 2>&1; then
      # Install the semanage package directly.
      run_as_super_user yum install -y policycoreutils-python-utils
      if ! command -v semanage >/dev/null 2>&1; then
        # Search and install the package that provides semanage.
        run_as_super_user yum install -y /usr/sbin/semanage
      fi
    elif command -v apt-get >/dev/null 2>&1; then
      run_as_super_user apt-get update -y
      run_as_super_user apt-get install -y semanage-utils
    fi
  fi
  # Check if semanage was installed in the previous steps.
  if command -v semanage >/dev/null 2>&1; then
    run_as_super_user semanage port -lC | grep -F "$NODE_PORT" >/dev/null 2>&1
    if [ "$?" -ne 0 ]; then
      check_run_as_super_user semanage port -a -t http_port_t -p tcp "$NODE_PORT"
    fi
    run_as_super_user semanage fcontext -lC | grep -F "$NODE_AGENT_HOME(/.*)?" >/dev/null 2>&1
    if [ "$?" -ne 0 ]; then
      check_run_as_super_user semanage fcontext -a -t bin_t "$NODE_AGENT_HOME(/.*)?"
    fi
    check_run_as_super_user restorecon -ir "$NODE_AGENT_HOME"
  else
    # Let it proceed as there can be policies to allow.
    echo "Command semanage does not exist. Defaulting to using chcon"
    check_run_as_super_user chcon -R -t bin_t "$NODE_AGENT_HOME"
  fi
  set -e
}

set_systemd_user_id() {
  set +e
  local UNIT_EXISTS=$(systemctl --user list-units | grep -F yb-node-agent.service)
  if [ -n "$UNIT_EXISTS" ]; then
    EXISTING_SYSTEMD_USER_ID=$CURRENT_USER_ID
  else
    UNIT_EXISTS=$(systemctl list-units | grep -F yb-node-agent.service)
    if [ -n "$UNIT_EXISTS" ]; then
      EXISTING_SYSTEMD_USER_ID=0
    fi
  fi
  set -e
}

stop_systemd_service() {
  if [ "$EXISTING_SYSTEMD_USER_ID" -eq -1 ]; then
    # Systemd unit does not exist.
    return
  fi
  local SCOPE_PARAM=""
  if [ "$EXISTING_SYSTEMD_USER_ID" -ne 0 ]; then
    SCOPE_PARAM="--user"
  fi
  set +e
  local UNIT_FILE=$(systemctl $SCOPE_PARAM cat $SERVICE_NAME --no-pager 2>/dev/null | head -1 | \
  sed -e 's/#\s*//g' || true)
  if [ "$EXISTING_SYSTEMD_USER_ID" -eq 0 ]; then
    if [ "$CURRENT_USER_ID" -ne 0 ]; then
      # Current user must be root as services are installed as root.
      echo "SUDO access is required to stop the root-level systemd service."
      echo "Uninstall node agent by running node-agent-installer.sh -c uninstall ... as root."
      exit 1
    fi
    run_as_super_user systemctl stop $SERVICE_NAME >/dev/null 2>&1
    run_as_super_user systemctl disable $SERVICE_NAME >/dev/null 2>&1
    run_as_super_user rm -f "$UNIT_FILE"
    run_as_super_user systemctl daemon-reload
  else
    run_as_target_user systemctl --user stop $SERVICE_NAME >/dev/null 2>&1
    run_as_target_user systemctl --user disable $SERVICE_NAME >/dev/null 2>&1
    rm -f "$UNIT_FILE"
    run_as_target_user systemctl --user daemon-reload
  fi
  set -e
}

install_systemd_service() {
  local USER_SCOPED_UNIT="false"
  local SCOPE_PARAM=""
  local SUDO_PARAM="sudo "
  if [ "$INSTALL_USER_ID" -ne 0 ]; then
    USER_SCOPED_UNIT="true"
    SCOPE_PARAM="--user"
    SUDO_PARAM=""
    SYSTEMD_PATH="$INSTALL_USER_HOME/.config/systemd/user"
  fi
  if [ "$USER_SCOPED_UNIT" = "false" ]; then
    if [ "$SE_LINUX_STATUS" = "Enforcing" ]; then
      modify_selinux
    fi
    modify_firewall
  fi
  echo "* Installing Node Agent Systemd Service"
  write_systemd_service_file "$USER_SCOPED_UNIT"
  echo "* Starting the systemd service"

  if [ "$USER_SCOPED_UNIT" = "false" ]; then
    #To enable the node-agent service on reboot.
    run_as_super_user systemctl enable $SERVICE_NAME
    run_as_super_user systemctl restart $SERVICE_NAME
  else
    run_as_target_user systemctl --user enable $SERVICE_NAME
    run_as_target_user systemctl --user restart $SERVICE_NAME
  fi

  echo "* Started the systemd service"
  echo "* Run '${SUDO_PARAM}systemctl $SCOPE_PARAM status $SERVICE_NAME' to check\
 the status of the $SERVICE_NAME"
  echo "* Run '${SUDO_PARAM}systemctl $SCOPE_PARAM stop $SERVICE_NAME' to stop\
 the $SERVICE_NAME service"
}

# Upgrade existing systemd service if it exists. Used internally.
upgrade_systemd_service() {
  if [ "$EXISTING_SYSTEMD_USER_ID" -eq -1 ]; then
    # User systemd unit does not exist.
    return
  fi
  echo "* Upgrading Node Agent Systemd Service"
  local USER_SCOPED_UNIT="true"
  # Not possible to have user systemd and root user.
  # If it is systemd user, the current user is always the systemd user.
  # So, check only the case for root systemd.
  if [ "$EXISTING_SYSTEMD_USER_ID" -eq 0 ]; then
    # Systemd unit is installed as root.
    if [ "$INSTALL_USER_ID" -ne 0 ]; then
      # Current user is non-root. It cannot do anything.
      return
    fi
    USER_SCOPED_UNIT="false"
  fi
  write_systemd_service_file "$USER_SCOPED_UNIT"
}

write_systemd_service_file() {
  local USE_USER_SYSTEMD="${1:-true}"
  local SERVICE_FILE_PATH="$SYSTEMD_PATH/$SERVICE_NAME"
  # Check if USER_SCOPED_UNIT is defined.
  if [ "$USE_USER_SYSTEMD" = "false" ]; then
    run_as_super_user tee "$SERVICE_FILE_PATH" <<-EOF
  [Unit]
  Description=YB Anywhere Node Agent
  After=network-online.target
  # Disable restart limits, using RestartSec to rate limit restarts.
  StartLimitInterval=0

  [Service]
  User=$INSTALL_USER
  WorkingDirectory=$NODE_AGENT_HOME
  LimitCORE=infinity
  LimitNOFILE=1048576
  LimitNPROC=12000
  ExecStart="$NODE_AGENT_PKG_PATH/bin/node-agent" server start
  Restart=always
  RestartSec=$SERVICE_RESTART_INTERVAL_SEC
  KillMode=process
  KillSignal=SIGTERM
  TimeoutStopSec=$SERVICE_TIMEOUT_STOP_SEC
  FinalKillSignal=SIGKILL

  [Install]
  WantedBy=default.target
EOF
  else
    SYSTEMD_PATH="$INSTALL_USER_HOME/.config/systemd/user"
    SERVICE_FILE_PATH="$SYSTEMD_PATH/$SERVICE_NAME"
    tee "$SERVICE_FILE_PATH" <<-EOF
  [Unit]
  Description=YB Anywhere Node Agent
  After=network-online.target

  [Service]
  WorkingDirectory=$NODE_AGENT_HOME
  LimitCORE=infinity
  LimitNOFILE=1048576
  LimitNPROC=12000
  ExecStart="$NODE_AGENT_PKG_PATH/bin/node-agent" server start
  Restart=always
  RestartSec=$SERVICE_RESTART_INTERVAL_SEC
  KillMode=process
  KillSignal=SIGTERM
  TimeoutStopSec=$SERVICE_TIMEOUT_STOP_SEC
  FinalKillSignal=SIGKILL

  [Install]
  WantedBy=default.target
EOF
  fi
  # Set the permissions after file creation. This is needed so that the service file
  # is executable during restart of systemd unit.
  chmod 755 "$SERVICE_FILE_PATH"
  if [ "$USE_USER_SYSTEMD" = "false" ]; then
    run_as_super_user systemctl daemon-reload
  else
    run_as_target_user systemctl --user daemon-reload
  fi
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
  -t, --api_token (REQUIRED for install and uninstall commands)
    Api token to download the build files.
  -ip, --node_ip (REQUIRED for uninstall command)
    Server IP.
  -p, --node_port (OPTIONAL for install command)
    Server port.
  --bind_ip (OPTIONAL if bind_ip is different than node_ip)
    Bind IP for the node agent to listen on.
  --user (REQUIRED only for install_service command for root systemd service)
    DEPRECATED: Username of the installation. A sudo user can install service for a non-sudo user.
  --skip_verify_cert (OPTIONAL)
    Specify to skip Yugabyte Anywhere server cert verification during install.
  --airgap (OPTIONAL)
    Specify to skip installing semanage utility.
  --silent (OPTIONAL for install command)
    Silent installation. By default, installation is interactive without this option.
  -h, --help
    Show usage.
EOT
}

show_silent_args() {
  cat <<-EOT

Required for silent installation:
  --node_ip for node ip.
  --node_name for node name.
  --provider_id for fully manual on-prem provider ID or name.
  --instance_type for instance type.
  --region_name for region name.
  --zone_name for zone name.
EOT
}

err_msg() {
  echo "$@" >&2
}

#Main entry function.
main() {
  echo "* Starting YB Node Agent $COMMAND."
  #Detect and set the systemd user ID.
  set_systemd_user_id
  if [ "$COMMAND" = "install_service" ]; then
    install_systemd_service
  elif [ "$COMMAND" = "upgrade" ]; then
    extract_package > /dev/null
    setup_symlink > /dev/null
    set_log_dir_permission > /dev/null
    upgrade_systemd_service > /dev/null
  elif [ "$COMMAND" = "install" ]; then
    local NODE_AGENT_CONFIG_ARGS=()
    if [ "$DISABLE_EGRESS" = "false" ]; then
      #Node agent can initiate connection to Yugabyte Anywhere.
      if [ "$INSTALL_USER_ID" -eq 0 ]; then
        # Allow only non-root user to run the installer for onprem.
        echo "Root user is not allowed to run $COMMAND."
        show_usage >&2
        exit 1
      fi
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
        if [ -z "$NODE_IP" ]; then
          echo "Node IP is required."
          show_silent_args >&2
          exit 1
        fi
        if [ -z "$NODE_NAME" ]; then
          echo "Node name is required."
          show_silent_args >&2
          exit 1
        fi
        if [ -z "$PROVIDER_ID" ]; then
          echo "Provider ID is required."
          show_silent_args >&2
          exit 1
        fi
        if [ -z "$INSTANCE_TYPE" ]; then
          echo "Instance type is required."
          show_silent_args >&2
          exit 1
        fi
        if [ -z "$REGION_NAME" ]; then
          echo "Region name is required."
          show_silent_args >&2
          exit 1
        fi
        if [ -z "$ZONE_NAME" ]; then
          echo "Zone name is required."
          show_silent_args >&2
          exit 1
        fi
      fi
      # Disable existing node-agent if it exists and is running.
      stop_systemd_service
      download_package
      NODE_AGENT_CONFIG_ARGS+=(--api_token "$API_TOKEN" --url "$PLATFORM_URL" \
      --node_port "$NODE_PORT" "${SKIP_VERIFY_CERT:+ "--skip_verify_cert"}")
      if [ "$SILENT_INSTALL" = "true" ]; then
        NODE_AGENT_CONFIG_ARGS+=(--silent --node_name "$NODE_NAME" --node_ip "$NODE_IP" \
        --provider_id "$PROVIDER_ID" --instance_type "$INSTANCE_TYPE" --region_name \
        "$REGION_NAME" --zone_name "$ZONE_NAME")
      fi
    else
        # This path is hidden from usage as this is used by YBA internally.
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
      if [ ! -d "$NODE_AGENT_CERT_PATH" ]; then
        echo "$NODE_AGENT_CERT_PATH is not found."
        exit 1
      fi
      # Disable existing node-agent if it exists and is running.
      stop_systemd_service
      NODE_AGENT_CONFIG_ARGS+=(--disable_egress --id "$NODE_AGENT_ID" --customer_id "$CUSTOMER_ID" \
      --cert_dir "$CERT_DIR" --node_name "$NODE_NAME" --node_ip "$NODE_IP" \
      --node_port "$NODE_PORT" "${SKIP_VERIFY_CERT:+ "--skip_verify_cert"}")
      # if bind ip is provided use that.
      if [ -n "$BIND_IP" ]; then
        NODE_AGENT_CONFIG_ARGS+=( --bind_ip "$BIND_IP" )
      fi
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
    if [ "$CURRENT_USER_ID" -ne 0 ] && [ "$EXISTING_SYSTEMD_USER_ID" -eq 0 ]; then
      echo "SUDO access is required to run the command $COMMAND."
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
    --install_path)
      INSTALL_PATH="$2"
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
    --skip_package_download)
      SKIP_PACKAGE_DOWNLOAD="true"
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
    --region_name)
      REGION_NAME="$2"
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
    --customer_id)
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
    --bind_ip)
      BIND_IP=$2
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

 if [ "$COMMAND" = "install_service" ]; then
  if [ -n "$INSTALL_USER" ] && [ "$CURRENT_USER_ID" -eq 0 ]; then
    # Root systemd service is not supported. Do not fail for backward compatibility.
    echo "--user option is deprecated and will be removed in future releases."
    echo "Systemd service should already be installed as the user running the installer."
    exit 0
  elif [ "$CURRENT_USER_ID" -eq 0 ]; then
    echo "Install user is required for install_service."
    show_usage >&2
    exit 1
  fi
elif [ -n "$INSTALL_USER" ]; then
  echo "--user option is not supported for command $COMMAND."
  show_usage >&2
  exit 1
fi

INSTALL_USER="$CURRENT_USER"
INSTALL_USER_ID="$CURRENT_USER_ID"
INSTALL_USER_HOME=$(eval cd ~"$INSTALL_USER" && pwd)

if [ -z "$INSTALL_PATH" ]; then
  INSTALL_PATH="$INSTALL_USER_HOME"
fi

NODE_AGENT_HOME="$INSTALL_PATH/node-agent"
NODE_AGENT_PKG_PATH="$NODE_AGENT_HOME/pkg"
NODE_AGENT_RELEASE_PATH="$NODE_AGENT_HOME/release"
NODE_AGENT_PKG_TGZ_PATH="$NODE_AGENT_RELEASE_PATH/$NODE_AGENT_PKG_TGZ"
NODE_AGENT_CERT_PATH="$NODE_AGENT_HOME/cert/$CERT_DIR"
NODE_AGENT_CONFIG_PATH="$NODE_AGENT_HOME/config/config.yml"
NODE_AGENT_REGISTRY_PATH="$INSTALL_USER_HOME"/.yugabyte/node-agent-registry.yml

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
if [ "$OS" = "Linux" ]; then
  set +e
  SE_LINUX_STATUS=$(getenforce 2>/dev/null)
  set -e
fi

main

if [ "$?" -eq 0 ] && [ "$COMMAND" = "install" ]; then
  # Always install the systemd service after a successful install.
  echo "Installing systemd service for node agent."
  install_systemd_service
fi
