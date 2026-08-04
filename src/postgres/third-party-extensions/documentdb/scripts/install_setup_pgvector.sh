#!/bin/bash

# fail if trying to reference a variable that is not set.
set -u
# exit immediately if a command exits with a non-zero status
set -e

source="${BASH_SOURCE[0]}"
while [[ -h $source ]]; do
   scriptroot="$( cd -P "$( dirname "$source" )" && pwd )"
   source="$(readlink "$source")"

   # if $source was a relative symlink, we need to resolve it relative to the path where the
   # symlink file was located
   [[ $source != /* ]] && source="$scriptroot/$source"
done
scriptDir="$( cd -P "$( dirname "$source" )" && pwd )"
echo "scriptDir: $scriptDir"

. $scriptDir/utils.sh
pgBinDir=$(GetPostgresPath $PGVERSION)
PATH=$pgBinDir:$PATH;

. $scriptDir/setup_versions.sh
PG_VECTOR_REF=$(GetPgVectorVersion)

pushd $INSTALL_DEPENDENCIES_ROOT

rm -rf pg_vector-repo
mkdir pg_vector-repo
cd pg_vector-repo

git init
git remote add origin https://github.com/pgvector/pgvector.git

git fetch --depth 1 origin "$PG_VECTOR_REF"
git checkout FETCH_HEAD

echo "building and installing pg_vector extension ..."
if [ "${DESTINSTALLDIR:-""}" == "" ]; then
    sudo PATH=$PATH -E make clean
    sudo PATH=$PATH -E make -j OPTFLAGS=""
    sudo PATH=$PATH -E make install
else
    make -j
    make DESTDIR=$DESTINSTALLDIR install
fi
popd

if [ "${CLEANUP_SETUP:-"0"}" == "1" ]; then
    rm -rf $INSTALL_DEPENDENCIES_ROOT/pg_vector-repo
fi