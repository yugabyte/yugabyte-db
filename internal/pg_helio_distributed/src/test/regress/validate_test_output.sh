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

# # Validate index/runtime equivalence.
# for validationFileName in $(ls ./expected/*_tests_index.out); do
#     runtimeFileName=${validationFileName/_tests_index.out/_tests_runtime.out};

#     $diff -s -I 'CALL helio_api.create_indexes' -I 'set local enable_seqscan' -I 'helio_api.next_collection_id' -I 'set local enable_bitmapscan' -I 'set local helio_api.forceUseIndexIfAvailable' -I 'set local citus.enable_local_execution' -I '\\set' -I 'set enable_seqscan'  -I 'set helio_api.forceUseIndexIfAvailable' -I 'helio_api.enableGeospatial' \
#         $validationFileName $runtimeFileName;
#     if [ $? -ne 0 ]; then echo "Validation failed on '${validationFileName}' against '${runtimeFileName}' error code $?"; exit 1; fi;
# done

# # Validate index_backcompat/index equivalence.
# for validationFileName in $(ls ./expected/*_tests_index_backcompat.out); do
#     indexFileName=${validationFileName/_tests_index_backcompat.out$/_tests_index.out};
#     $diff -s -I 'set local enable_seqscan' -I 'helio_api.next_collection_id' -I 'citus.next_shard_id' -I 'SET helio_api.next_collection_index_id' -I 'set local helio_api.enableGenerateNonExistsTerm' -I 'set local enable_bitmapscan' -I 'set local helio_api.forceUseIndexIfAvailable' -I 'set local citus.enable_local_execution' -I '\\set' -I 'set enable_seqscan'  -I 'set helio_api.forceUseIndexIfAvailable' \
#         $validationFileName $indexFileName;
#     if [ $? -ne 0 ]; then echo "Validation failed on '${validationFileName}' against '${runtimeFileName}' error code $?"; exit 1; fi;
# done

# # Validate index (NoBitMap)/runtime equivalence.
# for validationFileName in $(ls ./expected/*_tests_index_no_bitmap.out); do
#     regularIndexFileName=${validationFileName/_tests_index_no_bitmap.out$/_tests_index.out};
#     $diff -s -I 'set local enable_seqscan' -I 'set local enable_bitmapscan' -I 'set local helio_api.forceUseIndexIfAvailable' -I 'set local citus.enable_local_execution' -I '\\set' -I 'set enable_seqscan'  -I 'set helio_api.forceUseIndexIfAvailable' \
#         $validationFileName $regularIndexFileName;
#     if [ $? -ne 0 ]; then echo "Validation failed on '${validationFileName}' against '${runtimeFileName}' error code $?"; exit 1; fi;
# done

# Validate entry has helio_api's set next collection id.
# These are baselined - will be cleaned up in a subsequent change.
aggregateCollectionIdStr=""
aggregateShardIdStr=""
maxCollectionIdStr=""

# TODO: Investigate why these fail.
validationExceptions="/sql/helio_distributed_test_helpers.sql,/sql/and3.sql,/sql/andor.sql,/sql/and.sql,/sql/or9.sql,/sql/public_api_schema.sql"
for validationFile in $(ls ./expected/*.out); do
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

    # Allow skipping unique checks
    if [[ "$sqlFile" =~ "tests_index_no_bitmap.sql" ]] || [[ "$sqlFile" =~ "tests_index.sql" ]] ||  [[ "$sqlFile" =~ "tests_index_backcompat.sql" ]] || [[ "$sqlFile" =~ "tests_explain_index.sql" ]] || [[ "$sqlFile" =~ "tests_explain_index_no_bitmap.sql" ]]; then
            continue;
    fi

    # If it matches something seen before - fail.
    if [[ "$aggregateCollectionIdStr" =~ ":$collectionIdOutput:" ]]; then
        echo "Duplicate CollectionId used in '$sqlFile' - please use unique collection Ids across tests: $collectionIdOutput. Current Max: $maxCollectionIdStr";
        exit 1;
    fi

    if ! [[ ":$collectionIdOutput:" =~ "0:" ]]; then
        echo "CollectionId used in '$sqlFile' must be a multiple of 10: " $collectionIdOutput;
        exit 1;
    fi

    # Add it to the collection IDs being tracked.
    aggregateCollectionIdStr="$aggregateCollectionIdStr :$collectionIdOutput:"
    if [[ "$maxCollectionIdStr" == "" ]]; then
        maxCollectionIdStr=$collectionIdOutput;
    elif [ $collectionIdOutput -gt $maxCollectionIdStr ]; then
        maxCollectionIdStr=$collectionIdOutput;
    fi

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

    # Validate citus.next_shard_id.
    nextShardIdOutput=$(grep 'citus.next_shard_id' $sqlFilePath)
    if [ "$nextShardIdOutput" == "" ]; then
        echo "Test file '${sqlFile}' does not set citus.next_shard_id: consider setting citus.next_shard_id";
        exit 1;
    fi;

    nextShardIdOutput=${nextShardIdOutput/SET citus.next_shard_id TO/};
    nextShardIdOutput=${nextShardIdOutput/[\s|;]/};
    if ! [[ ":$nextShardIdOutput:" =~ ":${collectionIdOutput}0" ]]; then
        echo "citus.next_shard_id should be a multiple of CollectionId in '$sqlFile': CollectionId: $collectionIdOutput, shard_id: $nextShardIdOutput";
        exit 1;
    fi

    # Add it to the shard IDs being tracked.
    if [[ "$aggregateShardIdStr" =~ ":$nextShardIdOutput:" ]]; then
        echo "Duplicate shard_id used in '$sqlFile' - please use unique shard ids across tests: " $nextShardIdOutput;
        exit 1;
    fi
    aggregateShardIdStr="$aggregateShardIdStr :$nextShardIdOutput:"
done

echo "Validation checks done."