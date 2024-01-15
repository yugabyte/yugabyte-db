#!/bin/bash

# fail if trying to reference a variable that is not set.
set -u
# exit immediately if a command exits with a non-zero status
set -e

MONGO_DRIVER_VERSION="1.16.1"

if [ "${INSTALLDESTDIR:-""}" == "" ]; then
    INSTALLDESTDIR="/usr";
fi

if [ "${MAKE_PROGRAM:-""}" == "" ]; then
    MAKE_PROGRAM="cmake3"
fi

pushd $INSTALL_DEPENDENCIES_ROOT
curl -s -L https://github.com/mongodb/mongo-c-driver/releases/download/$MONGO_DRIVER_VERSION/mongo-c-driver-$MONGO_DRIVER_VERSION.tar.gz | tar -C $INSTALL_DEPENDENCIES_ROOT -zx --transform="s|mongo-c-driver-$MONGO_DRIVER_VERSION|mongo-c-driver|"
cd $INSTALL_DEPENDENCIES_ROOT/mongo-c-driver/build
$MAKE_PROGRAM -DENABLE_MONGOC=OFF -DCMAKE_C_FLAGS=-fPIC -DCMAKE_INSTALL_PREFIX=$INSTALLDESTDIR ..
make clean && make -sj$(cat /proc/cpuinfo | grep "core id" | wc -l) install
popd

if [ "${CLEANUP_SETUP:-"0"}" == "1" ]; then
    rm -rf $INSTALL_DEPENDENCIES_ROOT/mongo-c-driver
fi
