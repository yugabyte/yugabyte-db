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

. "${BASH_SOURCE%/*}"/bin/common.sh

# Ansible PEX Generation Steps.
cd "$yb_devops_home/pex"

# Remove existing pexEnv
rm -rf "pexEnv"

# Build pex docker image if doesn't exist.
docker inspect "$DOCKER_IMAGE_NAME" > /dev/null 2>&1 || docker build -t "$DOCKER_IMAGE_NAME" .

# Execute the build_pex.sh script inside the built docker image
# to generate the repaired PEX.
docker run -v "$yb_devops_home:/code" -u "$UID:$(id -g $UID)" \
"$DOCKER_IMAGE_NAME" -c "-r" "/code/ansible_python3_requirements.txt"
