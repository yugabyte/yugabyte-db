#!/bin/bash
# Lint script to ensure client versions are updated when OpenAPI specs change
#
# This script checks if OpenAPI spec files have changed and verifies that
# the corresponding version in the client pom.xml files has been updated.

set -e

# Get the repository root (assuming script is in managed/lint/)
REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
MANAGED_DIR="$REPO_ROOT/managed"
V1_POM="$MANAGED_DIR/client/java/v1/pom.xml"
V2_POM="$MANAGED_DIR/client/java/v2/pom.xml"
PARENT_POM="$MANAGED_DIR/client/java/pom.xml"
V1_CONFIG="$MANAGED_DIR/client/java/openapi-java-config.json"
V2_CONFIG="$MANAGED_DIR/client/java/openapi-java-config-v2.json"

# Check if we're in a git repository
if ! git rev-parse --git-dir > /dev/null 2>&1; then
  echo "ERROR: Not in a git repository"
  exit 1
fi

# Get all changed files from git diff (arcanist passes individual files, but we need all)
# Try multiple methods to get the diff base
DIFF_BASE=""
if git rev-parse --verify origin/master > /dev/null 2>&1; then
  DIFF_BASE="origin/master"
elif git rev-parse --verify master > /dev/null 2>&1; then
  DIFF_BASE="master"
elif git rev-parse --verify HEAD~1 > /dev/null 2>&1; then
  DIFF_BASE="HEAD~1"
fi

# Get all changed files in the diff
if [ -n "$DIFF_BASE" ]; then
  CHANGED_FILES=($(git diff --name-only "$DIFF_BASE"...HEAD 2>/dev/null || \
                   git diff --name-only "$DIFF_BASE" HEAD 2>/dev/null || echo ""))
fi

