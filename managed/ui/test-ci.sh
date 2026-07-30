#!/usr/bin/env bash
# Setup Node.js version using nvm or fallback methods, then run Vitest in CI mode.

set -ue -o pipefail

readonly UI_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Source the Node.js setup script
# shellcheck source=setup-node.sh
. "${UI_DIR}/setup-node.sh"

node node_modules/.bin/vitest run
