#!/usr/bin/env bash
#
# YugabyteDB 2025.x — discover all ALIVE YB-Master RPC endpoints for a universe and
# print a shell assignment for MASTERS (comma-separated host:port list for
#   yb-admin --master_addresses "$MASTERS"
# ).
#
# Requires: yb-admin on PATH or set YB_ADMIN to the binary path.
#
# Usage:
#   eval "$(./scripts/yb-universe-masters-env.sh 10.0.0.1:7100)"
#   eval "$(./scripts/yb-universe-masters-env.sh -- 10.0.0.1:7100,10.0.0.2:7100)"
#
# Environment (optional):
#   YB_ADMIN          Path to yb-admin (default: yb-admin from PATH)
#   YB_CERTS_DIR      TLS: directory with certs (-certs_dir_name)
#   YB_TIMEOUT_MS     RPC timeout for yb-admin (default: 60000)
#
set -euo pipefail

YB_ADMIN="${YB_ADMIN:-yb-admin}"
CERTS_FLAG=()
TIMEOUT_FLAG=()
VALUE_ONLY=false
SEED=""

usage() {
  cat <<'EOF'
Usage: yb-universe-masters-env.sh [options] <master-seed>

  <master-seed>   One RPC address (host:port) uses --init_master_addrs to discover
                  the full master set; a comma-separated list uses --master_addresses.

Options:
  --yb-admin PATH       Path to yb-admin binary
  --certs_dir_name DIR  TLS certificate directory (-certs_dir_name)
  --timeout_ms MS       yb-admin RPC timeout
  --value-only          Print only the comma-separated list (no export)
  -h, --help            Show this help

Environment: YB_ADMIN, YB_CERTS_DIR, YB_TIMEOUT_MS (same as flags when set)
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help)
      usage
      exit 0
      ;;
    --yb-admin)
      YB_ADMIN="$2"
      shift 2
      ;;
    --certs_dir_name|--certs-dir)
      CERTS_FLAG=(--certs_dir_name "$2")
      shift 2
      ;;
    --timeout_ms|--timeout-ms)
      TIMEOUT_FLAG=(--timeout_ms "$2")
      shift 2
      ;;
    --value-only)
      VALUE_ONLY=true
      shift
      ;;
    --)
      shift
      break
      ;;
    -*)
      echo "Unknown option: $1" >&2
      exit 2
      ;;
    *)
      if [[ -n "${SEED}" ]]; then
        echo "error: only one seed argument allowed (got extra: $1)" >&2
        exit 2
      fi
      SEED="$1"
      shift
      ;;
  esac
done

if [[ $# -gt 0 ]]; then
  if [[ -n "${SEED}" ]]; then
    echo "error: only one seed argument allowed" >&2
    exit 2
  fi
  SEED="$1"
fi

if [[ -z "${SEED}" ]]; then
  echo "error: pass at least one master RPC address (host:port), e.g. 10.0.0.1:7100" >&2
  echo "       or a comma-separated list to use as --master_addresses." >&2
  exit 1
fi

if [[ -n "${YB_CERTS_DIR:-}" ]]; then
  CERTS_FLAG=(--certs_dir_name "${YB_CERTS_DIR}")
fi

if [[ -n "${YB_TIMEOUT_MS:-}" ]]; then
  TIMEOUT_FLAG=(--timeout_ms "${YB_TIMEOUT_MS}")
fi

ADMIN_BASE=("${YB_ADMIN}" "${TIMEOUT_FLAG[@]}" "${CERTS_FLAG[@]}")

if [[ "${SEED}" == *","* ]]; then
  ADMIN_BASE+=(--master_addresses "${SEED}")
else
  ADMIN_BASE+=(--init_master_addrs "${SEED}")
fi

ADMIN_BASE+=(list_all_masters)

if ! OUT="$("${ADMIN_BASE[@]}" 2>&1)"; then
  echo "error: yb-admin list_all_masters failed:" >&2
  echo "${OUT}" >&2
  exit 1
fi

# Table columns: Master UUID | RPC Host/Port | State | Role | Broadcast
# State is ALIVE for healthy masters; see yb-admin list_all_masters.
LIST="$(
  echo "${OUT}" | awk '
    /^Master UUID/ { next }
    $1 ~ /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/ && NF >= 4 {
      if ($3 == "ALIVE" && $2 != "UNKNOWN") print $2
    }
  ' | sort -u | awk 'NR > 1 { printf "," } { printf "%s", $0 }'
)"

if [[ -z "${LIST}" ]]; then
  echo "error: no ALIVE masters parsed from list_all_masters output" >&2
  echo "${OUT}" >&2
  exit 1
fi

if [[ "${VALUE_ONLY}" == true ]]; then
  printf '%s\n' "${LIST}"
  exit 0
fi

printf 'export MASTERS=%q\n' "${LIST}"
