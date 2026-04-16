#!/bin/bash
set -ex

SCRIPT_DIR=$(dirname "$(readlink -f "$0")")

docker run --rm -it \
    -e POSTGRES_PASSWORD=${POSTGRES_PASSWORD} \
    -e DB_HOST=host.docker.internal \
    -p 8080:8080 \
    --name ts-backend \
    $(docker build -q -f ${SCRIPT_DIR}/Dockerfile ${SCRIPT_DIR}/../)
