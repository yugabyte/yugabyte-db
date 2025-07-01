#!/bin/bash
#
# Copyright 2024 YugaByte, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
set -euo pipefail

PYTHON_VERSION="python3"

# Function to display usage information
show_usage() {
    echo "Usage: $0 [-c|--command COMMAND]"
    echo "  -c, --command COMMAND    Specify the command to run"
}

# Function to handle errors
err_msg() {
    echo "Error: $1" >&2
    exit 1
}

is_csp=false
cloud_type=""
is_airgap=false

# Install Python3 on the node.
install_python3() {
    echo "Detecting OS and installing python3..."

    if grep -q -i "ubuntu\|debian" /etc/os-release; then
        echo "Detected Ubuntu. Installing python3..."
        apt update && apt install -y \
            python3 \
            python3-venv \
            python3-dev \
            python3-distutils

    elif grep -q -i "almalinux\|rocky\|rhel" /etc/os-release; then
        echo "Detected AlmaLinux/Rocky/RHEL. Installing python3..."
        dnf install -y \
            python3 \
            python3-devel

    elif grep -q -i "amazon linux 2023" /etc/os-release; then
        echo "Detected Amazon Linux 2023. Installing python3..."
        dnf install -y \
            python3 \
            python3-devel

    elif grep -q -i "sles\|suse" /etc/os-release; then
        echo "Detected SLES/SUSE. Installing python3..."
        zypper install -y \
            python3 \
            python3-devel \
            python3-pip

    else
        echo "Unsupported OS. Please install python3 manually."
        exit 1
    fi

    echo "python3 installation completed."
}

# Setup the correct python symlinks
setup_symlinks() {
    echo "Setting up symbolic links..."
    ln -sf /usr/bin/python3 /usr/bin/python
    echo "Python symlinks updated."
}

# Function to install the python wheels
install_pywheels() {
    WHEEL_DIR="pywheels"
    echo "Installing Python wheels from directory: $WHEEL_DIR"

    # Check if the wheel directory exists
    if [[ ! -d "$WHEEL_DIR" ]]; then
        err_msg "Wheel directory $WHEEL_DIR does not exist."
    fi

   # Extract package names (without version) from filenames
    declare -A PACKAGE_FILES
    for package in "$WHEEL_DIR"/*.tar.gz; do
        if [[ -f "$package" ]]; then
            # Extract package name
            base_name=$(basename "$package" | sed -E 's/-[0-9].*//')
            # Convert to lowercase
            base_name_lower=$(echo "$base_name" | tr '[:upper:]' '[:lower:]')
            PACKAGE_FILES["$base_name_lower"]="$package"
        fi
    done
    # Define the dependency order (without versions)
    declare -a DEP_ORDER=(
        "setuptools"          # Python devtools
        "wheel"               # Python devtools
        "markupsafe"          # Required by Jinja2
        "jinja2"              # Needs MarkupSafe
        "charset-normalizer"  # Used by Requests
        "idna"                # Used by Requests
        "urllib3"             # Used by Requests
        "certifi"             # Used by Requests
        "requests"            # Needs urllib3, certifi, idna, charset-normalizer
        "pyyaml"              # Independent
    )

    # Step 1: Install dependencies in order
    for package in "${DEP_ORDER[@]}"; do
        if [[ -n "${PACKAGE_FILES[$package]:-}" ]]; then
            echo "Installing $package from ${PACKAGE_FILES[$package]}..."
            python3 -m pip install \
                    --no-index --no-build-isolation \
                    --find-links="$WHEEL_DIR" "${PACKAGE_FILES[$package]}" || {
                echo "Retrying without --no-build-isolation for $package..."
                python3 -m pip install --no-index \
                        --find-links="$WHEEL_DIR" "${PACKAGE_FILES[$package]}" || {
                    echo "Error installing $package" >&2
                    exit 1
                }
            }
        else
            echo "Warning: Package $package not found in PACKAGE_FILES."
        fi
    done
}