# If still no files, try the arguments passed by arcanist
if [ ${#CHANGED_FILES[@]} -eq 0 ] || [ "${CHANGED_FILES[0]}" = "" ]; then
  CHANGED_FILES=("$@")
fi

# Track if we found spec changes
V1_SPEC_CHANGED=false
V2_SPEC_CHANGED=false
V1_POM_CHANGED=false
V2_POM_CHANGED=false

# Check for v1 spec changes (swagger.json)
for file in "${CHANGED_FILES[@]}"; do
  # Normalize path (remove leading ./ if present)
  file="${file#./}"

  if [[ "$file" == "managed/src/main/resources/swagger.json" ]]; then
    V1_SPEC_CHANGED=true
    break
  fi
done

# Check for v2 spec changes (openapi/**/*.yaml)
for file in "${CHANGED_FILES[@]}"; do
  file="${file#./}"

  if [[ "$file" =~ ^managed/src/main/resources/openapi/.*\.yaml$ ]]; then
    V2_SPEC_CHANGED=true
    break
  fi
done

# Check if pom.xml files were changed
for file in "${CHANGED_FILES[@]}"; do
  file="${file#./}"

  if [[ "$file" == "managed/client/java/v1/pom.xml" ]]; then
    V1_POM_CHANGED=true
  fi
  if [[ "$file" == "managed/client/java/v2/pom.xml" ]]; then
    V2_POM_CHANGED=true
  fi
done

# If no spec files changed, exit successfully
if [ "$V1_SPEC_CHANGED" = false ] && [ "$V2_SPEC_CHANGED" = false ]; then
  exit 0
fi

# If pom.xml wasn't changed, that's an error
if [ "$V1_SPEC_CHANGED" = true ] && [ "$V1_POM_CHANGED" = false ]; then
  echo "ERROR: v1 spec changed but v1/pom.xml not modified. Bump version in managed/build.sbt and compile again so pom.xml is regenerated."
  exit 1
fi

if [ "$V2_SPEC_CHANGED" = true ] && [ "$V2_POM_CHANGED" = false ]; then
  echo "ERROR: v2 spec changed but v2/pom.xml not modified. Bump version in managed/build.sbt and compile again so pom.xml is regenerated."
  exit 1
fi

# Pom.xml was changed, now verify the version actually changed
# Extract version from pom.xml (the <version> tag in the project element, typically line 8)
extract_version() {
  local pom_file="$1"
  # Extract the version from the project/version element (first <version> tag after <project>)
  grep -A 5 "<project" "$pom_file" 2>/dev/null | grep "<version>" | head -1 | sed 's/.*<version>\([^<]*\)<\/version>.*/\1/' || echo ""
}

# Extract parentVersion from JSON config file (without jq)
extract_parent_version() {
  local config_file="$1"
  # Extract parentVersion value from JSON - look for "parentVersion" followed by colon and value
  grep -o '"parentVersion"[[:space:]]*:[[:space:]]*"[^"]*"' "$config_file" 2>/dev/null | sed 's/.*"parentVersion"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/' || echo ""
}

# Get current versions from working directory
CURRENT_V1_VERSION=$(extract_version "$V1_POM")
CURRENT_V2_VERSION=$(extract_version "$V2_POM")

# Try to get the version from the base branch for comparison
BASE_V1_VERSION=""
BASE_V2_VERSION=""

# Try to get base versions from the diff base
if [ -n "$DIFF_BASE" ]; then
  if git show "${DIFF_BASE}:managed/client/java/v1/pom.xml" > /dev/null 2>&1; then
    BASE_V1_VERSION=$(git show "${DIFF_BASE}:managed/client/java/v1/pom.xml" 2>/dev/null | grep -A 5 "<project" | grep "<version>" | head -1 | sed 's/.*<version>\([^<]*\)<\/version>.*/\1/' || echo "")
  fi
  if git show "${DIFF_BASE}:managed/client/java/v2/pom.xml" > /dev/null 2>&1; then
    BASE_V2_VERSION=$(git show "${DIFF_BASE}:managed/client/java/v2/pom.xml" 2>/dev/null | grep -A 5 "<project" | grep "<version>" | head -1 | sed 's/.*<version>\([^<]*\)<\/version>.*/\1/' || echo "")
  fi
fi

# If we couldn't get base versions, we can't verify - assume it's OK
if [ -z "$BASE_V1_VERSION" ] && [ -z "$BASE_V2_VERSION" ]; then
  # Can't compare, assume developer updated it correctly
  exit 0
fi

# Check if versions were actually updated
ERRORS=0

if [ "$V1_SPEC_CHANGED" = true ] && [ -n "$BASE_V1_VERSION" ] && [ -n "$CURRENT_V1_VERSION" ]; then
  if [ "$BASE_V1_VERSION" = "$CURRENT_V1_VERSION" ]; then
    echo "ERROR: v1 spec changed but v1/pom.xml version unchanged ($CURRENT_V1_VERSION). Bump version in managed/build.sbt and compile again."
    ERRORS=$((ERRORS + 1))
  fi
fi

if [ "$V2_SPEC_CHANGED" = true ] && [ -n "$BASE_V2_VERSION" ] && [ -n "$CURRENT_V2_VERSION" ]; then
  if [ "$BASE_V2_VERSION" = "$CURRENT_V2_VERSION" ]; then
    echo "ERROR: v2 spec changed but v2/pom.xml version unchanged ($CURRENT_V2_VERSION). Bump version in managed/build.sbt and compile again."
    ERRORS=$((ERRORS + 1))
  fi
fi

# Validate that parentVersion in config files matches pom.xml version
CURRENT_PARENT_VERSION=$(extract_version "$PARENT_POM")
V1_PARENT_VERSION=$(extract_parent_version "$V1_CONFIG")
V2_PARENT_VERSION=$(extract_parent_version "$V2_CONFIG")

if [ -n "$CURRENT_PARENT_VERSION" ] && [ -n "$V1_PARENT_VERSION" ]; then
  if [ "$V1_PARENT_VERSION" != "$CURRENT_PARENT_VERSION" ]; then
    echo "ERROR: openapi-java-config.json parentVersion ($V1_PARENT_VERSION) != client/java/pom.xml ($CURRENT_PARENT_VERSION). Sync and compile again."
    ERRORS=$((ERRORS + 1))
  fi
fi

if [ -n "$CURRENT_PARENT_VERSION" ] && [ -n "$V2_PARENT_VERSION" ]; then
  if [ "$V2_PARENT_VERSION" != "$CURRENT_PARENT_VERSION" ]; then
    echo "ERROR: openapi-java-config-v2.json parentVersion ($V2_PARENT_VERSION) != client/java/pom.xml ($CURRENT_PARENT_VERSION). Sync and compile again."
    ERRORS=$((ERRORS + 1))
  fi
fi

if [ $ERRORS -gt 0 ]; then
  exit 1
fi

exit 0
