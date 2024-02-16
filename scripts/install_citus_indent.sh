#!/bin/bash

# fail if trying to reference a variable that is not set.
set -u
# exit immediately if a command exits with a non-zero status
set -e

# Install uncrustify.
#
# Uncrustify changes the way it formats code every release a bit. To make
# sure everyone formats consistently, we use version 0.68.1 as mentioned in
# CONTRIBUTING.md:
pushd $INSTALL_DEPENDENCIES_ROOT
rm -rf citus_indent
mkdir citus_indent
cd citus_indent
git clone https://github.com/uncrustify/uncrustify.git
cd uncrustify
# the commit of version 0.68.1
git checkout 8c80bd84d023d9ae220c2721e68b09d10403bb41
mkdir build
cd build/
cmake ..
make clean && make -sj$(cat /proc/cpuinfo | grep "processor" | wc -l) install

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