# Function to setup the virtual env.
setup_virtualenv() {
    virtualenv_dir=$(pwd)
    YB_VIRTUALENV_BASENAME="venv"
    VENV_PATH="$virtualenv_dir/$YB_VIRTUALENV_BASENAME"

    if [[ "$is_csp" == true && "$is_airgap" == false ]]; then
        # Check if venv is available
        if ! python3 -m ensurepip &>/dev/null; then
            echo "venv module is missing. Installing it..."

            if grep -q -i "ubuntu\|debian" /etc/os-release; then
                apt update && apt install -y python3-venv
            elif grep -q -i "almalinux\|rocky\|rhel\|amazon linux 2023" /etc/os-release; then
                dnf install -y python3-venv
            elif grep -q -i "sles\|suse" /etc/os-release; then
                zypper install -y python3-venv
            fi
        fi
    fi

    if [ ! -d "$VENV_PATH" ]; then
        echo "Creating virtual environment at $VENV_PATH..."
        if ! python3 -m venv "$VENV_PATH"; then
            echo "Failed to create virtual environment. Continuing without it..."
        fi
    else
        echo "Virtual environment already exists at $VENV_PATH."
    fi

    if source "$VENV_PATH/bin/activate"; then
        echo "Virtual environment activated."
    else
        echo "Failed to activate virtual environment. Continuing without it..."
    fi
}

# Funvtion to setup pip in venv.
setup_pip() {
    # Get the installed Python3 version dynamically
    PYTHON_CMD=$(command -v python3)
    PYTHON_VERSION_DETECTED=$(
        $PYTHON_CMD -c "import sys; print('python' + '.'.join(map(str, sys.version_info[:2])))"
    )

    echo "Detected Python version: $PYTHON_VERSION_DETECTED"

    # Install pip for detected Python version
    if ! $PYTHON_CMD -m pip --version &>/dev/null; then
        echo "Pip is not installed for $PYTHON_VERSION_DETECTED. Installing pip..."
        curl -sSL https://bootstrap.pypa.io/get-pip.py | $PYTHON_CMD
    else
        echo "Upgrading pip for $PYTHON_VERSION_DETECTED..."
    fi
}

# Function to check for Python
check_python() {
    echo "Checking for Python and pip..."
    if ! command -v python3 &> /dev/null; then
        err_msg "python3 is not installed or not found in PATH."
    fi

    if ! python3 -m pip --version &> /dev/null; then
        err_msg "pip is not installed. Please install pip before proceeding."
    fi

    echo "Python and pip are available."
}

# Function to execute the Python script
execute_python() {
    python3 ynp/main.py "$@"
}

# Function for importing the GPG key if required.
import_gpg_key_if_required() {
    # Check if the OS is Red Hat-based (RHEL, Rocky, Alma)
    if grep -qiE "rhel|rocky|almalinux" /etc/os-release && \
        grep -qIE 'VERSION_ID="8([.][0-9]+)?"' /etc/os-release; then
        echo "Importing RPM keys for Red Hat-based OS"
        rpm --import https://repo.almalinux.org/almalinux/RPM-GPG-KEY-AlmaLinux
        echo "Successfully imported GPG keys"
    else
        echo "Skipping GPG key import as the OS is not Red Hat-based."
    fi
}

# Main function
main() {
    filtered_args=()

    for ((i=1; i<=$#; i++)); do
        if [[ "${!i}" == "--cloud_type" ]]; then
            # Skip --cloud_type and its value
            next_index=$((i + 1))
            if [[ $next_index -le $# && ! "${!next_index}" =~ ^-- ]]; then
                cloud_type="${!next_index}"
                i=$next_index  # Skip next argument as well
            fi
        elif [[ "${!i}" == "--extra_vars" ]]; then
            echo "Extra vars detected. CSP use case..."
            is_csp=true
            filtered_args+=("${!i}")  # Keep --extra_vars
            next_index=$((i + 1))
            if [[ $next_index -le $# && ! "${!next_index}" =~ ^-- ]]; then
                filtered_args+=("${!next_index}")  # Keep its value
                i=$next_index  # Skip next argument
            fi
        elif [[ "${!i}" == "--is_airgap" ]]; then
            is_airgap=true  # Set the flag
        else
            filtered_args+=("${!i}")  # Keep all other arguments
        fi
    done

    if [[ "$is_csp" == true && "$is_airgap" == false ]]; then
        import_gpg_key_if_required
        # Check if python3 is installed; if not, install Python 3.11
        if ! command -v python3 &>/dev/null; then
            echo "Python3 is not installed. Installing Python 3.11..."
            install_python3
        fi
        setup_symlinks
    fi
    setup_virtualenv
    if [[ "$is_csp" == true && "$is_airgap" == false ]]; then
        setup_pip
    fi
    check_python
    install_pywheels
    execute_python "${filtered_args[@]}"
}

# Call the main function and pass all arguments
main "$@"
