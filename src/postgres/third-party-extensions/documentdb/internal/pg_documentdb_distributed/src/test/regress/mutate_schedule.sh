#!/bin/bash

targetFile=$1
pg_version=$2

source="${BASH_SOURCE[0]}"

while [[ -h $source ]]; do
   scriptroot="$( cd -P "$( dirname "$source" )" && pwd )"
   source="$(readlink "$source")"

   # if $source was a relative symlink, we need to resolve it relative to the path where the
   # symlink file was located
   [[ $source != /* ]] && source="$scriptroot/$source"
done

scriptDir="$( cd -P "$( dirname "$source" )" && pwd )"

sed -i -e "s/!MAJOR_VERSION!/${pg_version}/g" $targetFile

if (( $pg_version >= 16 )); then
    sed -i -e "s/!PG16_OR_HIGHER!/_pg16/g" $targetFile
else
    sed -i -e "s/!PG16_OR_HIGHER!//g" $targetFile
fi

if (( $pg_version >= 17 )); then
    sed -i -e "s/!PG17_OR_HIGHER!/_pg17/g" $targetFile
else
    sed -i -e "s/!PG17_OR_HIGHER!//g" $targetFile
fi


function ProcessMutateFile()
{
    local mutateFile=$1
    if [ -f $mutateFile ]; then
        cat $mutateFile | while read line 
        do
            sed -i -e "$line" $targetFile
        done
    else
        echo "No version specific mutation at $mutateFile. Skipping"
    fi
}

ProcessMutateFile "./test_mutate_${pg_version}"
ProcessMutateFile "$scriptDir/test_mutate_${pg_version}"

if [ "${DOCDB_ENABLE_ASAN:-}" == "true" ]; then
    ProcessMutateFile "./test_mutate_asan"
    ProcessMutateFile "$scriptDir/test_mutate_asan"
fi

if [ "${DOCDB_ENABLE_VALGRIND:-}" == "true" ]; then
    ProcessMutateFile "./test_mutate_valgrind"
    ProcessMutateFile "$scriptDir/test_mutate_valgrind"
fi
