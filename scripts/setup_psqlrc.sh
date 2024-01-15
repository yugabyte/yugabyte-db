#!/bin/bash

# fail if trying to reference a variable that is not set.
set -u
# exit immediately if a command exits with a non-zero status
set -e

touch ~/.psqlrc;

if grep -Fq "helio_api_catalog" ~/.psqlrc; then
    echo ":heliodb command already set"
else
    echo "\set heliodb 'SET search_path TO helio_core,helio_api_catalog,public;'" >> ~/.psqlrc
fi

if grep -Fq "bsonUseEJson" ~/.psqlrc; then
    echo "BsonTextRepresentation already set"
else
    echo "SET helio_core.bsonUseEJson TO true;" >> ~/.psqlrc
fi

export PSQLRC=~/.psqlrc