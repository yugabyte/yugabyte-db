#!/bin/bash
#
# Copyright 2026 YugabyteDB, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

# Integration test: stage server.conf, redact via yb-redact-server-conf, tar, assert redacted.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEVOPS_BIN="$(cd "$SCRIPT_DIR/../../bin" && pwd)"
# devops/bin holds only arch-suffixed binaries; nothing produces a bare-name symlink there.
HOST_ARCH="$(uname -m | sed 's/x86_64/amd64/;s/aarch64/arm64/')"
REDACT_BIN="${YB_REDACT_SERVER_CONF_BIN:-$DEVOPS_BIN/yb-redact-server-conf-linux-$HOST_ARCH}"

if [ ! -x "$REDACT_BIN" ]; then
  echo "Missing executable: $REDACT_BIN (run 'make -f GNUmakefile build-all' in serverconfredact/)"
  exit 1
fi

TMP_ROOT="$(mktemp -d)"
cleanup() {
  rm -rf "$TMP_ROOT"
}
trap cleanup EXIT

STAGING="$TMP_ROOT/staging"
mkdir -p "$STAGING/master/conf" "$STAGING/tserver/conf"
SECRET="MyLdapSecret123"
# The hba password is followed by clientcert=, and the last flag's value holds ',' and '='.
HBA="hostssl all all 0.0.0.0/0 ldap ldapbindpasswd=\"\"$SECRET\"\" clientcert=verify-full"
CONF_BODY="--webserver_port=13000
--ycql_ldap_bind_passwd=$SECRET
--ysql_hba_conf_csv=\"$HBA\"
--ycql_ldap_bind_passwd=$SECRET,def=$SECRET"
printf '%s\n' "$CONF_BODY" > "$STAGING/master/conf/server.conf"
printf '%s\n' "$CONF_BODY" > "$STAGING/tserver/conf/server.conf"

# shellcheck source=/dev/null
source "$DEVOPS_BIN/node_utils.sh"

status_output="$(redact_server_conf_in_staging "$STAGING" "$REDACT_BIN" "" "")"
if [ "$status_output" != "REDACTION_STATUS=NODE" ]; then
  echo "Expected REDACTION_STATUS=NODE after a successful node-side redaction, got: $status_output"
  exit 1
fi

for conf in "$STAGING/master/conf/server.conf" "$STAGING/tserver/conf/server.conf"; do
  if grep -q "$SECRET" "$conf"; then
    echo "Secret still present in $conf after redaction"
    exit 1
  fi
  if ! grep -q 'REDACTED' "$conf"; then
    echo "Expected REDACTED token in $conf"
    exit 1
  fi
done

REMOTE_TAR="$TMP_ROOT/gflags.tar.gz"
create_bundle_tar_as_current_user "$REMOTE_TAR" "$STAGING"

UNTAR_DIR="$TMP_ROOT/untar"
mkdir -p "$UNTAR_DIR"
tar -xzf "$REMOTE_TAR" -C "$UNTAR_DIR"

for conf in "$UNTAR_DIR/master/conf/server.conf" "$UNTAR_DIR/tserver/conf/server.conf"; do
  if grep -q "$SECRET" "$conf"; then
    echo "Secret leaked into tar at $conf"
    exit 1
  fi
  if ! grep -q 'REDACTED' "$conf"; then
    echo "Expected REDACTED in tar contents at $conf"
    exit 1
  fi
done

# A helper that can't run must not leave an unredacted copy behind: the node deletes it and reports
# REDACTION_FAILURE, so it never reaches the tar.
FALLBACK_STAGING="$TMP_ROOT/fallback"
FALLBACK_CONF="$FALLBACK_STAGING/master/conf/server.conf"
mkdir -p "$FALLBACK_STAGING/master/conf"
printf '%s\n' "$CONF_BODY" > "$FALLBACK_CONF"

set +e
missing_bin_output="$(redact_server_conf_in_staging \
  "$FALLBACK_STAGING" "$TMP_ROOT/does-not-exist/yb-redact-server-conf" "" "")"
missing_bin_rc=$?
set -e
if [ "$missing_bin_rc" -ne 0 ]; then
  echo "Expected exit 0 when the redaction helper is missing, got $missing_bin_rc"
  exit 1
fi
missing_bin_status="$(tail -n1 <<< "$missing_bin_output")"
if ! grep -qF "REDACTION_FAILURE=master/conf/server.conf: redaction helper not executable" \
    <<< "$missing_bin_output" || [ "$missing_bin_status" != "REDACTION_STATUS=FAILED" ]; then
  echo "Expected a REDACTION_FAILURE line and REDACTION_STATUS=FAILED, got: $missing_bin_output"
  exit 1
fi
if [ -e "$FALLBACK_CONF" ]; then
  echo "Expected the unredacted copy to be deleted when the helper is missing"
  exit 1
fi

# --drop_server_conf (YBA could not verify the helper) drops even when a working helper is present.
printf '%s\n' "$CONF_BODY" > "$FALLBACK_CONF"
drop_output="$(redact_server_conf_in_staging "$FALLBACK_STAGING" "$REDACT_BIN" "" "" "true")"
drop_status="$(tail -n1 <<< "$drop_output")"
if ! grep -qF "REDACTION_FAILURE=master/conf/server.conf: redaction helper unavailable" \
    <<< "$drop_output" || [ "$drop_status" != "REDACTION_STATUS=FAILED" ]; then
  echo "Expected REDACTION_FAILURE and REDACTION_STATUS=FAILED when dropping, got: $drop_output"
  exit 1
fi
if [ -e "$FALLBACK_CONF" ]; then
  echo "Expected the copy to be deleted when --drop_server_conf is set"
  exit 1
fi

# No server.conf staged at all is a distinct, conclusive outcome.
EMPTY_STAGING="$TMP_ROOT/empty"
mkdir -p "$EMPTY_STAGING/logs"
printf 'some log line\n' > "$EMPTY_STAGING/logs/yb-master.INFO"
empty_output="$(redact_server_conf_in_staging "$EMPTY_STAGING" "$REDACT_BIN" "" "")"
if [ "$empty_output" != "REDACTION_STATUS=NOT_APPLICABLE" ]; then
  echo "Expected REDACTION_STATUS=NOT_APPLICABLE with no server.conf, got: $empty_output"
  exit 1
fi

echo "serverconfredact integration test passed"
