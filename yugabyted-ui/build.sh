#!/bin/bash

readonly APISERVERDIR=apiserver/cmd/server
readonly UIDIR=ui
readonly BUILDDIR=bin

if ! command -v npm -version &> /dev/null
then
    echo "npm could not be found"
    exit
fi

if ! command -v go version &> /dev/null
then
    echo "go lang could not be found"
    exit
fi

rm -rf bin

cd $UIDIR
npm ci
npm run build
tar cfz build.tgz build/*
cd ..

mv $UIDIR/build.tgz $APISERVERDIR
cd $APISERVERDIR
mkdir -p ui
tar -xf build.tgz -C ui --strip-components 1
rm -rf build.tgz build
go build -o yugabyted-ui
cd ../../..

mkdir -p $BUILDDIR
mv $APISERVERDIR/yugabyted-ui $BUILDDIR/

if [ -f "$BUILDDIR/yugabyted-ui" ]
then
    echo "Yugabyted UI Binary generated successfully in the bin/ directory."
else
    echo "Build Failed."
fi
