#!/bin/bash
#
# Copyright 2019 YugaByte, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

set -e

. "${BASH_SOURCE%/*}/common.sh"
PEX_ENV_FOLDER="pexEnv"

# Ansible PEX Generation Steps.
cd "$yb_devops_home/pex"

force="${1:-}"

if [[ "$force" == "--force" ]]; then
  # Remove existing pexEnv
  rm -rf "$PEX_ENV_FOLDER"
elif [[ -d $PEX_ENV_FOLDER ]]; then
  fatal "$PEX_ENV_FOLDER already generated, skipping recreation."
fi

echo "Rebuilding pex env for ansible."


# Build pex docker image if doesn't exist.
docker inspect "$DOCKER_PEX_IMAGE_NAME" > /dev/null 2>&1 || \
docker build -t "$DOCKER_PEX_IMAGE_NAME" .

# Execute the build_pex.sh script inside the built docker image
# to generate the repaired PEX.
docker run --rm -v "$yb_devops_home:/code" -u "$UID:$(id -g $UID)" \
"$DOCKER_PEX_IMAGE_NAME" -c "-r" "/code/ansible_python_requirements.txt"
