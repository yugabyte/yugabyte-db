#!/bin/bash
set -e

# Ensure required environment variables are set
test -n "$OS" || (echo "OS not set" && false)
test -n "$POSTGRES_VERSION" || (echo "POSTGRES_VERSION not set" && false)

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

# Remove 'internal' references from Makefile
sed -i '/internal/d' Makefile

# Create RPM build directories
mkdir -p ~/rpmbuild/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS}

# Get the package version from the spec file
PACKAGE_VERSION=$(grep "^Version:" rpm/documentdb.spec | awk '{print $2}' | tr -d '\r')

# Construct the package name
PACKAGE_NAME="postgresql${POSTGRES_VERSION}-documentdb"

# Add PostgreSQL bin directory to PATH to ensure pg_config is found
export PATH="/usr/pgsql-${POSTGRES_VERSION}/bin:$PATH"
echo "Using PostgreSQL bin directory: $PATH"

echo "Package name: $PACKAGE_NAME"
echo "Package version: $PACKAGE_VERSION"
echo "PostgreSQL version: $POSTGRES_VERSION"

# Copy spec file to the SPECS directory
# If building for PG >= 18, remove the rum Requires line so the produced RPM
# won't require the distro 'rum' package (we provide documentdb_extended_rum for PG18+)
if [ "${POSTGRES_VERSION}" -ge 18 ]; then
    echo "POSTGRES_VERSION=${POSTGRES_VERSION} >= 18; removing rum Requires from rpm/documentdb.spec"
    # Delete the Requires line for rum
    sed -i '/^Requires:[[:space:]]*rum_%{pg_version}/d' rpm/documentdb.spec || true
fi

cp rpm/documentdb.spec ~/rpmbuild/SPECS/

# Prepare the source directory
SOURCE_DIR="/tmp/${PACKAGE_NAME}-${PACKAGE_VERSION}"
mkdir -p "$SOURCE_DIR"

# Copy source files into the source directory
# Adjust this as needed to include all necessary files
cp -r /build/* "$SOURCE_DIR/"

# Create the source tarball
echo "Creating tarball: ~/rpmbuild/SOURCES/${PACKAGE_NAME}-${PACKAGE_VERSION}.tar.gz"
tar -czf ~/rpmbuild/SOURCES/${PACKAGE_NAME}-${PACKAGE_VERSION}.tar.gz -C /tmp "${PACKAGE_NAME}-${PACKAGE_VERSION}"

# Build the RPM package
rpmbuild -ba ~/rpmbuild/SPECS/documentdb.spec

# Rename and copy RPMs to the output directory
mkdir -p /output
if [ -n "${ARCH}" ]; then
    RPM_ARCH=${ARCH}
else
    UNAME_ARCH=$(uname -m)
    case "${UNAME_ARCH}" in
        aarch64|arm64)
            RPM_ARCH=aarch64
            ;;
        x86_64|amd64)
            RPM_ARCH=x86_64
            ;;
        *)
            echo "Unknown runtime arch: ${UNAME_ARCH}, defaulting to x86_64" >&2
            RPM_ARCH=x86_64
            ;;
    esac
fi

for rpm_file in ~/rpmbuild/RPMS/${RPM_ARCH}/*.rpm; do
    [ -e "$rpm_file" ] || continue
    base_rpm=$(basename "$rpm_file")
    mv "$rpm_file" "/output/${OS}-${base_rpm}"
done

# Also handle source RPMs
# if [ -d ~/rpmbuild/SRPMS ]; then
#     for srpm_file in ~/rpmbuild/SRPMS/*.rpm; do
#         base_srpm=$(basename "$srpm_file")
#         mv "$srpm_file" "/output/${OS}-${base_srpm}"
#     done
# fi

# Adjust ownership of the output files
chown -R $(stat -c "%u:%g" /output) /output
