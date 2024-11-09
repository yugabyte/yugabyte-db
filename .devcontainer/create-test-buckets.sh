#!/bin/bash

aws --endpoint-url http://localhost:9000 s3 mb s3://$AWS_S3_TEST_BUCKET
