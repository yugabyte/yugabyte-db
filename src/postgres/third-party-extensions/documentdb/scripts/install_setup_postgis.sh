#!/bin/bash

# fail if trying to reference a variable that is not set.
set -u
# exit immediately if a command exits with a non-zero status
set -e

pushd $INSTALL_DEPENDENCIES_ROOT

# If not set only then set below variables
if [ -z ${DESTINSTALLDIR+x} ]; then
    DESTINSTALLDIR="/usr"
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
POSTGIS_REF=$(GetPostgisVersion)

. $scriptDir/utils.sh
pgBinDir=$(GetPostgresPath $PGVERSION)

POSTGIS_REPO=postgis-repo
rm -rf $POSTGIS_REPO
mkdir $POSTGIS_REPO
cd $POSTGIS_REPO

curl https://download.osgeo.org/postgis/source/postgis-$POSTGIS_REF.tar.gz -o ./postgis-$POSTGIS_REF.tar.gz
tar -xf ./postgis-$POSTGIS_REF.tar.gz --strip-components 1

# Remove the tar file
rm -rf postgis-$POSTGIS_REF.tar.gz

echo "building and installing postgis extension with pg_path $pgBinDir ..."
# Build postgis without protobuf, raster and topology support
CONFIGURE_OPTIONS="--without-protobuf --without-raster --without-topology --with-pgconfig=$pgBinDir/pg_config"

# If not set, assume it is available in the path
if [ ! -z ${GEOS_BIN_DIR+x} ]; then
    echo "GEOS config used $GEOS_BIN_DIR/geos-config..."
    CONFIGURE_OPTIONS+=" --with-geosconfig=$GEOS_BIN_DIR/geos-config"
fi

# If not set, assume it is available in the path
if [ ! -z ${PROJ_DIR+x} ]; then
    echo "PROJ dir used $PROJ_DIR..."
    CONFIGURE_OPTIONS+=" --with-projdir=$PROJ_DIR"
fi
echo "Configure options for PostGIS $CONFIGURE_OPTIONS"
./configure $CONFIGURE_OPTIONS
make PATH=$PATH -j
make PATH=$PATH install

popd

if [ "${CLEANUP_SETUP:-"0"}" == "1" ]; then
    rm -rf $INSTALL_DEPENDENCIES_ROOT/$POSTGIS_REPO
fi
