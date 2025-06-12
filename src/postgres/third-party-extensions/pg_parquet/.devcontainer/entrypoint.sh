#!/bin/bash

trap "echo 'Caught termination signal. Exiting...'; exit 0" SIGINT SIGTERM

# create azurite container
az storage container create -n $AZURE_TEST_CONTAINER_NAME --connection-string $AZURE_STORAGE_CONNECTION_STRING
az storage container create -n ${AZURE_TEST_CONTAINER_NAME}2 --connection-string $AZURE_STORAGE_CONNECTION_STRING

# create fake-gcs bucket
curl -v -X POST --data-binary "{\"name\":\"$GOOGLE_TEST_BUCKET\"}" -H "Content-Type: application/json" "$GOOGLE_SERVICE_ENDPOINT/storage/v1/b"
curl -v -X POST --data-binary "{\"name\":\"${GOOGLE_TEST_BUCKET}2\"}" -H "Content-Type: application/json" "$GOOGLE_SERVICE_ENDPOINT/storage/v1/b"

sleep infinity
