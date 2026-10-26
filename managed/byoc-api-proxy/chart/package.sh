#!/usr/bin/env bash
# Copyright (c) YugabyteDB, Inc.

set -euo pipefail

# Packages the byoc-api-proxy Helm chart, with the chart version and appVersion both set to the
# proxy release version, so a chart release always deploys the image of the same version.
# Usage: chart/package.sh <version X.Y.Z> <destination dir>
# Output: <destination dir>/byoc-api-proxy-<version>.tgz

if [[ $# -ne 2 ]]; then
  echo "Usage: ${0##*/} <version X.Y.Z> <destination dir>" >&2
  exit 1
fi
readonly version="$1"
readonly destination="$2"
if [[ ! "${version}" =~ ^[0-9]+\.[0-9]+\.[0-9]+(-SNAPSHOT)?$ ]]; then
  echo "Invalid version '${version}', expected X.Y.Z or X.Y.Z-SNAPSHOT" >&2
  exit 1
fi

readonly chart_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
mkdir -p "${destination}"
helm lint "${chart_dir}" \
  --set yba.uuid=00000000-0000-0000-0000-000000000000 \
  --set proxiedApp.baseUrl=https://example.com/api \
  --set proxiedApp.auth.apiKey=lint
helm package "${chart_dir}" --destination "${destination}" \
  --version "${version}" --app-version "${version}"
