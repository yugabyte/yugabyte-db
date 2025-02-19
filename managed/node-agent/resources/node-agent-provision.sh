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

PYTHON_VERSION="python3.11"

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

# Install Python3.11 on the node.
install_python3_11() {
    echo "Detecting OS and installing $PYTHON_VERSION..."

    if grep -q -i "ubuntu" /etc/os-release; then
        echo "Detected Ubuntu. Installing $PYTHON_VERSION..."
        apt update && apt install -y \
            $PYTHON_VERSION \
            ${PYTHON_VERSION}-venv \
            ${PYTHON_VERSION}-dev \
            ${PYTHON_VERSION}-distutils

    elif grep -q -i "almalinux\|rocky\|rhel" /etc/os-release; then
        echo "Detected AlmaLinux/Rocky/RHEL. Installing $PYTHON_VERSION..."
        dnf install -y \
            $PYTHON_VERSION \
            ${PYTHON_VERSION}-devel

    elif grep -q -i "amazon linux 2023" /etc/os-release; then
        echo "Detected Amazon Linux 2023. Installing $PYTHON_VERSION..."
        dnf install -y \
            $PYTHON_VERSION \
            ${PYTHON_VERSION}-devel

    else
        echo "Unsupported OS. Please install $PYTHON_VERSION manually."
        exit 1
    fi

    echo "$PYTHON_VERSION installation completed."
}

# Install Pip on the node
install_pip() {
    echo "Installing pip for $PYTHON_VERSION..."
    curl -sSL https://bootstrap.pypa.io/get-pip.py | $PYTHON_VERSION
}

# Setup the correct python symlinks
setup_symlinks() {
    echo "Setting up symbolic links..."
    ln -sf /usr/bin/$PYTHON_VERSION /usr/bin/python3
    ln -sf /usr/bin/$PYTHON_VERSION /usr/bin/python
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

    # Install all .tar.gz source distributions first
    for package in "$WHEEL_DIR"/*.tar.gz; do
        if [[ -f "$package" ]]; then
            echo "Installing source distribution: $package..."
            python3 -m pip install ./"$package"
        fi
    done

    # Install all wheels in the directory
    for wheel in "$WHEEL_DIR"/*.whl; do
        if [[ -f "$wheel" ]]; then
            echo "Installing $wheel..."
            python3 -m pip install ./"$wheel"
        else
            echo "No .whl files found in $WHEEL_DIR."
        fi
    done
}

# Function to setup the virtual env.
setup_virtualenv() {
    virtualenv_dir=$(pwd)
    YB_VIRTUALENV_BASENAME="venv"
    VENV_PATH="$virtualenv_dir/$YB_VIRTUALENV_BASENAME"

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
    if [[ "$cloud_type" == "gcp" ]]; then
        echo "Importing RPM keys for almalinux"
        rpm --import https://repo.almalinux.org/almalinux/RPM-GPG-KEY-AlmaLinux
        echo "Successfully imported GPG keys"
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
        else
            filtered_args+=("${!i}")  # Keep all other arguments
        fi
    done

    if [[ "$is_csp" == true ]]; then
        import_gpg_key_if_required
        install_python3_11
        install_pip
        setup_symlinks
    fi
    check_python
    setup_virtualenv
    install_pywheels
    execute_python "${filtered_args[@]}"
}

# Call the main function and pass all arguments
main "$@"
