#!/usr/bin/env bash
# Install npm dependencies with correct Node.js version.
# This script sources setup-node.sh to configure the Node.js environment,
# then runs npm install commands.

set -ue -o pipefail

readonly UI_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Source the Node.js setup script
# shellcheck source=setup-node.sh
. "${UI_DIR}/setup-node.sh"

echo "[npm-install.sh] npm config:"
npm config list
npm cache verify

npm ci --legacy-peer-deps
