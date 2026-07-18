#!/bin/bash

set -euo pipefail

# set script_dir to the parent directory of the script
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# Function to display help message
function show_help {
    echo "Usage: $0 --os <OS> --pg <PG_VERSION> [--test-clean-install] [--output-dir <DIR>] [-h|--help]"
    echo ""
    echo "Description:"
    echo "  This script builds extension packages (DEB/RPM) using Docker."
    echo ""
    echo "Mandatory Arguments:"
    echo "  --os                 OS to build packages for. Possible values: [deb11, deb12, deb13, ubuntu22.04, ubuntu24.04, rhel8, rhel9]"
    echo "  --pg                 PG version to build packages for. Possible values: [15, 16, 17]"
    echo ""
    echo "Optional Arguments:"
    echo "  --version            The version of documentdb to build. Examples: [0.100.0, 0.101.0]"
    echo "  --test-clean-install Test installing the packages in a clean Docker container."
    echo "  --output-dir         Relative path from the repo root of the directory where to drop the packages. The directory will be created if it doesn't exist. Default: packaging"
    echo "  -h, --help           Display this help message."
    exit 0
}

# Initialize variables
OS=""
PG=""
DOCUMENTDB_VERSION=""
TEST_CLEAN_INSTALL=false
OUTPUT_DIR="packaging"  # Default value for output directory (relative to script_dir)
PACKAGE_TYPE=""  # Will be set to "deb" or "rpm"

# Process arguments to convert long options to short ones
while [[ $# -gt 0 ]]; do
    case "$1" in
        --os)
            shift
            case $1 in
                deb11|deb12|deb13|ubuntu22.04|ubuntu24.04)
                    OS=$1
                    PACKAGE_TYPE="deb"
                    ;;
                rhel8|rhel9)
                    OS=$1
                    PACKAGE_TYPE="rpm"
                    ;;
                *)
                    echo "Invalid --os value. Allowed values are [deb11, deb12, deb13, ubuntu22.04, ubuntu24.04, rhel8, rhel9]"
                    exit 1
                    ;;
            esac
            ;;
        --pg)
            shift
            case $1 in
                15|16|17|18)
                    PG=$1
                    ;;
                *)
                    echo "Invalid --pg value. Allowed values are [15, 16, 17, 18]"
                    exit 1
                    ;;
            esac
            ;;
        --version)
            shift
            DOCUMENTDB_VERSION=$1
            ;;
        --test-clean-install)
            TEST_CLEAN_INSTALL=true
            ;;
        --output-dir)
            shift
            OUTPUT_DIR=$1
            ;;
        -h|--help)
            show_help
            ;;
        *)
            echo "Unknown argument: $1"
            show_help
            exit 1
            ;;
    esac
    shift
done

# Check mandatory arguments
if [[ -z "$OS" ]]; then
    echo "Error: --os is required."
    show_help
    exit 1
fi

# Set the appropriate Docker image and configuration based on the OS
DOCKERFILE=""
OS_VERSION_NUMBER=""

if [[ "$PACKAGE_TYPE" == "deb" ]]; then
    case $OS in
        deb11)
            DOCKER_IMAGE="rust:slim-bullseye"
            TEST_DOCKER_IMAGE="debian:bullseye-slim"
            DOCKERFILE="${script_dir}/packaging/deb/Dockerfile_gateway_deb"
            ;;
        deb12)
            DOCKER_IMAGE="rust:slim-bookworm"
            TEST_DOCKER_IMAGE="debian:bookworm-slim"
            DOCKERFILE="${script_dir}/packaging/deb/Dockerfile_gateway_deb"
            ;;
        deb13)
            DOCKER_IMAGE="rust:slim-trixie"
            TEST_DOCKER_IMAGE="debian:trixie-slim"
            DOCKERFILE="${script_dir}/packaging/deb/Dockerfile_gateway_deb"
            ;;
        # Ubuntu images need to install rust manually
        ubuntu22.04)
            DOCKER_IMAGE="ubuntu:22.04"
            TEST_DOCKER_IMAGE="ubuntu:22.04"
            DOCKERFILE="${script_dir}/packaging/deb/Dockerfile_gateway_ubuntu"
            ;;
        ubuntu24.04)
            DOCKER_IMAGE="ubuntu:24.04"
            TEST_DOCKER_IMAGE="ubuntu:24.04"
            DOCKERFILE="${script_dir}/packaging/deb/Dockerfile_gateway_ubuntu"
            ;;
    esac
elif [[ "$PACKAGE_TYPE" == "rpm" ]]; then
    # TODO: Implement RPM package building
    echo "Building RPM packages is not yet implemented."
fi

TAG=documentdb-build-packages-$OS-pg$PG:latest

abs_output_dir="$script_dir/$OUTPUT_DIR"

echo "Building $PACKAGE_TYPE packages for OS: $OS, PostgreSQL version: $PG, DOCUMENTDB version: $DOCUMENTDB_VERSION"
echo "Output directory: $abs_output_dir"

# Create the output directory if it doesn't exist
mkdir -p "$abs_output_dir"
chmod 777 "$abs_output_dir"

# Build the Docker image while showing the output to the console
if [[ "$PACKAGE_TYPE" == "deb" ]]; then
    docker build -t "$TAG" -f "$DOCKERFILE" \
        --build-arg BASE_IMAGE="$DOCKER_IMAGE" \
        --build-arg DOCUMENTDB_VERSION="$DOCUMENTDB_VERSION" "$script_dir"
    # Run the Docker container to build the packages
    docker run --rm --env OS="$OS" --env DOCUMENTDB_VERSION="$DOCUMENTDB_VERSION" -v "$abs_output_dir:/output" "$TAG"
elif [[ "$PACKAGE_TYPE" == "rpm" ]]; then
    echo "Building RPM packages is not yet implemented."
    # TODO: Implement RPM package building
fi

echo "Packages built successfully!!"

if [[ $TEST_CLEAN_INSTALL == true ]]; then
    echo "Testing clean installation in a Docker container..."

    if [[ "$PACKAGE_TYPE" == "deb" ]]; then
        ls "$abs_output_dir"
        deb_package_name=$(ls "$abs_output_dir" | grep -E "${OS}-postgresql-$PG-documentdb_${DOCUMENTDB_VERSION}.*\.deb" | grep -v "dbg" | head -n 1)
        deb_package_rel_path="$OUTPUT_DIR/$deb_package_name"
        gateway_package_name=$(ls "$abs_output_dir" | grep -E "^documentdb_gateway_.*\.deb" | grep -v "dbg" | head -n 1)
        gateway_package_rel_path="$OUTPUT_DIR/$gateway_package_name"

        echo "Debian package path passed into Docker build: $deb_package_rel_path"

        # Build the Docker image while showing the output to the console
        docker build -t documentdb-test-gateway-packages:latest -f "${script_dir}/packaging/test_packages/deb/Dockerfile_deb_gateway_test" \
            --build-arg BASE_IMAGE="$TEST_DOCKER_IMAGE" \
            --build-arg DEB_PACKAGE_REL_PATH="$deb_package_rel_path" \
            --build-arg GATEWAY_PACKAGE_PATH="$gateway_package_rel_path" "$script_dir"
        # Run the Docker container to test the packages
        docker run --rm documentdb-test-gateway-packages:latest

    elif [[ "$PACKAGE_TYPE" == "rpm" ]]; then
        echo "RPM package installation test is not yet implemented."
    fi

    echo "Clean installation test successful!!"
fi

echo "Packages are available in $abs_output_dir"