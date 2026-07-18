#!/bin/bash

# fail if trying to reference a variable that is not set.
set -u
# exit immediately if a command exits with a non-zero status
set -e

touch ~/.psqlrc;

if grep -Fq "documentdb_api_catalog" ~/.psqlrc; then
    echo "documentdb command already set"
else
    echo "SET search_path TO documentdb_core,documentdb_api_catalog,public;" >> ~/.psqlrc
fi

if grep -Fq "documentdb_core.bsonUseEJson" ~/.psqlrc; then
    echo "BsonTextRepresentation already set"
else
    echo "SET documentdb_core.bsonUseEJson TO true;" >> ~/.psqlrc
fi

export PSQLRC=~/.psqlrc