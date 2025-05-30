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

. $scriptDir/setup_versions.sh
CITUS_PG_CRON_REF=$(GetPgCronVersion)

. $scriptDir/utils.sh
pgBinDir=$(GetPostgresPath $PGVERSION)
PATH=$pgBinDir:$PATH;

pushd $INSTALL_DEPENDENCIES_ROOT

rm -rf citus-pg_cron-repo
mkdir citus-pg_cron-repo
cd citus-pg_cron-repo

git init
git remote add origin https://github.com/citusdata/pg_cron.git

# checkout to the commit specified in the cgmanifest.json
git fetch --depth 1 origin "$CITUS_PG_CRON_REF"
git checkout FETCH_HEAD

echo "building and installing pg_cron extension with pg path $pgBinDir..."
if [ "${DESTINSTALLDIR:-""}" == "" ]; then
    sudo PATH=$PATH -E make USE_PGXS=1 clean
    sudo PATH=$PATH -E make USE_PGXS=1 install
else
    make DESTDIR=$DESTINSTALLDIR USE_PGXS=1 install
fi
popd

rm -rf $INSTALL_DEPENDENCIES_ROOT/citus-pg_cron-repo