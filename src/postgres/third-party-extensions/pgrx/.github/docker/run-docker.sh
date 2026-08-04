#! /usr/bin/env bash

# Environment variables:
#   PG_MAJOR_VER: The major version of Postgres in which to build/run. E.g. 13, 14, 15
#   DOCKERFILE_ID: The Dockerfile identifier to be built, included in this repo,
#                  e.g. debian:bullseye or amazon:2
#   CARGO_LOCKED_OPTION: Set to '--locked' to use "cargo --locked", or set to
#                        blank '' to use "cargo" without "--locked"

# Examples of running this script in CI (currently Github Actions):
#   ./.github/docker/run-docker.sh 14 debian_bullseye
#   ./.github/docker/run-docker.sh 13 fedora

set -x

PG_MAJOR_VER=$1
DOCKERFILE_ID=$2

echo "Building docker container for PGRX using Postgres version $PG_MAJOR_VER in container $DOCKERFILE_ID"
echo "Cargo lock flag set to: '$CARGO_LOCKED_OPTION'"

docker build \
  --build-arg PG_MAJOR_VER="$PG_MAJOR_VER" \
  --build-arg CARGO_LOCKED_OPTION="$CARGO_LOCKED_OPTION" \
  -t pgrx \
  -f ".github/docker/Dockerfile.$DOCKERFILE_ID" \
  .

echo "Running PGRX test suite using Postgres version $PG_MAJOR_VER in container $DOCKERFILE_ID with 'cshim', 'proptest'"

docker run pgrx \
  cargo test \
  --no-default-features \
  --features "pg$PG_MAJOR_VER cshim proptest" \
  "$CARGO_LOCKED_OPTION"
