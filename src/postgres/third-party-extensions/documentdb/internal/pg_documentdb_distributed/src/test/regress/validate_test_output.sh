#!/bin/bash

# fail if trying to reference a variable that is not set.
set -u -e

source="${BASH_SOURCE[0]}"
diff=/usr/bin/diff

pg_version=$1

while [[ -h $source ]]; do
   scriptroot="$( cd -P "$( dirname "$source" )" && pwd )"
   source="$(readlink "$source")"

   # if $source was a relative symlink, we need to resolve it relative to the path where the
   # symlink file was located
   [[ $source != /* ]] && source="$scriptroot/$source"
done

scriptDir="$( cd -P "$( dirname "$source" )" && pwd )"

# Validate index/runtime equivalence.
for validationFileName in $(ls ./expected/*_tests_index.out); do
    runtimeFileName=${validationFileName/_tests_index.out/_tests_runtime.out};

    $diff -s -I 'SELECT documentdb_api_internal.create_indexes' -I 'set local enable_seqscan' -I 'documentdb.next_collection_id' -I 'set local enable_bitmapscan' -I 'set local documentdb.forceUseIndexIfAvailable' -I 'set local citus.enable_local_execution' -I '\\set' -I 'set enable_seqscan'  -I 'set documentdb.forceUseIndexIfAvailable' -I 'documentdb.enableGeospatial' \
        $validationFileName $runtimeFileName;
    if [ $? -ne 0 ]; then echo "Validation failed on '${validationFileName}' against '${runtimeFileName}' error code $?"; exit 1; fi;
done

# Validate new composite index test equivalence
for validationFileName in $(ls ./expected/*_tests_index_composite.out); do
    runtimeFileName=${validationFileName/_tests_index_composite.out/_tests_runtime.out};

    $diff -s -I 'SET documentdb.next_collection_id' -I 'SET documentdb.next_collection_index_id' -I 'SET citus.next_shard_id' -I 'SELECT documentdb_api.create_collection' -I 'set documentdb.forceDisableSeqScan' -I 'SELECT documentdb_api_internal.create_indexes' -I 'set local documentdb.enableNewCompositeIndexOpClass' -I 'set local enable_seqscan' -I 'documentdb.next_collection_id' -I 'set local enable_bitmapscan' -I 'set local documentdb.forceUseIndexIfAvailable' -I 'set local citus.enable_local_execution' -I '\\set' -I 'set enable_seqscan'  -I 'set documentdb.forceUseIndexIfAvailable' -I 'documentdb.enableGeospatial' \
        $validationFileName $runtimeFileName;
    if [ $? -ne 0 ]; then echo "Validation failed on '${validationFileName}' against '${runtimeFileName}' error code $?"; exit 1; fi;
done

for validationFileName in $(ls ./expected/*_tests_index_comp_desc.out); do
    runtimeFileName=${validationFileName/_tests_index_comp_desc.out/_tests_runtime.out};

    $diff -s -I 'SET documentdb.next_collection_id' -I 'documentdb.enableDescendingCompositeIndex' -I 'SET documentdb.next_collection_index_id' -I 'SET citus.next_shard_id' -I 'SELECT documentdb_api.create_collection' -I 'set documentdb.forceDisableSeqScan' -I 'SELECT documentdb_api_internal.create_indexes' -I 'set local documentdb.enableNewCompositeIndexOpClass' -I 'set local enable_seqscan' -I 'documentdb.next_collection_id' -I 'set local enable_bitmapscan' -I 'set local documentdb.forceUseIndexIfAvailable' -I 'set local citus.enable_local_execution' -I '\\set' -I 'set enable_seqscan'  -I 'set documentdb.forceUseIndexIfAvailable' -I 'documentdb.enableGeospatial' \
        $validationFileName $runtimeFileName;
    if [ $? -ne 0 ]; then echo "Validation failed on '${validationFileName}' against '${runtimeFileName}' error code $?"; exit 1; fi;
done

for validationFileName in $(ls ./expected/*_tests_index_comp_wild.out); do
    runtimeFileName=${validationFileName/_tests_index_comp_wild.out/_tests_runtime.out};

    $diff -s -I 'SET documentdb.next_collection_id' -I 'documentdb.enableCompositeWildcardIndex' -I 'documentdb.enableDescendingCompositeIndex' -I 'SET documentdb.next_collection_index_id' -I 'SET citus.next_shard_id' -I 'SELECT documentdb_api.create_collection' -I 'set documentdb.forceDisableSeqScan' -I 'SELECT documentdb_api_internal.create_indexes' -I 'set local documentdb.enableNewCompositeIndexOpClass' -I 'set local enable_seqscan' -I 'documentdb.next_collection_id' -I 'set local enable_bitmapscan' -I 'set local documentdb.forceUseIndexIfAvailable' -I 'set local citus.enable_local_execution' -I '\\set' -I 'set enable_seqscan'  -I 'set documentdb.forceUseIndexIfAvailable' -I 'documentdb.enableGeospatial' \
        $validationFileName $runtimeFileName;
    if [ $? -ne 0 ]; then echo "Validation failed on '${validationFileName}' against '${runtimeFileName}' error code $?"; exit 1; fi;
done

for validationFileName in $(ls ./expected/*_tests_index_comp_unique.out); do
    runtimeFileName=${validationFileName/_tests_index_comp_unique.out/_tests_runtime.out};

    $diff -s -I 'SET documentdb.next_collection_id' -I 'documentdb.enableDescendingCompositeIndex' -I 'SET documentdb.next_collection_index_id' -I 'SET citus.next_shard_id' -I 'SELECT documentdb_api.create_collection' -I 'set documentdb.forceDisableSeqScan' -I 'SELECT documentdb_api_internal.create_indexes' -I 'set local documentdb.enableNewCompositeIndexOpClass' -I 'set local enable_seqscan' -I 'documentdb.next_collection_id' -I 'set local enable_bitmapscan' -I 'set local documentdb.forceUseIndexIfAvailable' -I 'set local citus.enable_local_execution' -I '\\set' -I 'set enable_seqscan'  -I 'set documentdb.forceUseIndexIfAvailable' -I 'documentdb.enableGeospatial' \
        $validationFileName $runtimeFileName;
    if [ $? -ne 0 ]; then echo "Validation failed on '${validationFileName}' against '${runtimeFileName}' error code $?"; exit 1; fi;
done

# Validate index_backcompat/index equivalence.
for validationFileName in $(ls ./expected/*_tests_index_backcompat.out); do
    indexFileName=${validationFileName/_tests_index_backcompat.out/_tests_index.out};
    $diff -s -I '\set' -I 'set local enable_seqscan' -I 'documentdb.next_collection_id' -I 'citus.next_shard_id' -I 'set documentdb.next_collection_index_id' -I 'set local documentdb.enableGenerateNonExistsTerm' -I 'set local enable_bitmapscan' -I 'set local documentdb.forceUseIndexIfAvailable' -I 'set local citus.enable_local_execution' -I '\\set' -I 'set enable_seqscan'  -I 'set documentdb.forceUseIndexIfAvailable' \
        $validationFileName $indexFileName;
    if [ $? -ne 0 ]; then echo "Validation failed on '${validationFileName}' against '${runtimeFileName}' error code $?"; exit 1; fi;
done

# Validate index (NoBitMap)/runtime equivalence.
for validationFileName in $(ls ./expected/*_tests_index_no_bitmap.out); do
    regularIndexFileName=${validationFileName/_tests_index_no_bitmap.out$/_tests_index.out};
    $diff -s -I 'set local enable_seqscan' -I 'set local enable_bitmapscan' -I 'set local documentdb.forceUseIndexIfAvailable' -I 'set local citus.enable_local_execution' -I '\\set' -I 'set enable_seqscan'  -I 'set documentdb.forceUseIndexIfAvailable' \
        $validationFileName $regularIndexFileName;
    if [ $? -ne 0 ]; then echo "Validation failed on '${validationFileName}' against '${runtimeFileName}' error code $?"; exit 1; fi;
done

# Validate entry has documentdb_api's set next collection id.
# These are baselined - will be cleaned up in a subsequent change.
aggregateCollectionIdStr=""
aggregateShardIdStr=""
maxCollectionIdStr=""

validationExceptions="/sql/documentdb_distributed_test_helpers.sql,/sql/public_api_schema.sql,/sql/documentdb_distributed_setup.sql,/sql/distributed_install_setup.sql"

skippedDuplicateCheckFile=""
echo "Validating test file output"
for validationFile in $(ls $scriptDir/expected/*.out); do
    fileName=$(basename $validationFile);
    fileNameBase="${fileName%.out}";
    sqlFile="${fileName%.out}.sql";
    sqlExceptionStr="/sql/$sqlFile"

    has_invalid_results=$(grep -E "No function matches the given name and argument types." $validationFile || true)
    if [ "$has_invalid_results" != "" ]; then
        if [ "$fileName" == "bson_deduplicate.out" ]; then
            echo "Skipping $fileName"
        else
            echo "test file $validationFile has invalid function specification: '$has_invalid_results'";
            exit 1
        fi
    fi

    # skip isolation test for now
    if [[ $fileName == isolation* ]]; then
        continue;
    fi;

    if [[ $validationExceptions =~ $sqlExceptionStr ]]; then
        continue;
    fi;

    # check if the base file is in the schedule
    findResult=""
    for macro in "" "!PG16_OR_HIGHER!" "!PG17_OR_HIGHER!"; do
        fileNameMod=$(echo "$fileNameBase" | sed -E "s/_tests/${macro}_tests/g")
        findResult=$(grep "$fileNameMod" basic_schedule_core || true)
        [ -n "$findResult" ] && break
    done

    if [ "$findResult" == "" ]; then
        for macro in "" "!PG16_OR_HIGHER!" "!PG17_OR_HIGHER!"; do
            fileNameMod=$(echo "$fileNameBase" | sed -E "s/_tests/_tests${macro}/g")
            findResult=$(grep "$fileNameMod" basic_schedule_core || true)
            [ -n "$findResult" ] && break
        done
    fi

    if [ "$findResult" == "" ]; then
        if [[ "$fileNameBase" =~ "pg15" ]] || [[ "$fileNameBase" =~ "pg16" ]] || [[ "$fileNameBase" =~ "pg17" ]] || [[ "$fileNameBase" =~ "_explain" ]]; then
            echo "Skipping schedule existence check for $fileNameBase"
        else
            echo "Test file '$validationFile' with name '$fileNameBase' or '$fileNameMod' is not in the schedule, please add it to the schedule";
            exit 1;
        fi
    fi

    isSimpleIncludeTest=false
    if [[ "$sqlFile" =~ _pg[0-9]+ ]]; then
        lineCount=$(cat $scriptDir/sql/$sqlFile | wc -l)
        if [ $lineCount -eq 0 ]; then
            isSimpleIncludeTest="true"
        fi
    fi

    # Extract the actual collection ID (we'll use this to check for uniqueness).
    collectionIdOutput=$(grep -m 1 'documentdb.next_collection_id' $validationFile || true)

    # Fail if not found.
    if [ "$isSimpleIncludeTest" == "true" ]; then
        echo "Skipping test file prefix validation on '${sqlFile}' due to it being a simple test"
    elif [ "$collectionIdOutput" == "" ]; then
        echo "Test file prefix Validation failed on '${sqlFile}': Please ensure test files set documentdb.next_collection_id";
        exit 1;
    fi;

    # Get the actual collection ID.
    collectionIdOutput=${collectionIdOutput/SET documentdb.next_collection_id TO/};
    collectionIdOutput=${collectionIdOutput/[\s|;]/};

    # Allow skipping unique checks
    skipUniqueCheck="false"
    if [[ "$sqlFile" =~ "tests_runtime.sql" ]] || [[ "$sqlFile" =~ "explain_index_comp_wild" ]] || [[ "$sqlFile" =~ "explain_index_composite" ]] || [[ "$sqlFile" =~ "explain_index_comp_desc.sql" ]] || [[ "$sqlFile" =~ "tests_index_no_bitmap.sql" ]] || [[ "$sqlFile" =~ "tests_index.sql" ]] ||  [[ "$sqlFile" =~ "tests_index_backcompat.sql" ]] || [[ "$sqlFile" =~ "tests_pg17_explain" ]] || [[ "$sqlFile" =~ "tests_explain_index.sql" ]] || [[ "$sqlFile" =~ "tests_explain_index_no_bitmap.sql" ]]; then
            skippedDuplicateCheckFile="$skippedDuplicateCheckFile $sqlFile"
            skipUniqueCheck="true"
    elif [[ "$sqlFile" =~ _pg[0-9]+ ]]; then
        echo "Skipping duplicate collectionId check for $sqlFile"
        skippedDuplicateCheckFile="$skippedDuplicateCheckFile $sqlFile"
        skipUniqueCheck="true"
    elif [[ "$aggregateCollectionIdStr" =~ ":$collectionIdOutput:" ]]; then
        echo "Duplicate CollectionId used in '$sqlFile' - please use unique collection Ids across tests: $collectionIdOutput. Current max: $maxCollectionIdStr";
        exit 1;
    else
        # Add it to the collection IDs being tracked.
        aggregateCollectionIdStr="$aggregateCollectionIdStr :$collectionIdOutput:"
    fi

    if ! [[ ":$collectionIdOutput:" =~ "0:" ]]; then
        echo "CollectionId used in '$sqlFile' must be a multiple of 10: " $collectionIdOutput;
        exit 1;
    fi

    if [[ "$maxCollectionIdStr" == "" ]]; then
        maxCollectionIdStr=$collectionIdOutput;
    elif [ $collectionIdOutput -gt $maxCollectionIdStr ]; then
        maxCollectionIdStr=$collectionIdOutput;
    fi

    # See if the index id is also set.
    collectionIndexIdOutput=$(grep -m 1 'documentdb.next_collection_index_id' $validationFile || true)
    if [ "$collectionIndexIdOutput" == "" ]; then
        echo "Test file '${sqlFile}' does not set next_collection_index_id: consider setting documentdb.next_collection_index_id";
        exit 1;
    fi;

    collectionIndexIdOutput=${collectionIndexIdOutput/SET documentdb.next_collection_index_id TO/};
    collectionIndexIdOutput=${collectionIndexIdOutput/[\s|;]/};
    if [ "$collectionIndexIdOutput" != "$collectionIdOutput" ]; then
        echo "CollectionId and CollectionIndexId used in '$sqlFile' must match. CollectionId: $collectionIdOutput, CollectionIndexId: $collectionIndexIdOutput";
        exit 1;
    fi

    # Validate citus.next_shard_id.
    nextShardIdOutput=$(grep 'citus.next_shard_id' $validationFile || true)
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
    if [[ "$skipUniqueCheck" == "true" ]]; then
        # do nothing
        continue;
    elif [[ "$aggregateShardIdStr" =~ ":$nextShardIdOutput:" ]]; then
        echo "Duplicate shard_id used in '$sqlFile' - please use unique shard ids across tests: " $nextShardIdOutput;
        exit 1;
    else
        aggregateShardIdStr="$aggregateShardIdStr :$nextShardIdOutput:"
    fi
done

# Now validate for every sql file there's an .out file unless excluded
for sqlFile in $(ls $scriptDir/sql/*.sql); do
    fileName=$(basename $sqlFile);
    fileNameBase="${fileName%.sql}";
    outFile="$scriptDir/expected/$fileNameBase.out";

    if [[ "$fileName" =~ "_core.sql" ]] || [[ "$fileName" == "bson_query_operator_tests_insert.sql" ]]; then
        echo "Skipping sql to out file validation on $fileName";
        continue;
    fi

    if [ ! -f "$outFile" ]; then
        echo "Test file '$sqlFile' does not have a corresponding expected output file '$outFile'. Please add to the schedule file and run tests.";
        exit 1;
    fi
done

echo "Skipped duplicate checks on $skippedDuplicateCheckFile"
echo "Validation checks done."