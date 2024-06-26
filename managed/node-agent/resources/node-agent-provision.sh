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

# Include common functions
. "${BASH_SOURCE%/*}/../devops/bin/common.sh"

# Function to display usage information
show_usage() {
    echo "Usage: $0 [-c|--command COMMAND]"
    echo "  -c, --command COMMAND    Specify the command to run"
}

# Function to handle errors
err_msg() {
    echo "Error: $1" >&2
    show_usage
    exit 1
}

# Function to set up the environment
setup_environment() {
    detect_os
    activate_pex
}

# Function to check for Python
check_python() {
    if ! command -v python &> /dev/null; then
        err_msg "Python is not installed or not found in PATH."
    fi
}

# Function to execute the Python script
execute_python() {
    python ynp/main.py "$@"
}

# Main function
main() {
    setup_environment
    check_python
    execute_python "$@"
}

# Call the main function and pass all arguments
main "$@"
