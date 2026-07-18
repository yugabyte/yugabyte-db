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

. $scriptDir/setup_versions.sh
RUM_REF=$(GetRumVersion)

# If PGVERSION is set and is >= 18, we don't need to install the public RUM
# since the repository provides documentdb_extended_rum for pg18 and later.
if [ "${PGVERSION:-0}" -ge 18 ]; then
    echo "PGVERSION=${PGVERSION} >= 18; skipping public RUM installation (using documentdb_extended_rum)"
    exit 0
fi

. $scriptDir/utils.sh
if [ "${PGVERSION:-}" != "" ]; then
    pgPath=$(GetPostgresPath $PGVERSION)
    PATH=$pgPath:$PATH
fi

pushd $INSTALL_DEPENDENCIES_ROOT

rm -rf rum-repo
mkdir rum-repo
cd rum-repo

git init
git remote add origin https://github.com/postgrespro/rum.git

git fetch --depth 1 origin "$RUM_REF"
git checkout FETCH_HEAD

echo "building and installing rum extension ..."
echo $PATH
if [ "${DESTINSTALLDIR:-""}" == "" ]; then
    sudo PATH=$PATH -E make USE_PGXS=1
    sudo PATH=$PATH -E make install USE_PGXS=1
else
    make USE_PGXS=1
    make USE_PGXS=1 DESTDIR=$DESTINSTALLDIR install
fi
popd

if [ "${CLEANUP_SETUP:-"0"}" == "1" ]; then
    rm -rf $INSTALL_DEPENDENCIES_ROOT/rum-repo
fi