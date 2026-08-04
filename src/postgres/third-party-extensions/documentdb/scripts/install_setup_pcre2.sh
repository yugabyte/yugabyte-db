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
PCRE_LIB_VERSION=$(GetPcre2Version)

PCRE_LIB_WITH_VERSION="pcre2-"$PCRE_LIB_VERSION

pushd $INSTALL_DEPENDENCIES_ROOT

# If not set only then set below variables
if [ -z ${DESTINSTALLDIR+x} ]; then
    DESTINSTALLDIR="/usr"
fi

if [ -z ${CLEANUP_SETUP+x} ]; then
    CLEANUP_SETUP="0"
fi

if [ -z ${PKG_CONFIG_INSTALL_PATH+x} ]; then

    PkgConfigInstallDirectories="$(pkg-config pkg-config --variable=pc_path)";
    # take the first path
    PKG_CONFIG_INSTALL_PATH=${PkgConfigInstallDirectories%%:*}
fi

# Create the folders and download PCRE2 lib
rm -rf $PCRE_LIB_WITH_VERSION

mkdir -p $PCRE_LIB_WITH_VERSION

cd $PCRE_LIB_WITH_VERSION

curl -L https://github.com/PCRE2Project/pcre2/releases/download/$PCRE_LIB_WITH_VERSION/$PCRE_LIB_WITH_VERSION.tar.gz -o ./$PCRE_LIB_WITH_VERSION.tar.gz
tar -xf ./$PCRE_LIB_WITH_VERSION.tar.gz --strip-components 1

# Remove the tar file
rm -rf $PCRE_LIB_WITH_VERSION.tar.gz

# Build the library
./configure --prefix=$DESTINSTALLDIR --disable-shared --enable-static --enable-jit
make clean && make -sj$(cat /proc/cpuinfo | grep -c "processor") AM_CFLAGS=-fPIC pkgconfigdir=$PKG_CONFIG_INSTALL_PATH install

popd

if [ "$CLEANUP_SETUP" == "1" ]; then
    rm -rf $INSTALL_DEPENDENCIES_ROOT/$PCRE_LIB_WITH_VERSION
fi
