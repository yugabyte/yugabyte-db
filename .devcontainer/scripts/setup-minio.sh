#!/bin/bash

nohup minio server /tmp/minio-storage > /dev/null 2>&1 &

mc alias set local http://localhost:9000 $MINIO_ROOT_USER $MINIO_ROOT_PASSWORD

aws --endpoint-url http://localhost:9000 s3 mb s3://testbucket
