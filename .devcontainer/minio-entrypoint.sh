#!/bin/bash

trap "echo 'Caught termination signal. Exiting...'; exit 0" SIGINT SIGTERM

minio server /data &

minio_pid=$!

while ! curl $AWS_ENDPOINT_URL; do
    echo "Waiting for $AWS_ENDPOINT_URL..."
    sleep 1
done

# set access key and secret key
mc alias set local $AWS_ENDPOINT_URL $MINIO_ROOT_USER $MINIO_ROOT_PASSWORD

# create bucket
mc mb local/$AWS_S3_TEST_BUCKET

wait $minio_pid
