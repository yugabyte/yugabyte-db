#!/usr/bin/env bash

# Script not currently being used, but might be applied in the future to generate
# a PEX file that works on both Linux and macOS. Needs to be run on a macOS system.

set -e

. "${BASH_SOURCE%/*}"/common.sh

# Assumes that Python 3.6, 3.7, 3.8, and 3.9 are already installed on the
# macOS computer that this script is being invoked on to create the PEX wheels
# for macOS (which are then fed into build_pex.sh to create the cross-platform,
# python-version agnostic PEX).

# Install delocate to repair the generated macOS wheels (not Python version specific)
pip3 install delocate

# Remove and recreate the wheels directory if it already exists.
rm -rf wheels
mkdir wheels

# Faster creation of yb_cassandra_driver wheels without prebuilt compilation.
export CASS_DRIVER_NO_CYTHON=1

# Build each wheel for a given Python version one at a time (easier to parse/debug)
for version in ${PYTHON3_VERSIONS[@]}; do
   $version -m pip install wheel
    cat requirements.txt | while read module || [[ -n $module ]];
do
    echo "Building wheel for $module for $version"
    $version -m pip wheel -w wheels $module --no-deps
done
done

echo "Repairing wheels using delocate"
cd wheels
for whl in *; do
    if [[ $whl =~ "none" ]]; then
        echo "$whl does not need to be repaired!"
    else
        delocate-listdeps $whl
        echo "delocate-listdeps $whl executed!"
        delocate-wheel $whl
        echo "delocate-wheel $whl executed!"
    fi
done

echo "macOS X wheels are now ready to be supplied to the PEX!"
