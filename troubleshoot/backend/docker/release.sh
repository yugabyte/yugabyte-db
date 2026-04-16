#!/bin/bash
set -ex

SCRIPT_DIR=$(dirname "$(readlink -f "$0")")
TAG_TS=$(TZ=UTC date +"%Y-%m-%dT%H%M%SZ")

docker build --platform linux/amd64 \
    --tag quay.io/yugabyte/yb-troubleshooting-service:latest  \
    --tag quay.io/yugabyte/yb-troubleshooting-service:$TAG_TS  \
    -f ${SCRIPT_DIR}/Dockerfile \
    ${SCRIPT_DIR}/..

docker push quay.io/yugabyte/yb-troubleshooting-service:latest
docker push quay.io/yugabyte/yb-troubleshooting-service:$TAG_TS
