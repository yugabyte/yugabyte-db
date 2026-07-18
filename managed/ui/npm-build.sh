#!/usr/bin/env bash
# Build the UI with correct Node.js version.
# This script sources setup-node.sh to configure the Node.js environment,
# then runs npm build commands.

set -ue -o pipefail

readonly UI_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Source the Node.js setup script
# shellcheck source=setup-node.sh
. "${UI_DIR}/setup-node.sh"

# Now run npm build command
npm run build-and-copy
