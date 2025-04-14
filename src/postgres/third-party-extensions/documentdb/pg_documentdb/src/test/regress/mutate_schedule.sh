#!/bin/bash

targetFile=$1
pg_version=$2

sed -i -e "s/!MAJOR_VERSION!/${pg_version}/g" $targetFile

mutateFile="./test_mutate_${pg_version}"
if [ -f $mutateFile ]; then
    cat $mutateFile | while read line 
    do
        sed -i -e "$line" $targetFile
    done
else
    echo "No version specific mutation. Skipping"
fi