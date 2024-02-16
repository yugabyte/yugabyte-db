#!/bin/bash

# fail if trying to reference a variable that is not set.
set -u / set -o nounset

# exit immediately if a command exits with a non-zero status
set -e

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

if [ -z ${PKG_CONFIG_INSTALL_PATH+x} ]; then
    echo "PKG_CONFIG_INSTALL_PATH must be specified"
    exit 1;
fi

MATH_LIB_VERSION="20U2"
MATH_LIB_WITH_VERSION="IntelRDFPMathLib"$MATH_LIB_VERSION

# Create the folders and download Intel RDF MathLib
rm -rf $MATH_LIB_WITH_VERSION
mkdir -p $MATH_LIB_WITH_VERSION/lib/intelmathlib
mkdir -p $PKG_CONFIG_INSTALL_PATH

# Pipeline is not copying the files properly, so creating the structure upfront
mkdir -p $DESTINSTALLDIR/lib/intelmathlib

echo "Moving to $MATH_LIB_WITH_VERSION/lib/intelmathlib"
cd $MATH_LIB_WITH_VERSION/lib/intelmathlib

# see https://netlib.org/misc/intel/ pointed to from https://www.intel.com/content/www/us/en/developer/articles/tool/intel-decimal-floating-point-math-library.html
curl https://netlib.org/misc/intel/$MATH_LIB_WITH_VERSION.tar.gz -o ./$MATH_LIB_WITH_VERSION.tar.gz

tar -xf ./$MATH_LIB_WITH_VERSION.tar.gz --strip-components 1

# Remove the tar file
rm -rf $MATH_LIB_WITH_VERSION.tar.gz

# Build the library with -fPIC so that it is linked properly and also other variables are defined to configure the library
cd LIBRARY
make -sj$(cat /proc/cpuinfo | grep "processor" | wc -l) _CFLAGS_OPT=-fPIC CC=gcc CALL_BY_REF=0 GLOBAL_RND=0 GLOBAL_FLAGS=0 UNCHANGED_BINARY_FLAGS=0

# Create a package config file to easily locate the lib


cd $INSTALL_DEPENDENCIES_ROOT/$MATH_LIB_WITH_VERSION
touch intelmathlib.pc
echo "prefix=$DESTINSTALLDIR/lib/intelmathlib" >> intelmathlib.pc
echo 'libdir=${prefix}/LIBRARY' >> intelmathlib.pc
echo 'includedir=${prefix}/LIBRARY/src' >> intelmathlib.pc
echo '' >> intelmathlib.pc
echo 'Name: intelmathlib' >> intelmathlib.pc
echo 'Description: Intel Decimal Floating point math library' >> intelmathlib.pc
echo 'Version: 2.0 Update 2' >> intelmathlib.pc
echo 'Cflags: -I${includedir}' >> intelmathlib.pc
echo 'Libs: -L${libdir} -lbid' >> intelmathlib.pc

# Copy Library to Destination
cd $INSTALL_DEPENDENCIES_ROOT
cp -R -v $MATH_LIB_WITH_VERSION/* $DESTINSTALLDIR/
cp -v $MATH_LIB_WITH_VERSION/intelmathlib.pc $PKG_CONFIG_INSTALL_PATH


popd

if [ "$CLEANUP_SETUP" == "1" ]; then
    rm -rf $INSTALL_DEPENDENCIES_ROOT/$MATH_LIB_WITH_VERSION
fi
