#!/bin/bash
set -e

test -n "$OS" || (echo "OS not set" && false)

# Change to the build directory
cd /build

# Update packaging changelogs from CHANGELOG.md (fail build if this fails)
if [[ -n "${DOCUMENTDB_VERSION:-}" ]]; then
   echo "DOCUMENTDB_VERSION provided via environment: ${DOCUMENTDB_VERSION}"
else
   DOCUMENTDB_VERSION=$(grep -E "^default_version" pg_documentdb_core/documentdb_core.control | sed -E "s/.*'([0-9]+\.[0-9]+-[0-9]+)'.*/\1/" || true)
fi
if [[ -n "$DOCUMENTDB_VERSION" ]]; then
   echo "Running changelog update for version: $DOCUMENTDB_VERSION"
   /bin/bash /build/packaging/update_spec_changelog.sh "$DOCUMENTDB_VERSION"
else
   echo "WARNING: Could not determine documentdb version; skipping changelog update"
   exit 1
fi

# Keep the internal directory out of the Debian package
sed -i '/internal/d' Makefile

# Build the Debian package
debuild -us -uc

# Change to the root to make file renaming expression simpler
cd /

# Rename .deb files to include the OS name prefix
for f in *.deb; do
   mv $f $OS-$f;
done

# Create the output directory if it doesn't exist
mkdir -p /output

# Copy the built packages to the output directory
cp *.deb /output/

# Change ownership of the output files to match the host user's UID and GID
chown -R $(stat -c "%u:%g" /output) /output