#!/bin/bash
set -e

# Script to build and install DocumentDB with all dependencies (no gateway)
# Usage: ./build_documentdb_with_scripts.sh --pg-version <version> [--citus-version <version>]

usage() {
    echo "Usage: $0 --pg-version <version> [--citus-version <version>]"
    echo "  --pg-version: PostgreSQL version (required, e.g., 15, 16, 17, 18)"
    echo "  --citus-version: Citus version (optional, defaults to 12)"
    exit 1
}

PG_VERSION=""
CITUS_VERSION=12

while [[ $# -gt 0 ]]; do
    case $1 in
        --pg-version)
            PG_VERSION="$2"
            shift 2
            ;;
        --citus-version)
            CITUS_VERSION="$2"
            shift 2
            ;;
        -h|--help)
            usage
            ;;
        *)
            echo "Unknown option: $1"
            usage
            ;;
    esac
done

if [ -z "$PG_VERSION" ]; then
    echo "Error: --pg-version is required"
    usage
fi

echo "Building DocumentDB for PostgreSQL $PG_VERSION with Citus $CITUS_VERSION"

export CITUS_VERSION
export LC_ALL=en_US.UTF-8
export LANGUAGE=en_US
export LC_COLLATE=en_US.UTF-8
export LC_CTYPE=en_US.UTF-8
export LANG=en_US.UTF-8

# Install system dependencies
echo "Installing system dependencies..."
sudo apt-get update
sudo apt-get install -y --no-install-recommends \
    wget \
    curl \
    sudo \
    gnupg2 \
    lsb-release \
    tzdata \
    build-essential \
    pkg-config \
    cmake \
    git \
    locales \
    gcc \
    gdb \
    libipc-run-perl \
    unzip \
    apt-transport-https \
    bison \
    flex \
    libreadline-dev \
    zlib1g-dev \
    libkrb5-dev \
    software-properties-common \
    libtool \
    libicu-dev \
    libssl-dev \
    openssl

# Generate locale
echo "Generating locale..."
sudo locale-gen en_US.UTF-8

export CLEAN_SETUP=1
export INSTALL_DEPENDENCIES_ROOT=/tmp/install_setup
mkdir -p /tmp/install_setup

# Copy setup scripts
echo "Setting up installation scripts..."
cp ./scripts/setup_versions.sh /tmp/install_setup

# Install libbson
echo "Installing libbson..."
cp ./scripts/install_setup_libbson.sh /tmp/install_setup
sudo INSTALL_DEPENDENCIES_ROOT=$INSTALL_DEPENDENCIES_ROOT MAKE_PROGRAM=cmake /tmp/install_setup/install_setup_libbson.sh

# Install PostgreSQL
echo "Installing PostgreSQL $PG_VERSION..."
cp ./scripts/utils.sh /tmp/install_setup
cp ./scripts/install_setup_postgres.sh /tmp/install_setup/
sudo INSTALL_DEPENDENCIES_ROOT=$INSTALL_DEPENDENCIES_ROOT /tmp/install_setup/install_setup_postgres.sh -d /usr/lib/postgresql/${PG_VERSION} -v ${PG_VERSION}

# Install RUM (skip for PG 18+)
if [ "$PG_VERSION" -lt 18 ]; then
    echo "Installing RUM for PostgreSQL $PG_VERSION..."
    cp ./scripts/install_setup_rum_oss.sh /tmp/install_setup/
    sudo INSTALL_DEPENDENCIES_ROOT=$INSTALL_DEPENDENCIES_ROOT PGVERSION=$PG_VERSION /tmp/install_setup/install_setup_rum_oss.sh
else
    echo "Skipping public RUM install for PG ${PG_VERSION} (using documentdb_extended_rum)"
fi

# Install Citus
echo "Installing Citus..."
cp ./scripts/install_setup_citus_core_oss.sh /tmp/install_setup/
sudo INSTALL_DEPENDENCIES_ROOT=$INSTALL_DEPENDENCIES_ROOT PGVERSION=$PG_VERSION /tmp/install_setup/install_setup_citus_core_oss.sh ${CITUS_VERSION}

# Install citus_indent
echo "Installing citus_indent..."
cp ./scripts/install_citus_indent.sh /tmp/install_setup/
sudo INSTALL_DEPENDENCIES_ROOT=$INSTALL_DEPENDENCIES_ROOT /tmp/install_setup/install_citus_indent.sh

# Install contrib extensions
echo "Installing contrib extensions..."
cp ./scripts/install_setup_contrib_extensions_core.sh /tmp/install_setup/
cp ./scripts/install_setup_contrib_extensions.sh /tmp/install_setup/
sudo INSTALL_DEPENDENCIES_ROOT=$INSTALL_DEPENDENCIES_ROOT PGVERSION=$PG_VERSION /tmp/install_setup/install_setup_contrib_extensions.sh

# Install pg_cron
echo "Installing pg_cron..."
cp ./scripts/install_setup_pg_cron.sh /tmp/install_setup/
sudo INSTALL_DEPENDENCIES_ROOT=$INSTALL_DEPENDENCIES_ROOT PGVERSION=$PG_VERSION /tmp/install_setup/install_setup_pg_cron.sh

# Install Intel Decimal Math Library
echo "Installing Intel Decimal Math Library..."
cp ./scripts/install_setup_intel_decimal_math_lib.sh /tmp/install_setup/
sudo INSTALL_DEPENDENCIES_ROOT=$INSTALL_DEPENDENCIES_ROOT /tmp/install_setup/install_setup_intel_decimal_math_lib.sh

# Install PCRE2
echo "Installing PCRE2..."
cp ./scripts/install_setup_pcre2.sh /tmp/install_setup/
sudo INSTALL_DEPENDENCIES_ROOT=$INSTALL_DEPENDENCIES_ROOT /tmp/install_setup/install_setup_pcre2.sh

# Install pgvector
echo "Installing pgvector..."
cp ./scripts/install_setup_pgvector.sh /tmp/install_setup/
sudo INSTALL_DEPENDENCIES_ROOT=$INSTALL_DEPENDENCIES_ROOT PGVERSION=$PG_VERSION /tmp/install_setup/install_setup_pgvector.sh

# Install PostGIS dependencies and PostGIS
echo "Installing PostGIS dependencies..."
sudo apt-get update
sudo apt-get install -qy \
    libproj-dev \
    libxml2-dev \
    libjson-c-dev \
    libgdal-dev \
    libgeos++-dev \
    libgeos-dev

echo "Installing PostGIS..."
cp ./scripts/install_setup_postgis.sh /tmp/install_setup/
sudo INSTALL_DEPENDENCIES_ROOT=$INSTALL_DEPENDENCIES_ROOT PGVERSION=$PG_VERSION /tmp/install_setup/install_setup_postgis.sh

# Export pg_config PATH
echo "Setting up PostgreSQL paths..."
export PATH="/usr/lib/postgresql/${PG_VERSION}/bin:$PATH"

# Build and install DocumentDB
echo "Building DocumentDB..."
which pg_config
make
sudo PATH=$PATH make install

echo "DocumentDB build completed successfully!"
echo "PostgreSQL binaries are in: /usr/lib/postgresql/${PG_VERSION}/bin"
