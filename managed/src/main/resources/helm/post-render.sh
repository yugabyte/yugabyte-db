#!/bin/bash
#
# Copyright 2026 YugabyteDB, Inc. and Contributors
#
# Helm post-renderer. Helm streams the rendered manifest to stdin and reads the transformed manifest
# back from stdout; kustomize applies the patch sitting next to this script. See
# PgDataOwnershipPostRenderer.
set -euo pipefail
cd "$(dirname "$0")"
cat > manifest.yaml
exec kubectl kustomize .
