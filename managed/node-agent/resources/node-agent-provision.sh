#!/bin/bash
#
# Copyright 2024 YugabyteDB, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
set -euo pipefail

PYTHON_VERSION="python3"
VENV_SETUP_COMPLETION_MARKER=".yb_env_setup_complete"

SCRIPT_PATH="$(readlink -f "${BASH_SOURCE[0]}")"
SCRIPT_DIR="$(dirname "$SCRIPT_PATH")"

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
cloud_type="onprem"
is_airgap=false
# By default, we use the virtual environment.
use_system_python=false
use_python_driver=false
curr_user_id=$(id -u)

# Retry function with 30 seconds delay between retries.
retry_cmd() {
    local retries=5
    local count=0
    local delay=30

    until "$@"; do
        exit_code=$?
        count=$((count + 1))
        if [ $count -lt $retries ]; then
            echo "Command failed. Attempt $count/$retries. Retrying in $delay seconds..."
            sleep $delay
        else
            echo "Command failed after $retries attempts."
            return $exit_code
        fi
    done
    return 0
}

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
    python3_path=$(command -v python3)
    ln -sf "$python3_path" /usr/bin/python
    echo "Python symlinks updated."
}

# Get the root path of the virtual environment when it is activated.
get_activated_venv_path() {
    PYTHON_CMD=$(command -v python3)
    PYTHON_VENV_PATH=$($PYTHON_CMD -c "import sys; print(sys.prefix)")
    echo "$PYTHON_VENV_PATH"
}

# User specific path suffix for the virtual environment.
get_env_path_suffix() {
    echo "venv/$(id -u -n)"
}

# Function to check for Python. Python command is needed.
check_python() {
    echo "Checking for Python and pip..."
    if ! command -v python3 &> /dev/null; then
        err_msg "python3 is not installed or not found in PATH."
    fi
    echo "Python is available."
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

# Function to execute the Go script.
execute_go() {
    COMMAND_PATH="$SCRIPT_DIR/node-provisioner"
    if [[ ! -f "$COMMAND_PATH" ]]; then
        COMMAND_PATH="$SCRIPT_DIR/../bin/node-provisioner"
    fi
    YNP_BASE_PATH="$SCRIPT_DIR/ynp"
    "$COMMAND_PATH" --ynp_base_path "$YNP_BASE_PATH" "$@"
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
            [[ "$curr_user_id" -eq 0 ]] && is_csp=true
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
    check_python
    execute_go "${filtered_args[@]}"
}

# Call the main function and pass all arguments
main "$@"
