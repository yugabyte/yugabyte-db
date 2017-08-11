#!/bin/bash

set -euo pipefail

SCRIPT_DIR=$( cd "$( dirname "$0" )" && pwd )
POST_INSTALL_SCRIPT="$SCRIPT_DIR/post_install.sh"

if [ ! -f "$POST_INSTALL_SCRIPT" ];
then
  echo "Could not find post install script: $POST_INSTALL_SCRIPT"
  exit 1
fi

# Run the script
echo "Running post install script..."
$POST_INSTALL_SCRIPT
