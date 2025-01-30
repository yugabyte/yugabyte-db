#!/bin/bash

trap "echo 'Caught termination signal. Exiting...'; exit 0" SIGINT SIGTERM

# create azurite container
az storage container create -n $AZURE_TEST_CONTAINER_NAME --connection-string $AZURE_STORAGE_CONNECTION_STRING
az storage container create -n ${AZURE_TEST_CONTAINER_NAME}2 --connection-string $AZURE_STORAGE_CONNECTION_STRING

sleep infinity
