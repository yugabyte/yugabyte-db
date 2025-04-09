#!/bin/bash

# exit immediately if a command exits with a non-zero status
set -e
# fail if trying to reference a variable that is not set.
set -u

postgresqlInstallDir=""
debug="false"
cassert="false"
help="false";
pgVersion=""
while getopts "d:hxcv:" opt; do
  case $opt in
    d) postgresqlInstallDir="$OPTARG"
    ;;
    x) debug="true"
    ;;
    c) cassert="true"
    ;;
    h) help="true"
    ;;
    v) pgVersion="$OPTARG"
    ;;
  esac

  # Assume empty string if it's unset since we cannot reference to
  # an unset variabled due to "set -u".
  case ${OPTARG:-""} in
    -*) echo "Option $opt needs a valid argument"
    exit 1
    ;;
  esac
done

if [ "$help" == "true" ]; then
    echo "downloads postgresql-14.2 sources, build and install it."
    echo "[-d] the directory to install postgresql to. Default: /usr/lib/postgresql/14"
    echo "[-x] build with debug symbols."
    exit 1;
fi

if [ -z $postgresqlInstallDir ]; then
    echo "Postgres Install Directory must be specified."
    exit 1;
fi

if [ -z $pgVersion ]; then
  echo "PG Version must be specified";
  exit 1;
fi

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
POSTGRESQL_REF=$(GetPostgresSourceRef $pgVersion)

pushd $INSTALL_DEPENDENCIES_ROOT

rm -rf postgres-repo/$pgVersion
mkdir -p postgres-repo/$pgVersion
cd postgres-repo/$pgVersion

git init
git remote add origin https://github.com/postgres/postgres

# checkout to the commit specified in the cgmanifest.json
git fetch --depth 1 origin "$POSTGRESQL_REF"
git checkout FETCH_HEAD

echo "building and installing postgresql ref $POSTGRESQL_REF and installing to $postgresqlInstallDir..."

if [ "$debug" == "true" ]; then
  ./configure --enable-debug --enable-cassert --enable-tap-tests CFLAGS="-ggdb -Og -g3 -fno-omit-frame-pointer" --with-openssl --prefix="$postgresqlInstallDir" --with-icu
elif [ "$cassert" == "true" ]; then
  ./configure --enable-debug --enable-cassert --enable-tap-tests --with-openssl --prefix="$postgresqlInstallDir" --with-icu
else
  ./configure --enable-debug --enable-tap-tests --with-openssl --prefix="$postgresqlInstallDir" --with-icu
fi

make clean && make -sj$(cat /proc/cpuinfo | grep -c "processor") install

popd

if [ "${CLEANUP_SETUP:-"0"}" == "1" ]; then
    rm -rf $INSTALL_DEPENDENCIES_ROOT/postgres-repo
fi
