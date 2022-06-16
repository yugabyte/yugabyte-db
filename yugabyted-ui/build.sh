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

mkdir -p bin
mv $UIDIR/build.tgz $BUILDDIR

cd $BUILDDIR/
mkdir -p ui
tar -xf build.tgz -C ui --strip-components 1
rm -rf build.tgz build
cd ..

cd $APISERVERDIR
go build -o yugabyted-ui
cd ../../..

mv $APISERVERDIR/yugabyted-ui $BUILDDIR/

if [[ -f "$BUILDDIR/yugabyted-ui" && -d "$BUILDDIR/ui" ]] 
then
    echo "Yugabyted UI Binary generated successfully in the bin/ directory."
else
    echo "Build Failed."
fi