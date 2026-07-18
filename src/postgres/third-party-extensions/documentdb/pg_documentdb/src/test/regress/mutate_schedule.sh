#!/bin/bash

targetFile=$1
pg_version=$2

sed -i -e "s/!MAJOR_VERSION!/${pg_version}/g" $targetFile

if (( $pg_version >= 16 )); then
    sed -i -e "s/!PG16_OR_HIGHER!/_pg16/g" $targetFile
else
    sed -i -e "s/!PG16_OR_HIGHER!//g" $targetFile
fi

if (( $pg_version >= 18 )); then
    sed -i -e "s/!PG18_OR_HIGHER!/_pg18/g" $targetFile
else
    sed -i -e "s/!PG18_OR_HIGHER!//g" $targetFile
fi

mutateFile="./test_mutate_${pg_version}"
if [ -f $mutateFile ]; then
    cat $mutateFile | while read line 
    do
        sed -i -e "$line" $targetFile
    done
else
    echo "No version specific mutation. Skipping"
fi