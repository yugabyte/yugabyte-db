#!/usr/bin/env bash

set -eu

SRC_ROOT="${BASH_SOURCE%/*}"

TARGET_ARCH=${TARGET_ARCH:-$(uname -m)}
TARGET_OS=${TARGET_OS:-almalinux8}
BUILD_IMAGE_TAG=${BUILD_IMAGE_TAG:-latest}
# export DOCKER_ORG='' to use a local docker image
DOCKER_ORG=${DOCKER_ORG:-docker.io/yugabyteci/}
pull="missing"
if [[ "$BUILD_IMAGE_TAG" == "latest" ]]; then
  pull="always"
fi

DKR_CMD=()
if command -v docker >/dev/null; then
  DKR_CMD=(docker)
elif command -v podman >/dev/null; then
  # We have to run podman with sudo or else the volume mounts are owned by root and not writeable
  # from within the container
  DKR_CMD=(sudo podman)
else
  echo "Could not find a suitable docker/podman command"
  exit 1
fi

# Determine if we are in an interactive shell or not
interactive_ops=("--interactive")
if tty -s; then
  interactive_ops+=("--tty")
fi

# We don't want these showing up as subcommands below from the 'set -x'
my_id=$(id -u)
my_group=$(id -g)
my_dir=$(pwd)

set -x
${DKR_CMD[@]} run ${interactive_ops[@]} \
  --cap-add SYS_PTRACE \
  --pull "$pull" \
  --env UID=${my_id} \
  --env GID=${my_group} \
  --ulimit core=-1 \
  --mount type=bind,source="${my_dir}",target="${my_dir}" \
  -w "${my_dir}" \
  ${DOCKER_ORG}yb_build_infra_${TARGET_OS}_${TARGET_ARCH}:${BUILD_IMAGE_TAG} \
    bash --login -c "$@"
