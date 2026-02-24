#!/bin/bash

# Copyright (c) YugabyteDB, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
# in compliance with the License.  You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software distributed under the License
# is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
# or implied.  See the License for the specific language governing permissions and limitations under
# the License.

set -euo pipefail

check_architecture() {
    
    local release_root_arg="$1"
    
    # Retrieve the expected architecture from the package metadata (if available)
    # If not available, return early since we can't perform the check
    local metadata_file="$release_root_arg/version_metadata.json"
    if [[ ! -f "$metadata_file" ]]; then
        return
    fi
    
    # Ensure jq is available before attempting to parse the metadata.
    if ! command -v jq >/dev/null 2>&1; then
        echo "WARNING: 'jq' is not installed; skipping architecture check." >&2
        return
    fi
    
    # Extract the "architecture" field from the JSON metadata using jq.
    # If we can't access the file, or if it's missing or null, return early.
    local package_arch
    if ! package_arch=$(jq -r '.architecture' "$metadata_file"); then
        echo "WARNING: Failed to parse architecture from $metadata_file; skipping architecture check." >&2
        return
    fi
    if [[ -z "$package_arch" || "$package_arch" == "null" ]]; then
        return
    fi
    
    # Retrieve the system architecture
    local system_arch
    if ! system_arch=$(uname -m); then
        echo "WARNING: Could not determine system architecture; skipping check." >&2
        return
    fi
    
    # Normalize: replace "arm64" with "aarch64" since these terms are often used interchangeably
    # Use for comparison but use original values for messaging
    local norm_sys norm_pkg
    norm_sys="${system_arch/arm64/aarch64}"
    norm_pkg="${package_arch/arm64/aarch64}"
    
    # Warn if there's a mismatch between the system architecture and the package architecture.
    if [[ "$norm_sys" != "$norm_pkg" ]]; then
        echo "WARNING: Architecture mismatch ($package_arch package on $system_arch system). Installation may not work." >&2
        return
    fi

    echo "Architecture check passed ($system_arch)." >&2
}

release_root="${BASH_SOURCE%/*}/.."

check_architecture "$release_root"

"$release_root"/bin/fips_install.sh "$release_root/openssl-config/"
