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

if [ -n $CONTRIB_EXTENSIONS ]; then
    echo "Contrib extensions must be defined"
fi

pg_source_dir=$INSTALL_DEPENDENCIES_ROOT/postgres-repo-for-contrib-ext
rm -rf $pg_source_dir
mkdir -p $pg_source_dir
cd $pg_source_dir

git init
git remote add origin https://github.com/postgres/postgres

# checkout to the commit specified in the setup_versions.sh
git fetch --depth 1 origin "$POSTGRESQL_REF"
git checkout FETCH_HEAD

pgBinDir=$(GetPostgresPath $PGVERSION)
PATH=$pgBinDir:$PATH;

for ext in "${CONTRIB_EXTENSIONS[@]}"; do
    echo "building and installing $ext extension with pg path $pgBinDir ..."
    cd $pg_source_dir/contrib/$ext
    if [ "${DESTINSTALLDIR:-""}" == "" ]; then
        sudo PATH=$PATH -E make USE_PGXS=1 clean
        sudo PATH=$PATH -E make USE_PGXS=1 install
    else
        make DESTDIR=$DESTINSTALLDIR USE_PGXS=1 install
    fi
done
popd

rm -rf $INSTALL_DEPENDENCIES_ROOT/postgres-repo-for-contrib-ext