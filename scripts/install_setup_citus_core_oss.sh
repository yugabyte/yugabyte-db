#!/bin/bash

# fail if trying to reference a variable that is not set.
set -u
# exit immediately if a command exits with a non-zero status
set -e

citusVersion=$1

source="${BASH_SOURCE[0]}"
while [[ -h $source ]]; do
   scriptroot="$( cd -P "$( dirname "$source" )" && pwd )"
   source="$(readlink "$source")"

   # if $source was a relative symlink, we need to resolve it relative to the path where the
   # symlink file was located
   [[ $source != /* ]] && source="$scriptroot/$source"
done
scriptDir="$( cd -P "$( dirname "$source" )" && pwd )"

. $scriptDir/setup_versions.sh
CITUS_REF=$(GetCitusVersion $citusVersion)

. $scriptDir/utils.sh
if [ "${PGVERSION:-}" != "" ]; then
    pgPath=$(GetPostgresPath $PGVERSION)
    PATH=$pgPath:$PATH
fi

pushd $INSTALL_DEPENDENCIES_ROOT

rm -rf citus-repo
mkdir citus-repo
cd citus-repo

git init
git remote add origin https://github.com/citusdata/citus.git

git fetch --depth 1 origin "$CITUS_REF"
git checkout FETCH_HEAD

echo "building and installing citus extension ..."
./configure --without-lz4 --without-zstd --without-libcurl
make PATH=$PATH clean

if declare -F ProcessCitusMakefileGlobal > /dev/null; then
    echo "Function ProcessCitusMakefileGlobal is defined"
    ProcessCitusMakefileGlobal
else
    echo "Function ProcessCitusMakefileGlobal is not defined"
fi


if [ "${DESTINSTALLDIR:-}" == "" ]; then
make PATH=$PATH -j$(cat /proc/cpuinfo | grep -c "processor") install
else
make PATH=$PATH DESTDIR=$DESTINSTALLDIR -j$(cat /proc/cpuinfo | grep -c "processor") install
fi
popd

if [ "${CLEANUP_SETUP:-"0"}" == "1" ]; then
    rm -rf $INSTALL_DEPENDENCIES_ROOT/citus-repo
fi
