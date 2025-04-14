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
. $scriptDir/utils.sh
. $scriptDir/setup_versions.sh
POSTGRESQL_REF=$(GetPostgresSourceRef $PGVERSION)

pushd $INSTALL_DEPENDENCIES_ROOT

rm -rf postgres-repo-for-system-rows
mkdir postgres-repo-for-system-rows
cd postgres-repo-for-system-rows

git init
git remote add origin https://github.com/postgres/postgres

# checkout to the commit specified in the cgmanifest.json
git fetch --depth 1 origin "$POSTGRESQL_REF"
git checkout FETCH_HEAD

pgBinDir=$(GetPostgresPath $PGVERSION)
PATH=$pgBinDir:$PATH;

echo "building and installing tsm_system_rows extension with pg path $pgBinDir ..."

cd contrib/tsm_system_rows
if [ "${DESTINSTALLDIR:-""}" == "" ]; then
    sudo PATH=$PATH -E make USE_PGXS=1 clean
    sudo PATH=$PATH -E make USE_PGXS=1 install
else
    make DESTDIR=$DESTINSTALLDIR USE_PGXS=1 install
fi
popd

rm -rf $INSTALL_DEPENDENCIES_ROOT/postgres-repo-for-system-rows