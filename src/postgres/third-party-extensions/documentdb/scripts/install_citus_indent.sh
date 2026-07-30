#!/bin/bash

# fail if trying to reference a variable that is not set.
set -u
# exit immediately if a command exits with a non-zero status
set -e

pushd $INSTALL_DEPENDENCIES_ROOT

source="${BASH_SOURCE[0]}"
while [[ -h $source ]]; do
   scriptroot="$( cd -P "$( dirname "$source" )" && pwd )"
   source="$(readlink "$source")"

   # if $source was a relative symlink, we need to resolve it relative to the path where the
   # symlink file was located
   [[ $source != /* ]] && source="$scriptroot/$source"
done
scriptDir="$( cd -P "$( dirname "$source" )" && pwd )"

# Install uncrustify.
. $scriptDir/setup_versions.sh
UNCRUSTIFY_REF=$(GetUncrustifyVersion)
#
# Uncrustify changes the way it formats code every release a bit. To make
# sure everyone formats consistently, we use version 0.68.1 as mentioned in
# CONTRIBUTING.md:
rm -rf citus_indent
mkdir citus_indent
cd citus_indent
curl -L https://github.com/uncrustify/uncrustify/archive/${UNCRUSTIFY_REF}.tar.gz | tar xz
cd uncrustify-${UNCRUSTIFY_REF}/
mkdir build
cd build
cmake ..
make -j5
make install
cd ../..

# Install citus_indent.
git clone https://github.com/citusdata/tools.git
cd tools
# checkout to the commit mapped in component governance.
git checkout e36e4ea4258989bf527744334f6c633bb67e0686

make uncrustify/.install

if [ "${CLEANUP_SETUP:-"0"}" == "1" ]; then
    rm -rf $INSTALL_DEPENDENCIES_ROOT/citus_indent
fi

popd