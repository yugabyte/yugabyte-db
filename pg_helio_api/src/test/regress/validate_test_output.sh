#!/bin/bash

# fail if trying to reference a variable that is not set.
set -u

source="${BASH_SOURCE[0]}"
diff=/usr/bin/diff

while [[ -h $source ]]; do
   scriptroot="$( cd -P "$( dirname "$source" )" && pwd )"
   source="$(readlink "$source")"

   # if $source was a relative symlink, we need to resolve it relative to the path where the
   # symlink file was located
   [[ $source != /* ]] && source="$scriptroot/$source"
done

scriptDir="$( cd -P "$( dirname "$source" )" && pwd )"

# Validate entry has tests's set next collection id.
aggregateCollectionIdStr=""
maxCollectionIdStr=""

validationExceptions="/sql/helio_test_helpers.sql"
for validationFile in $(ls $scriptDir/expected/*.out); do
    fileName=$(basename $validationFile);
    sqlFile="${fileName%.out}.sql";
    sqlFilePath="$scriptDir/sql/$sqlFile";
    sqlExceptionStr="/sql/$sqlFile"

    # skip isolation test for now
    if [[ $fileName == isolation* ]]; then
        continue;
    fi;

    if [[ $validationExceptions =~ $sqlExceptionStr ]]; then
        continue;
    fi;

    # Extract the actual collection ID (we'll use this to check for uniqueness).
    collectionIdOutput=$(grep 'helio_api.next_collection_id' $sqlFilePath)

    # Fail if not found.
    if [ "$collectionIdOutput" == "" ]; then
        echo "Test file prefix Validation failed on '${sqlFile}': Please ensure test files set helio_api.next_collection_id";
        exit 1;
    fi;

    # Get the actual collection ID.
    collectionIdOutput=${collectionIdOutput/SET helio_api.next_collection_id TO/};
    collectionIdOutput=${collectionIdOutput/[\s|;]/};

    # If it matches something seen before - fail.
    if [[ "$aggregateCollectionIdStr" =~ ":$collectionIdOutput:" ]]; then
        echo "Duplicate CollectionId used in '$sqlFile' - please use unique collection Ids across tests: $collectionIdOutput. Current max: $maxCollectionIdStr";
        exit 1;
    fi

    if ! [[ ":$collectionIdOutput:" =~ "00:" ]]; then
        echo "CollectionId used in '$sqlFile' must be a multiple of 100: " $collectionIdOutput;
        exit 1;
    fi

    if [[ "$maxCollectionIdStr" == "" ]]; then
        maxCollectionIdStr=$collectionIdOutput;
    elif [ $collectionIdOutput -gt $maxCollectionIdStr ]; then
        maxCollectionIdStr=$collectionIdOutput;
    fi

    # Add it to the collection IDs being tracked.
    aggregateCollectionIdStr="$aggregateCollectionIdStr :$collectionIdOutput:"

    # See if the index id is also set.
    collectionIndexIdOutput=$(grep 'helio_api.next_collection_index_id' $sqlFilePath)
    if [ "$collectionIndexIdOutput" == "" ]; then
        echo "Test file '${sqlFile}' does not set next_collection_index_id: consider setting helio_api.next_collection_index_id";
        exit 1;
    fi;

    collectionIndexIdOutput=${collectionIndexIdOutput/SET helio_api.next_collection_index_id TO/};
    collectionIndexIdOutput=${collectionIndexIdOutput/[\s|;]/};
    if [ "$collectionIndexIdOutput" != "$collectionIdOutput" ]; then
        echo "CollectionId and CollectionIndexId used in '$sqlFile' must match. CollectionId: $collectionIdOutput, CollectionIndexId: $collectionIndexIdOutput";
        exit 1;
    fi
done

echo "Validation checks done."