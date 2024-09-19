#!/bin/bash
set -e

# Change to the build directory
cd /build

# Keep the internal directory out of the Debian package
sed -i '/internal/d' Makefile

# Build the Debian package
debuild -us -uc

# Create the output directory if it doesn't exist
mkdir -p /output

# Copy the built packages to the output directory
cp ../*.deb /output/

# Change ownership of the output files to match the host user's UID and GID
chown -R $(stat -c "%u:%g" /output) /output
