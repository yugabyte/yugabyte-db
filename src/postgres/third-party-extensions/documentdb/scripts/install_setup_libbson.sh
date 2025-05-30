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
MONGO_DRIVER_VERSION=$(GetLibbsonVersion)

if [ "${INSTALLDESTDIR:-""}" == "" ]; then
    INSTALLDESTDIR="/usr";
fi

if [ "${MAKE_PROGRAM:-""}" == "" ]; then
    MAKE_PROGRAM="cmake3"
fi

pushd $INSTALL_DEPENDENCIES_ROOT
curl -s -L https://github.com/mongodb/mongo-c-driver/releases/download/$MONGO_DRIVER_VERSION/mongo-c-driver-$MONGO_DRIVER_VERSION.tar.gz -o ./mongo-c-driver-$MONGO_DRIVER_VERSION.tar.gz
tar -xzvf ./mongo-c-driver-$MONGO_DRIVER_VERSION.tar.gz -C $INSTALL_DEPENDENCIES_ROOT --transform="s|mongo-c-driver-$MONGO_DRIVER_VERSION|mongo-c-driver|"

# remove the tar file
rm -rf ./mongo-c-driver-$MONGO_DRIVER_VERSION.tar.gz

cd $INSTALL_DEPENDENCIES_ROOT/mongo-c-driver/build
$MAKE_PROGRAM -DENABLE_MONGOC=ON -DMONGOC_ENABLE_ICU=OFF -DENABLE_ICU=OFF -DCMAKE_C_FLAGS=-fPIC -DCMAKE_INSTALL_PREFIX=$INSTALLDESTDIR ..
make clean && make -sj$(cat /proc/cpuinfo | grep -c "processor") install
popd

if [ "${CLEANUP_SETUP:-"0"}" == "1" ]; then
    rm -rf $INSTALL_DEPENDENCIES_ROOT/mongo-c-driver
fi
