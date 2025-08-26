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
# By default, we use the virtual environment.
use_system_python=false
sudo_user="${SUDO_USER:-$USER}"

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
    ln -sf /usr/bin/python3 /usr/bin/python
    echo "Python symlinks updated."
}

# Function to install the python wheels
install_pywheels() {
    WHEEL_DIR="$(pwd)/pywheels"
    # Check if the wheel directory exists
    if [[ ! -d "$WHEEL_DIR" ]]; then
        err_msg "Wheel directory $WHEEL_DIR does not exist."
    fi

    PYTHON_CMD=$(command -v python3)
    PYTHON_VERSION_DETECTED=$(
        $PYTHON_CMD -c "import sys; print('python' + '.'.join(map(str, sys.version_info[:2])))"
    )
    PYTHON_MAJOR_MINOR=$(
        $PYTHON_CMD -c "import sys; print('.'.join(map(str, sys.version_info[:2])))"
    )
    # Select the appropriate requirements file based on Python version
    if [[ "$PYTHON_MAJOR_MINOR" == "3.6" || "$PYTHON_MAJOR_MINOR" == "3.7" ]]; then
        REQUIREMENTS_FILE="$(pwd)/ynp_requirements_3.6.txt"
        echo "Using Python 3.6/3.7 compatible requirements file: $REQUIREMENTS_FILE"
    else
        REQUIREMENTS_FILE="$(pwd)/ynp_requirements.txt"
        echo "Using Python 3.8+ requirements file: $REQUIREMENTS_FILE"
    fi

    echo "Installing Python wheels from directory: $WHEEL_DIR using requirements: $REQUIREMENTS_FILE"

    # Check if requirements file exists
    if [[ ! -f "$REQUIREMENTS_FILE" ]]; then
        err_msg "Requirements file $REQUIREMENTS_FILE does not exist."
    fi
    # If we are using system python, we need to use --break-system-packages to install packages.
    extra_pip_flags=""
    if [[ "$PYTHON_MAJOR_MINOR" > "3.11" && "$use_system_python" == true ]]; then
        extra_pip_flags="--break-system-packages"
    fi

    echo "Using Python version: $PYTHON_VERSION_DETECTED"

    # Get the virtual environment path
    virtualenv_dir=$(pwd)
    YB_VIRTUALENV_BASENAME="venv"
    VENV_PATH="$virtualenv_dir/$YB_VIRTUALENV_BASENAME"

    # Check if virtual environment exists and use it
    if [[ -d "$VENV_PATH" && "$use_system_python" == false ]]; then
        echo "Using virtual environment at $VENV_PATH"
        # Use the virtual environment's pip directly (no need to source activate)
        PIP_CMD="$VENV_PATH/bin/pip"
        PYTHON_CMD="$VENV_PATH/bin/python"

    elif [[ "$use_system_python" == true ]]; then
        echo "Using system Python"
        PIP_CMD="python3 -m pip"
        PYTHON_CMD="python3"
    else
        echo "Error: Virtual environment not found and system Python not enabled"
        exit 1
    fi

    # Install packages in specific order to handle build dependencies
    if [[ "$PYTHON_MAJOR_MINOR" == "3.6" || "$PYTHON_MAJOR_MINOR" == "3.7" ]]; then
        echo "Installing packages in order for Python 3.6/3.7 compatibility..."

        # Install setuptools and wheel first
        $PIP_CMD install --no-index --ignore-installed $extra_pip_flags \
            --find-links="$WHEEL_DIR" setuptools==53.0.0 wheel==0.37.1 || {
            echo "Error installing setuptools and wheel"
            exit 1
        }
        $PIP_CMD install --no-index --ignore-installed $extra_pip_flags \
            --find-links="$WHEEL_DIR" -r "$REQUIREMENTS_FILE" || {
            echo "Error installing packages from requirements file"
            exit 1
        }
    else
        # Install setuptools and wheel first for Python 3.8+
        $PIP_CMD install --no-index --ignore-installed $extra_pip_flags \
            --find-links="$WHEEL_DIR" setuptools==69.5.1 wheel==0.43.0 || {
            echo "Error installing setuptools and wheel"
            exit 1
        }
        # For Python 3.8+, install all packages at once
        $PIP_CMD install --no-index --no-build-isolation --ignore-installed $extra_pip_flags \
            --find-links="$WHEEL_DIR" -r "$REQUIREMENTS_FILE" || {
            echo "Retrying without --no-build-isolation installing packages from requirements file"
            $PIP_CMD install --no-index --ignore-installed --find-links="$WHEEL_DIR" $extra_pip_flags \
                -r "$REQUIREMENTS_FILE" || {
                echo "Error installing packages from requirements file"
                exit 1
            }
        }
    fi
    echo "All packages installed successfully."
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
                retry_cmd apt update && retry_cmd apt install -y python3-venv
            elif grep -q -i "almalinux\|rocky\|rhel\|amazon linux 2023" /etc/os-release; then
                retry_cmd dnf install -y python3-venv
            elif grep -q -i "sles\|suse" /etc/os-release; then
                retry_cmd zypper install -y python3-venv
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

    # Changing the ownership of the virtual environment to the current user.
    chown -R "$sudo_user":"$sudo_user" "$VENV_PATH"

    if source "$VENV_PATH/bin/activate"; then
        echo "Virtual environment activated."
    else
        if [[ "$use_system_python" == true ]]; then
            echo "Using system python..."
        else
            echo "Failed to activate virtual environment. Continuing without it..."
            err_msg "Failed to activate virtual environment. Please check if the virtual \
                    environment is correctly set up."
        fi
    fi
}

# Function to setup pip in venv.
setup_pip() {
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
        elif [[ "${!i}" == "--use_system_python" ]]; then
            use_system_python=true  # Set the flag
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
