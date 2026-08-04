#!/usr/bin/env bash
# Setup Node.js version using nvm or fallback methods, then build the UI.

set -ue -o pipefail

readonly UI_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Source the Node.js setup script
# shellcheck source=setup-node.sh
. "${UI_DIR}/setup-node.sh"

VITE_BUILD_COMMIT=$(git rev-parse HEAD)

CI="${CI:-false}" VITE_BUILD_COMMIT="$VITE_BUILD_COMMIT" node node_modules/.bin/vite build
npm run fetch-map

