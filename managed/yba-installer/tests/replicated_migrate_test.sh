#!/bin/bash

print_help() {
  cat <<-EOT
Run a yba-ctl test to check that we can migrate from replicated to a yba-installer instance
Usage: ${0##*/} <options>
Options:
  -h, --help
    Show usage.
  -b, --build_ybactl
    build and use a custom yba-ctl
  -i, --ipaddr <ip_address> [required]
    ip address of the vm to use
  -u, --username <username>
    username to log into the vm. defaults to centos. Needs ec2-user for aws
  -v, --version <version> [required]
    the version of yba_installer_full to download and use
  --download_directory </path/to/download_directory>
    We store all yba-installer-full downloads here. defaults to ~/downloads
  --license_path <yba-ctl license file>
    Path to the yba installer license file. Defaults to ${download_directory}/yugabyte_anywhere.lic
  --skip_repl
    Skip the replicated install
EOT
}

BUILD_YBACTL=0
IP_ADDR=""
USERNAME="centos"
VERSION=""
DOWNLOAD_DIR="${HOME}/downloads/"
LICENSE_PATH="${DOWNLOAD_DIR}/yugabyte_anywhere.lic"
INSTALL_REPL=1

while [ $# -gt 0 ]; do
  case "$1" in
    -h|--help)
      print_help
      exit 0
    ;;
    -b|--build_ybactl)
      BUILD_YBACTL=1
    ;;
    -i|--ipaddr)
      IP_ADDR="$2"
      shift
    ;;
    -u|--username)
      USERNAME="$2"
      shift
    ;;
    -v|--version)
      VERSION="$2"
      shift
    ;;
    --download_directory)
      DOWNLOAD_DIR="$2"
      shift
    ;;
    --license_path)
      LICENSE_PATH="$2"
      shift
    ;;
    --skip_repl)
      INSTALL_REPL=0
  esac
  shift
done

################################################################################
# Setting Global Variables #####################################################
################################################################################

path=$(readlink -f "${BASH_SOURCE:-$0}")
DIR_PATH=$(dirname $path)
TOP=${DIR_PATH}/..

YBA_FULL_NAME="yba_installer_full-${VERSION}-centos-x86_64.tar.gz"
YBA_FULL_DIR="yba_installer_full-${VERSION}"
YBA_FULL_LOCAL="${DOWNLOAD_DIR}/${YBA_FULL_NAME}"

LICENSE_FILE=$(basename ${LICENSE_PATH})

SSH_OPTS="-i $HOME/.yugabyte/yb-dev-aws-2.pem -o UserknownHostsFile=/dev/null -o \
StrictHostKeychecking=no"

################################################################################
# Helper Functions Here ########################################################
################################################################################

# Copy a file to the specified vm
copy_file(){
  FILE=$1
  echo "scp ${FILE}"
  set -e
  scp ${SSH_OPTS} ${FILE} ${USERNAME}@${IP_ADDR}:~/
  set +e
  echo "scp complete"
}

# Run a command on the specified vm
remote_exec() {
  CMD=$1
  USE_TTY=$2
  echo "executing \"${CMD}\" on ${IP_ADDR}"
  set -e
  if [[ "x${USE_TTY}" != "x" ]]; then
    ssh -tt ${SSH_OPTS} ${USERNAME}@${IP_ADDR} ${CMD}
  else
    ssh ${SSH_OPTS} ${USERNAME}@${IP_ADDR} ${CMD}
  fi
  set +e
  echo "\"${CMD}\" is complete"
}

################################################################################
# Test Workflow ################################################################
################################################################################

# Install Replicated
if [[ "${INSTALL_REPL}" == "1" ]]; then
  echo "Installing replicated"
  remote_exec "curl -sSL -o install.sh https://get.replicated.com/docker/testappyugaware/unstable"
  remote_exec "sudo bash ./install.sh" "y" # Execute with tty
else
  echo "skipping replicated install"
fi

# Copy the license
copy_file ${LICENSE_PATH}

# Check if the yba full tarball exists in downloads, if not download it
if [[ -f "${YBA_FULL_LOCAL}" ]]; then
  echo "downloading yba_full from aws s3"
  aws s3 cp s3://releases.yugabyte.com/${VERSION}/${YBA_FULL_NAME} ${DOWNLOAD_DIR}
else
  echo "${YBA_FULL_NAME} already downloaded"
fi

# Copy and untar the yba_full
copy_file ${YBA_FULL_LOCAL}
remote_exec "tar -xf ${YBA_FULL_NAME}"

if [[ "${BUILD_YBACTL}" == "1" ]]; then
  echo "Building local yba-ctl"
  pushd ${TOP}
  rm -f bin/yba-ctl
  make yba-ctl
  copy_file bin/yba-ctl
  remote_exec "mv yba-ctl ${YBA_FULL_DIR}/yba-ctl"
else
  echo "using default yba-ctl"
fi

echo "Run the replicated migration"
remote_exec "cd ${YBA_FULL_DIR}; sudo ./yba-ctl replicated-migrate start -l ~/${LICENSE_FILE}"
