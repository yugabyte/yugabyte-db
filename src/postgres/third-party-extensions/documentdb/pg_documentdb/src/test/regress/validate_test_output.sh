#!/bin/bash

# fail if trying to reference a variable that is not set.
set -u -e

source="${BASH_SOURCE[0]}"
diff=/usr/bin/diff

pg_version=$1
check_directory=$2

# Validate entry has tests's set next collection id.
aggregateCollectionIdStr=""
maxCollectionIdStr=""

validationExceptions="/sql/documentdb_test_helpers.sql /sql/public_api_schema.sql"

echo "Validating test file output"
for validationFile in $(ls $check_directory/expected/*.out); do
    fileName=$(basename $validationFile);
    fileNameBase="${fileName%.out}";
    sqlFile="${fileName%.out}.sql";
    sqlExceptionStr="/sql/$sqlFile"

    has_invalid_results=$(grep -E "No function matches the given name and argument types." $validationFile || true)
    if [ "$has_invalid_results" != "" ]; then
        echo "test file $validationFile has invalid function specification: '$has_invalid_results'";
        exit 1
    fi

    # skip isolation test for now
    if [[ $fileName == isolation* ]]; then
        continue;
    fi;

    if [[ $validationExceptions =~ $sqlExceptionStr ]]; then
        continue;
    fi;

    # check if the base file is in the schedule
    findResult=$(grep $fileNameBase *schedule || true)
    if [ "$findResult" == "" ]; then
        if [[ "$fileNameBase" =~ "pg15" ]] || [[ "$fileNameBase" =~ "pg16" ]] || [[ "$fileNameBase" =~ "pg17" ]] || [[ "$fileNameBase" =~ "pg18" ]]; then
            echo "Skipping duplicate check for $fileNameBase"
        else
            echo "Test file '$validationFile' with name '$fileNameBase' is not in the schedule, please add it to the schedule";
            exit 1;
        fi
    fi

    # Extract the actual collection ID (we'll use this to check for uniqueness).
    collectionIdOutput=$(grep 'documentdb.next_collection_id' $validationFile || true)

    # Fail if not found.
    if [ "$collectionIdOutput" == "" ]; then
        echo "Test file prefix Validation failed on '${sqlFile}': Please ensure test files set documentdb.next_collection_id";
        exit 1;
    fi;

    # Get the actual collection ID.
    collectionIdOutput=${collectionIdOutput/SET documentdb.next_collection_id TO/};
    collectionIdOutput=${collectionIdOutput/[\s|;]/};

    isSimpleIncludeTest=false
    if [[ "$sqlFile" =~ _pg[0-9]+.sql ]]; then
        lineCount=$(cat $check_directory/sql/$sqlFile | wc -l)
        if [ $lineCount -eq 0 ]; then
            isSimpleIncludeTest="true"
        fi
    fi

    # If it matches something seen before - fail.
    if [[ "$sqlFile" =~ _pg[0-9]+.sql ]] && [[ ! "$sqlFile" =~ "_pg${pg_version}.sql" ]]; then
        echo "Skipping duplicate check for $sqlFile"
        continue;
    elif [ "$isSimpleIncludeTest" == "true" ]; then
        echo "Skipping duplicate check for $sqlFile due to it being a simple include test"
        continue;
    elif [[ "$aggregateCollectionIdStr" =~ ":$collectionIdOutput:" ]]; then
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
    collectionIndexIdOutput=$(grep 'documentdb.next_collection_index_id' $validationFile || true)
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
done

# Now validate for every sql file there's an .out file unless excluded
for sqlFile in $(ls $check_directory/sql/*.sql); do
    fileName=$(basename $sqlFile);
    fileNameBase="${fileName%.sql}";
    outFile="$check_directory/expected/$fileNameBase.out";

    includedInFiles=$(grep -l ${fileName} $check_directory/sql/*.sql || true)
    if [ ! -f "$outFile" ]; then
        if [ "${includedInFiles:-}" != "" ]; then
            echo "Skipping requirement for adding to schedule for $fileName since it's included in $includedInFiles"
            continue;
        fi

        echo "Test file '$sqlFile' does not have a corresponding expected output file '$outFile'. Please add to the schedule file and run tests.";
        exit 1;
    fi
done

echo "Validation checks done."