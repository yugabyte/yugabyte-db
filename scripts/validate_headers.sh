#!/bin/bash

# fail if trying to reference a variable that is not set.
set -e


source="${BASH_SOURCE[0]}"
while [[ -h $source ]]; do
   scriptroot="$( cd -P "$( dirname "$source" )" && pwd )"
   source="$(readlink "$source")"

   # if $source was a relative symlink, we need to resolve it relative to the path where the
   # symlink file was located
   [[ $source != /* ]] && source="$scriptroot/$source"
done

repoScriptDir="$( cd -P "$( dirname "$source" )" && pwd )"

declare -A headerMap;

# Ensure unique headers between helio_core and helio_api
for f in $repoScriptDir/../pg_helio_core/include/**/*; do
    subPath=${f#$repoScriptDir/../pg_helio_core/include/}

    if [ "${headerMap[$subPath]}" != "" ]; then
        echo "Duplicate header path found at $subPath. Header files must be unique across the extensions";
        exit 1;
    fi
    headerMap[$subPath]=1;

done;

for f in $repoScriptDir/../pg_helio_api/include/**/*; do
    subPath=${f#$repoScriptDir/../pg_helio_api/include/}
    if [ "${headerMap[$subPath]}" != "" ]; then
        echo "Duplicate header path found at $subPath. Header files must be unique across the extensions";
        exit 1;
    fi
    headerMap[$subPath]=1;
done;

# Now check they don't duplicate with postgres's headers.
pg_include_dir=$(pg_config --includedir)
pg_include_dir_server=$(pg_config --includedir-server)
for f in ${!example_array[@]}
do
  if [ -f "$pg_include_dir/$f"]; then
    echo "Duplicate header path found at $f. Header files must not collide with postgres headers";
    exit 1;
  fi

  if [ -f "$pg_include_dir_server/$f"]; then
    echo "Duplicate header path found at $f. Header files must not collide with postgres server headers";
    exit 1;
  fi
done

hasInvalidHeaders="0"

for f in $(grep -R "include <server" --include \*.h --include \*.c oss)
do
  echo "Invalid header file found $f do not reference header files via 'server'"
  hasInvalidHeaders="1"
done

for f in $(grep -R "include \"server" --include \*.h --include \*.c oss)
do
  echo "Invalid header file found $f do not reference header files via 'server'"
  hasInvalidHeaders="1"
done

if [ "$hasInvalidHeaders" == "1" ]; then
   exit 1;
fi


echo "Total headers checked ${#headerMap[@]}"

