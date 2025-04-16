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

    if [ ! -d "$VENV_PATH" ]; then
        echo "Creating virtual environment at $VENV_PATH..."
        if ! python3 -m venv "$VENV_PATH"; then
            echo "Failed to create virtual environment. Continuing without it..."
        fi
    else
        echo "Virtual environment already exists at $VENV_PATH."
    fi

    if source "$VENV_PATH/bin/activate"; then
        USER_NAME=$(logname 2>/dev/null || echo "$SUDO_USER" || whoami)
        chown -R $USER_NAME:$USER_NAME $VENV_PATH
        echo "Virtual environment activated."
    else
        echo "Failed to activate virtual environment. Continuing without it..."
    fi
}

# Function to check for Python
check_python() {
    echo "Checking for Python and pip..."
    if ! command -v python3 &> /dev/null; then
        err_msg "Python is not installed or not found in PATH."
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

# Main function
main() {
    check_python
    setup_virtualenv
    install_pywheels
    execute_python "$@"
}

# Call the main function and pass all arguments
main "$@"
