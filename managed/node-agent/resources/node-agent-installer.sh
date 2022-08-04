#!/usr/bin/env bash
# Copyright 2020 YugaByte, Inc. and Contributors
ROOT_DIR="/home/yugabyte"
NODE_AGENT_DIR="$ROOT_DIR/node-agent"
NODE_AGENT_PKG_DIR="$NODE_AGENT_DIR/pkg"
NODE_AGENT_RELEASE_DIR="$NODE_AGENT_DIR/release"
NODE_AGENT_RUNNER_FILE="yb-node-agent.sh"
YUGABYTE_USER="yugabyte"
YUGABYTE_GROUP=$YUGABYTE_USER
API_TOKEN=""
PLATFORM_URL=""
TYPE=""
VERSION=""
JWT=""
API_TOKEN_HEADER="X-AUTH-YW-API-TOKEN"
JWT_HEADER="X-AUTH-YW-API-JWT"
INSTALLER_NAME="node-agent-installer.sh"

set -e

pushd () {
    command pushd "$@" > /dev/null
}

popd () {
    command popd "$@" > /dev/null
}

node_agent_dir_setup(){
    echo "* Creating Node Agent Directory"
    #create node-agent directory
    mkdir -p $NODE_AGENT_DIR

    #Copy installer script to the node-agent directory
    cp "$0" "$NODE_AGENT_DIR/$INSTALLER_NAME"

    #change permissions
    chmod 754 $NODE_AGENT_DIR


    echo "* Changing directory to node agent"
    #Change directory
    pushd $NODE_AGENT_DIR

    echo "* Creating Sub Directories"
    mkdir -p cert
    mkdir -p config
    mkdir -p logs
    mkdir -p release

    chmod -R 754 .

    echo "PATH=$PATH:$NODE_AGENT_PKG_DIR/bin" >> "$ROOT_DIR"/.bashrc
    export PATH="$NODE_AGENT_PKG_DIR/bin":$PATH

    popd
}

run_yb_node_agent_installer(){
    CURRENT_USER=$(whoami)

    if [ $CURRENT_USER != "$YUGABYTE_USER" ]; then
      echo "x You should be logged in as $YUGABYTE_USER user"
      exit 1
    fi

    USER_GROUP=$(id -gn $CURRENT_USER)
    if [ $USER_GROUP != "$YUGABYTE_GROUP" ]; then
      echo "x Yugabyte User must belong to Yugabyte Group."
      exit 1
    fi

    #Change to home directory
    cd $ROOT_DIR

    echo "* Starting YB Node Agent $TYPE"
    if [ "$TYPE" = "install" ]; then
      node_agent_dir_setup
    fi
    pushd $NODE_AGENT_RELEASE_DIR
    echo "* Downloading YB Node Agent build package"
    #Get OS version and Hardware info
    ARCH=$(uname -p)

    #Change x86_64 to amd64
    if [ $ARCH="x86_64" ]; then
      ARCH="amd64"
    fi
    OS=$(uname -s)

    BUILD_TAR="node-agent.tgz"

    if [ $TYPE = "install" ]; then
      HEADER=$API_TOKEN_HEADER
      HEADER_VAL=$API_TOKEN
    else
      HEADER=$JWT_HEADER
      HEADER_VAL=$JWT
    fi

    RESPONSE_CODE=$(curl -s -w "%{http_code}" --location --request GET "$PLATFORM_URL/api/node_agents/download?downloadType=package&os=$OS&arch=$ARCH" \
    --header "$HEADER: $HEADER_VAL" --output $BUILD_TAR
    )

    if [ $RESPONSE_CODE -ne 200 ]; then
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
    VERSION=$(tar -tzf $BUILD_TAR | head -2 | tail -1 | cut -f2 -d"/")

    echo "* Downloaded Version - $VERSION"
    #Untar the package
    echo "* Extracting the build package"
    #This will extract the build files to a directory named $VERSION
    #Packaging should take care of this
    tar -zxf $BUILD_TAR
    #Delete the installer gzip
    rm -rf $BUILD_TAR
}

show_usage() {
  cat <<-EOT
Usage: ${0##*/} [<options>]

Options:
  -t, --type (REQUIRED)
    Type of install to perform. Must be in ['install', 'upgrade'].
  -u, --url (REQUIRED)
    Platform URL
  -at, --api_token (REQUIRED with install type)
    Api token to download the build files
  --jwt (REQUIRED with upgrade type)
    Jwt required for upgrading the node agent
  -h, --help
    Show usage.
EOT
}

err_msg() {
  echo $@ >&2
}

if [[ ! $# -gt 0 ]]; then
  show_usage
  exit 1
fi

while [[ $# -gt 0 ]]; do
  case $1 in
    -t|--type)
      options="install upgrade"
      if [[ ! $options =~ (^|[[:space:]])"$2"($|[[:space:]]) ]]; then
        err_msg "Invalid option: $2. Must be one of ['install', 'upgrade'].\n"
        show_usage >&2
        exit 1
      fi
      TYPE="$2"
      shift
    ;;
    -u|--url)
      PLATFORM_URL="$2"
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
      trap "rm -- $0" EXIT
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

#Trim leading and trailing whitespaces
PLATFORM_URL=$(echo $PLATFORM_URL | xargs)
API_TOKEN=$(echo $API_TOKEN | xargs)
JWT=$(echo $JWT | xargs)

#Return error if type is not passed
if [ -z "$TYPE" ] || [ -z "$PLATFORM_URL" ]; then
  show_usage >&2
  exit 1
fi

if [ -z "$API_TOKEN" ] && [ "$TYPE" = "install" ]; then
    echo "Pass API Token"
    show_usage >&2
    exit 1
elif [ "$TYPE" = "upgrade" ] && [ -z "$JWT" ]; then
    echo "Pass JWT"
    show_usage >&2
    exit 1
fi

if [ "$TYPE" = "upgrade" ]; then
  run_yb_node_agent_installer >/dev/null
  echo "$VERSION"
else
  run_yb_node_agent_installer
  #Call the yb_node_agent.sh script to complete the registration/upgrade flow.
  source $NODE_AGENT_RELEASE_DIR/$VERSION/bin/$NODE_AGENT_RUNNER_FILE $TYPE $VERSION $PLATFORM_URL $API_TOKEN
fi
