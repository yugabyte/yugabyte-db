#!/bin/bash
#
# Copyright 2026 YugabyteDB, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

# Cases covering devops/bin/support_bundle_utils.sh, the script that runs on DB nodes.
#
# Needs python3; needs no Go helper binaries.
#   bash managed/devops/supportbundle/support_bundle_utils_test.sh

# shellcheck source=./test_helpers.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/test_helpers.sh"

SB_UTILS="$DEVOPS_BIN/support_bundle_utils.sh"

# ===============================================================================================
# Bundle tar status
# ===============================================================================================
# A bundle entrypoint whose tar fails must report BUNDLE_STATUS=FAILED instead of OK. YBA only
# checks that the tar exists, so an entrypoint that ignores tar's exit status ships a truncated
# archive as a successful component.

# A regular file where a directory is needed: tar cannot create the archive, and it fails the same
# way for root, unlike a chmod-based setup.
BLOCKER="$TMP_ROOT/blocker"
printf 'regular file, not a directory\n' > "$BLOCKER"

GFLAGS_HOME="$TMP_ROOT/gflags-home"
mkdir -p "$GFLAGS_HOME/master/conf"
printf -- '--webserver_port=13000\n' > "$GFLAGS_HOME/master/conf/server.conf"

# Stands in for yb-redact-server-conf: overwrites the file it is given with a redacted line, or
# exits 3 when the path contains $REDACT_STUB_FAIL_ON.
REDACT_STUB="$(stub_command yb-redact-server-conf <<'STUB'
#!/bin/sh
file=""
while [ $# -gt 0 ]; do
  if [ "$1" = "--file" ]; then file=$2; fi
  shift
done
case "$file" in *"${REDACT_STUB_FAIL_ON:-<none>}"*) exit 3 ;; esac
printf -- '--ldap_bind_passwd=REDACTED\n' > "$file"
STUB
)/yb-redact-server-conf"

run_gflags_bundle() {
  bash "$SB_UTILS" create_gflags_bundle \
    --node_home_dir "$GFLAGS_HOME" \
    --remote_tar_path "$1" \
    --gflag_file master/conf/server.conf \
    --redact_bin "$REDACT_STUB" 2>&1
}

output="$(run_gflags_bundle "$TMP_ROOT/out/gflags.tar.gz")"
expect_bundle_status "node entrypoint reports OK when tar succeeds" OK zero "$?" "$output"

if ! tar -tzf "$TMP_ROOT/out/gflags.tar.gz" > /dev/null 2>&1; then
  fail "node entrypoint: reported OK but produced an unreadable tar"
fi

output="$(run_gflags_bundle "$BLOCKER/gflags.tar.gz")"
expect_bundle_status "node entrypoint reports FAILED when tar fails" FAILED nonzero "$?" "$output"

# A tar that writes part of the archive and then fails, as on a disk that fills mid-write.
PARTIAL_TAR_STUB="$(stub_command tar <<'STUB'
#!/bin/sh
while [ $# -gt 0 ]; do
  if [ "$1" = "-czf" ]; then printf 'partial' > "$2"; fi
  shift
done
exit 2
STUB
)"
partial_tar="$TMP_ROOT/out/gflags-partial.tar.gz"
output="$(export PATH="$PARTIAL_TAR_STUB:$PATH"; run_gflags_bundle "$partial_tar")"
expect_bundle_status "node entrypoint reports FAILED when tar fails part-way" FAILED nonzero "$?" \
  "$output"
check "node entrypoint removes a partial tar" \
  "$([ -e "$partial_tar" ] && echo present || echo absent)" absent

# ===============================================================================================
# server.conf is redacted only on the node
# ===============================================================================================
# An unredacted server.conf must never reach the tar. A staged copy the helper can't redact is
# deleted on the node and reported on stdout as REDACTION_FAILURE, which YBA logs as an error; when
# the copy can't be deleted the component fails without building a tar. Output is captured from
# stdout only, since that is all YBA receives from a successful script.
SECRET="NodeOnlySecret123"
REDACT_HOME="$TMP_ROOT/redact-home"
mkdir -p "$REDACT_HOME/master/conf" "$REDACT_HOME/tserver/conf"
for role in master tserver; do
  printf -- '--ldap_bind_passwd=%s\n' "$SECRET" > "$REDACT_HOME/$role/conf/server.conf"
done

run_redact_gflags() {
  local tar_path=$1
  shift
  bash "$SB_UTILS" create_gflags_bundle \
    --node_home_dir "$REDACT_HOME" --remote_tar_path "$tar_path" "$@" 2>/dev/null
}
tar_server_confs() {
  tar -tzf "$1" 2>/dev/null | grep -F '/server.conf' | sed 's|^\./||' | sort | tr '\n' ' '
}
tar_secret_count() {
  tar -xzOf "$1" 2>/dev/null | grep -c "$SECRET"
}
originals_intact() {
  [ "$(cat "$REDACT_HOME"/*/conf/server.conf | grep -c "$SECRET")" -eq 2 ] && echo yes || echo no
}

tar_path="$TMP_ROOT/out/redact-all.tar.gz"
output="$(run_redact_gflags "$tar_path" --redact_bin "$REDACT_STUB")"
expect_bundle_status "redaction: helper redacts every file" OK zero "$?" "$output"
check_contains "redaction: helper redacts every file -> NODE" "$output" "REDACTION_STATUS=NODE"
check "redaction: both files collected" "$(tar_server_confs "$tar_path")" \
  "master/conf/server.conf tserver/conf/server.conf "
check "redaction: no secret in the tar" "$(tar_secret_count "$tar_path")" 0
check "redaction: the node's own server.conf is untouched" "$(originals_intact)" yes

tar_path="$TMP_ROOT/out/redact-drop.tar.gz"
output="$(run_redact_gflags "$tar_path" --redact_bin "$REDACT_STUB" --drop_server_conf)"
expect_bundle_status "redaction: --drop_server_conf leaves nothing to tar" EMPTY zero "$?" "$output"
check_contains "redaction: --drop_server_conf reports each file" "$output" \
  "REDACTION_FAILURE=master/conf/server.conf: redaction helper unavailable on this node"
check_contains "redaction: --drop_server_conf reports each file (tserver)" "$output" \
  "REDACTION_FAILURE=tserver/conf/server.conf: redaction helper unavailable on this node"
check_contains "redaction: --drop_server_conf -> FAILED" "$output" "REDACTION_STATUS=FAILED"
check "redaction: --drop_server_conf builds no tar" \
  "$([ -e "$tar_path" ] && echo present || echo absent)" absent
check "redaction: --drop_server_conf leaves the node's server.conf untouched" \
  "$(originals_intact)" yes

# Only the binary YBA verified is used: without --redact_bin nothing is looked up on the node.
tar_path="$TMP_ROOT/out/redact-nobin.tar.gz"
output="$(PATH="$(dirname "$REDACT_STUB"):$PATH" run_redact_gflags "$tar_path")"
expect_bundle_status "redaction: no --redact_bin drops rather than searching PATH" EMPTY zero "$?" \
  "$output"
check_contains "redaction: no --redact_bin reports why" "$output" \
  "REDACTION_FAILURE=master/conf/server.conf: this component doesn't redact server.conf"

tar_path="$TMP_ROOT/out/redact-badbin.tar.gz"
output="$(run_redact_gflags "$tar_path" --redact_bin "$TMP_ROOT/no-such-helper")"
expect_bundle_status "redaction: a missing --redact_bin file drops" EMPTY zero "$?" "$output"
check_contains "redaction: a missing --redact_bin file reports why" "$output" \
  "REDACTION_FAILURE=master/conf/server.conf: redaction helper not executable"

tar_path="$TMP_ROOT/out/redact-partial.tar.gz"
output="$(REDACT_STUB_FAIL_ON=tserver run_redact_gflags "$tar_path" --redact_bin "$REDACT_STUB")"
expect_bundle_status "redaction: one failure keeps the redacted file" OK zero "$?" "$output"
check_contains "redaction: the failed file is reported" "$output" \
  "REDACTION_FAILURE=tserver/conf/server.conf: redaction helper failed (exit 3); dropped"
check_contains "redaction: a partial result is FAILED" "$output" "REDACTION_STATUS=FAILED"
check "redaction: only the redacted file is collected" "$(tar_server_confs "$tar_path")" \
  "master/conf/server.conf "
check "redaction: no secret in a partial tar" "$(tar_secret_count "$tar_path")" 0

# A copy that can't be deleted fails the component, so no tar can carry it.
rm() {
  case "$*" in *server.conf*) return 1 ;; esac
  command rm "$@"
}
export -f rm
tar_path="$TMP_ROOT/out/redact-undeletable.tar.gz"
output="$(run_redact_gflags "$tar_path" --drop_server_conf)"
rc=$?
unset -f rm
expect_bundle_status "redaction: an undeletable copy fails the component" FAILED nonzero "$rc" \
  "$output"
check_contains "redaction: an undeletable copy is reported" "$output" \
  "could not delete the unredacted copy, so nothing from this component is collected"
check "redaction: an undeletable copy builds no tar" \
  "$([ -e "$tar_path" ] && echo present || echo absent)" absent

# YbcLogs keeps its logs when its server.conf is dropped.
YBC_REDACT_HOME="$TMP_ROOT/ybc-redact-home"
YBC_REDACT_LOG="yb-controller.u-n1.log.INFO.20260115-120000.1"
mkdir -p "$YBC_REDACT_HOME/ybc/logs" "$YBC_REDACT_HOME/controller/conf"
printf 'controller log line\n' > "$YBC_REDACT_HOME/ybc/logs/$YBC_REDACT_LOG"
printf -- '--ldap_bind_passwd=%s\n' "$SECRET" > "$YBC_REDACT_HOME/controller/conf/server.conf"
tar_path="$TMP_ROOT/out/ybc-drop.tar.gz"
output="$(bash "$SB_UTILS" create_ybc_logs_bundle \
  --node_home_dir "$YBC_REDACT_HOME" \
  --ybc_logs_path "$YBC_REDACT_HOME/ybc/logs" \
  --start_date "2026-01-01" \
  --end_date "2026-01-31" \
  --remote_tar_path "$tar_path" \
  --drop_server_conf 2>/dev/null)"
expect_bundle_status "redaction: YbcLogs still ships its logs" OK zero "$?" "$output"
check_contains "redaction: YbcLogs reports its dropped server.conf" "$output" \
  "REDACTION_FAILURE=controller/conf/server.conf: redaction helper unavailable on this node"
ybc_logs_in_tar="$(tar -tzf "$tar_path" 2>/dev/null | grep -c "$YBC_REDACT_LOG")"
check "redaction: YbcLogs tar has the log but no server.conf" \
  "$ybc_logs_in_tar/$(tar_server_confs "$tar_path")" "1/"

# With nothing to collect there is nothing to redact; without the marker YBA reports a failure.
markers() {
  printf '%s\n' "$1" | grep -E '^(REDACTION|BUNDLE)_STATUS=' | tr '\n' ' '
}
EMPTY_HOME="$TMP_ROOT/empty-home"
mkdir -p "$EMPTY_HOME/ybc/logs"
output="$(bash "$SB_UTILS" create_gflags_bundle --node_home_dir "$EMPTY_HOME" \
  --remote_tar_path "$TMP_ROOT/out/gflags-empty.tar.gz" 2>/dev/null)"
expect_bundle_status "redaction: GFlags with nothing to stage" EMPTY zero "$?" "$output"
check "redaction: GFlags with nothing to stage -> NOT_APPLICABLE" "$(markers "$output")" \
  "REDACTION_STATUS=NOT_APPLICABLE BUNDLE_STATUS=EMPTY "
output="$(bash "$SB_UTILS" create_ybc_logs_bundle --node_home_dir "$EMPTY_HOME" \
  --ybc_logs_path "$EMPTY_HOME/ybc/logs" --start_date "2026-01-01" --end_date "2026-01-31" \
  --remote_tar_path "$TMP_ROOT/out/ybc-empty.tar.gz" 2>/dev/null)"
expect_bundle_status "redaction: YbcLogs with nothing to stage" EMPTY zero "$?" "$output"
check "redaction: YbcLogs with nothing to stage -> NOT_APPLICABLE" "$(markers "$output")" \
  "REDACTION_STATUS=NOT_APPLICABLE BUNDLE_STATUS=EMPTY "

# ===============================================================================================
# File lists stay under their base directory
# ===============================================================================================
# YBA sends file lists from its manifest; the script still refuses a path that is absolute or has a
# '..' segment. A server.conf a list stages is redacted when YBA sends the helper (redactServerConf)
# and dropped otherwise.
FILES_HOME="$TMP_ROOT/files-home"
mkdir -p "$FILES_HOME/tserver/conf" "$FILES_HOME/master/consensus-meta/conf"
printf 'tserver out\n' > "$FILES_HOME/tserver/tserver.out"
printf -- '--ldap_bind_passwd=%s\n' "$SECRET" > "$FILES_HOME/tserver/conf/server.conf"
printf 'meta\n' > "$FILES_HOME/master/consensus-meta/meta-1"
printf -- '--ldap_bind_passwd=%s\n' "$SECRET" > "$FILES_HOME/master/consensus-meta/conf/server.conf"
printf 'outside the base\n' > "$TMP_ROOT/outside.txt"

for bad_path in "../outside.txt" "/etc/hostname" "tserver/../../outside.txt" ".."; do
  tar_path="$TMP_ROOT/out/escape.tar.gz"
  rm -f "$tar_path"
  output="$(bash "$SB_UTILS" create_output_files_bundle --node_home_dir "$FILES_HOME" \
    --remote_tar_path "$tar_path" --output_file tserver/tserver.out \
    --output_file "$bad_path" 2>/dev/null)"
  expect_bundle_status "file list: '$bad_path' fails the component" FAILED nonzero "$?" "$output"
  check "file list: '$bad_path' builds no tar" \
    "$([ -e "$tar_path" ] && echo present || echo absent)" absent
done

# Every file-list entrypoint applies the check: <entrypoint> <dir flag> <file flag>.
for entry in "create_output_files_bundle --node_home_dir --output_file" \
  "create_error_files_bundle --node_home_dir --error_file" \
  "create_gflags_bundle --node_home_dir --gflag_file" \
  "create_instance_bundle --node_data_dir --instance_file" \
  "create_consensus_meta_bundle --node_data_dir --consensus_meta_file" \
  "create_tablet_meta_bundle --node_data_dir --tablet_meta_file" \
  "create_node_agent_bundle --node_agent_home_dir --node_agent_path"; do
  read -r fn dir_flag file_flag <<< "$entry"
  output="$(bash "$SB_UTILS" "$fn" "$dir_flag" "$FILES_HOME" \
    --remote_tar_path "$TMP_ROOT/out/$fn-escape.tar.gz" "$file_flag" ../outside.txt 2>/dev/null)"
  check_contains "file list: $fn refuses '..'" "$output" \
    "Refusing file path '../outside.txt'"
done

output="$(bash "$SB_UTILS" create_instance_bundle --node_home_dir / \
  --remote_tar_path "$TMP_ROOT/out/alias.tar.gz" --instance_file etc/hostname 2>/dev/null)"
expect_bundle_status "file list: Instance no longer takes --node_home_dir" FAILED nonzero "$?" \
  "$output"
check_contains "file list: --node_home_dir is an unknown argument" "$output" \
  "Unknown argument: --node_home_dir"

run_output_files_conf() {
  bash "$SB_UTILS" create_output_files_bundle --node_home_dir "$FILES_HOME" \
    --remote_tar_path "$1" --output_file tserver/tserver.out \
    --output_file tserver/conf/server.conf "${@:2}" 2>/dev/null
}
tar_conf_count() {
  tar_server_confs "$1" | wc -w | tr -d ' '
}
# <tserver.out entries>/<server.conf entries>/<secret lines> in a tar.
tar_counts() {
  echo "$(tar -tzf "$1" 2>/dev/null | grep -c "$2")/$(tar_conf_count "$1")/$(tar_secret_count "$1")"
}

tar_path="$TMP_ROOT/out/output-files-conf.tar.gz"
output="$(run_output_files_conf "$tar_path" --redact_bin "$REDACT_STUB")"
expect_bundle_status "file list: OutputFiles ships its files" OK zero "$?" "$output"
check_contains "file list: OutputFiles redacts the server.conf it staged" "$output" \
  "REDACTION_STATUS=NODE"
check "file list: OutputFiles tar has tserver.out and a redacted server.conf" \
  "$(tar_counts "$tar_path" 'tserver.out$')" "1/1/0"

for label in "no helper" "helper fails"; do
  tar_path="$TMP_ROOT/out/output-files-conf-${label// /-}.tar.gz"
  helper_args=()
  [ "$label" = "helper fails" ] && helper_args=(--redact_bin "$REDACT_STUB")
  output="$(REDACT_STUB_FAIL_ON=server.conf run_output_files_conf "$tar_path" \
    "${helper_args[@]}")"
  expect_bundle_status "file list: OutputFiles, $label, ships its other files" OK zero "$?" \
    "$output"
  reason="this component doesn't redact server.conf"
  [ "$label" = "helper fails" ] && reason="redaction helper failed (exit 3)"
  check_contains "file list: OutputFiles, $label, reports the dropped server.conf" "$output" \
    "REDACTION_FAILURE=output_files/server.conf: $reason"
  check "file list: OutputFiles, $label, tar has tserver.out but no server.conf or secret" \
    "$(tar_counts "$tar_path" 'tserver.out$')" "1/0/0"
done

tar_path="$TMP_ROOT/out/output-files-only-conf.tar.gz"
output="$(bash "$SB_UTILS" create_output_files_bundle --node_home_dir "$FILES_HOME" \
  --remote_tar_path "$tar_path" --output_file tserver/conf/server.conf --drop_server_conf \
  2>/dev/null)"
expect_bundle_status "file list: a list of only a dropped server.conf leaves nothing to tar" EMPTY \
  zero "$?" "$output"
check_contains "file list: --drop_server_conf gives the helper-unavailable reason" "$output" \
  "REDACTION_FAILURE=output_files/server.conf: redaction helper unavailable on this node"

tar_path="$TMP_ROOT/out/consensus-conf.tar.gz"
output="$(bash "$SB_UTILS" create_consensus_meta_bundle --node_data_dir "$FILES_HOME" \
  --remote_tar_path "$tar_path" --consensus_meta_file master/consensus-meta \
  --redact_bin "$REDACT_STUB" 2>/dev/null)"
expect_bundle_status "file list: a listed directory ships" OK zero "$?" "$output"
check "file list: the directory's server.conf is redacted, its files kept" \
  "$(tar_counts "$tar_path" 'meta-1$')" "1/1/0"

# Every file-list entrypoint takes the redaction flags, so marking one redactServerConf needs no
# script change.
for entry in "create_output_files_bundle --node_home_dir --output_file" \
  "create_error_files_bundle --node_home_dir --error_file" \
  "create_gflags_bundle --node_home_dir --gflag_file" \
  "create_instance_bundle --node_data_dir --instance_file" \
  "create_consensus_meta_bundle --node_data_dir --consensus_meta_file" \
  "create_tablet_meta_bundle --node_data_dir --tablet_meta_file" \
  "create_node_agent_bundle --node_agent_home_dir --node_agent_path"; do
  read -r fn dir_flag file_flag <<< "$entry"
  output="$(bash "$SB_UTILS" "$fn" "$dir_flag" "$FILES_HOME" \
    --remote_tar_path "$TMP_ROOT/out/$fn-flags.tar.gz" "$file_flag" tserver/tserver.out \
    --redact_bin "$REDACT_STUB" --yb_software_version 2.25.0.0 --sensitive_gflags_b64 e30= \
    --drop_server_conf 2>/dev/null)"
  check_contains "file list: $fn takes the redaction flags" "$output" \
    "REDACTION_STATUS=NOT_APPLICABLE"
done

check "file list: the node's own files are untouched" \
  "$(grep -c "$SECRET" "$FILES_HOME/tserver/conf/server.conf" \
    "$FILES_HOME/master/consensus-meta/conf/server.conf" | awk -F: '{s+=$2} END {print s}')" 2

# ===============================================================================================
# Estimate must not stage
# ===============================================================================================
# The estimate is measured from the source files, so any copy made on that path is written and then
# abandoned -- once per tablet, per node, on every estimate. It matters more than the bytes suggest:
# the estimate path never goes through the cgroup wrapper, so that work runs uncapped on a live node
# while the collection it predicts is capped.
#
# Intercepts cp rather than inspecting the staging tree, which the entrypoint deletes on exit.

ESTIMATE_HOME="$TMP_ROOT/estimate-home"
mkdir -p "$ESTIMATE_HOME/tserver/tablet-meta" "$ESTIMATE_HOME/tserver/consensus-meta"
for i in 1 2 3; do
  head -c 4096 /dev/zero | tr '\0' 'a' > "$ESTIMATE_HOME/tserver/tablet-meta/tablet-$i"
  head -c 2048 /dev/zero | tr '\0' 'b' > "$ESTIMATE_HOME/tserver/consensus-meta/tablet-$i"
done

# A cp on PATH that records every call, then does the real copy.
CP_STUB_DIR="$(stub_command cp <<'STUB'
#!/bin/bash
echo "$@" >> "$CP_CALL_LOG"
exec /bin/cp "$@"
STUB
)"

run_meta_entrypoint() {
  local fn=$1 estimate=$2 log=$3 tar_path=$4
  : > "$log"
  CP_CALL_LOG="$log" PATH="$CP_STUB_DIR:$PATH" \
    SUPPORT_BUNDLE_ESTIMATE_ONLY="$estimate" \
    bash "$SB_UTILS" "$fn" \
      --node_data_dir "$ESTIMATE_HOME" \
      --remote_tar_path "$tar_path" 2>&1
}

for fn in create_consensus_meta_bundle create_tablet_meta_bundle; do
  echo "--- $fn"
  log="$TMP_ROOT/$fn.cp.log"
  tar_path="$TMP_ROOT/$fn.tar.gz"

  out="$(run_meta_entrypoint "$fn" 1 "$log" "$tar_path")"
  check "  estimate reports a size" "$(grep -c 'ESTIMATE_STATUS=OK' <<< "$out")" "1"
  check "  estimate copies nothing" "$(wc -l < "$log")" "0"
  check "  estimate writes no tar" "$([ -f "$tar_path" ] && echo present || echo absent)" "absent"

  # Control: the collection path must still stage and tar, or the reorder broke collection.
  out="$(run_meta_entrypoint "$fn" "" "$log" "$tar_path")"
  check "  collection reports OK" "$(grep -c 'BUNDLE_STATUS=OK' <<< "$out")" "1"
  if [ "$(wc -l < "$log")" -gt 0 ]; then
    pass "  collection still copies"
  else
    fail "  collection copied nothing"
  fi
  check "  collection writes a tar" "$([ -f "$tar_path" ] && echo present || echo absent)" "present"
done

# ===============================================================================================
# yb-log-parse fallback
# ===============================================================================================
# UniverseLogs and YbcLogs must still collect when the yb-log-parse helper cannot be used. They are
# the two largest components in a bundle and used to be lost outright on a node the helper could not
# be copied to, even though all five of its uses have a shell/Python fallback.

LOG_REGEX='((?:.*)(?:yb-)(?:master|tserver)(?!.*WARNING)(?:.*))(\d{8}-\d{6})\.(?:.*)'

LOGPARSE_HOME="$TMP_ROOT/logparse-home"
mkdir -p "$LOGPARSE_HOME/master/logs" "$LOGPARSE_HOME/tserver/logs" "$LOGPARSE_HOME/ybc/logs"
MASTER_LOG="yb-master.u-n1.yugabyte.log.INFO.20260115-120000.1"
TSERVER_LOG="yb-tserver.u-n1.yugabyte.log.INFO.20260115-120000.1"
printf 'master log line\n' > "$LOGPARSE_HOME/master/logs/$MASTER_LOG"
printf 'tserver log line\n' > "$LOGPARSE_HOME/tserver/logs/$TSERVER_LOG"
YBC_LOG="yb-controller.u-n1.log.INFO.20260115-120000.1"
printf 'controller log line\n' > "$LOGPARSE_HOME/ybc/logs/$YBC_LOG"

# A helper that resolves but does not work. The smoke test must reject it, and the rejection must
# reach all five call sites -- not only the two that used to consult YB_LOG_PARSE_SKIP themselves.
BROKEN_HELPER="$TMP_ROOT/yb-log-parse-broken"
printf '#!/bin/sh\nexit 3\n' > "$BROKEN_HELPER"
chmod +x "$BROKEN_HELPER"

run_universe_logs() {
  local tar_path=$1
  bash "$SB_UTILS" create_universe_logs_bundle \
    --node_home_dir "$LOGPARSE_HOME" \
    --master_logs_path "$LOGPARSE_HOME/master/logs" \
    --tserver_logs_path "$LOGPARSE_HOME/tserver/logs" \
    --start_date "2026-01-01" \
    --end_date "2026-01-31" \
    --remote_tar_path "$tar_path" \
    --regex "$LOG_REGEX" 2>&1
}

run_ybc_logs() {
  local tar_path=$1
  bash "$SB_UTILS" create_ybc_logs_bundle \
    --node_home_dir "$LOGPARSE_HOME" \
    --ybc_logs_path "$LOGPARSE_HOME/ybc/logs" \
    --start_date "2026-01-01" \
    --end_date "2026-01-31" \
    --remote_tar_path "$tar_path" \
    --redact_bin "$REDACT_STUB" \
    --regex "$LOG_REGEX" 2>&1
}

expect_collected() {
  local name=$1 output=$2 tar_path=$3
  if grep -q "BUNDLE_STATUS=FAILED" <<< "$output"; then
    fail "$name" "reported FAILED rather than falling back" "$output"
    return
  fi
  if ! grep -qE "BUNDLE_STATUS=(OK|EMPTY)" <<< "$output"; then
    fail "$name" "no OK/EMPTY marker" "$output"
    return
  fi
  if grep -q "BUNDLE_STATUS=OK" <<< "$output" && ! tar -tzf "$tar_path" > /dev/null 2>&1; then
    fail "$name" "reported OK but produced no readable tar"
    return
  fi
  pass "$name"
}

# --- helper absent -----------------------------------------------------------------------------
output="$(YB_LOG_PARSE_SKIP=1 run_universe_logs "$TMP_ROOT/universe-absent.tar.gz")"
expect_collected "UniverseLogs collects with no yb-log-parse" "$output" \
  "$TMP_ROOT/universe-absent.tar.gz"

# The date window is what the fallback has to reproduce, so check a real log actually landed.
if ! tar -tzf "$TMP_ROOT/universe-absent.tar.gz" 2>/dev/null | grep -q "$MASTER_LOG"; then
  fail "UniverseLogs fallback: master log missing from the tar (date filtering degraded)"
else
  pass "UniverseLogs fallback still date-filters and collects the master log"
fi

output="$(YB_LOG_PARSE_SKIP=1 run_ybc_logs "$TMP_ROOT/ybc-absent.tar.gz")"
expect_collected "YbcLogs collects with no yb-log-parse" "$output" "$TMP_ROOT/ybc-absent.tar.gz"

# --- helper present but broken ------------------------------------------------------------------
output="$(YB_LOG_PARSE_BIN="$BROKEN_HELPER" run_universe_logs "$TMP_ROOT/universe-broken.tar.gz")"
expect_collected "UniverseLogs collects with a broken yb-log-parse" "$output" \
  "$TMP_ROOT/universe-broken.tar.gz"

if ! grep -q "failed validation" <<< "$output"; then
  fail "broken helper: expected the smoke test to reject it"
else
  pass "broken yb-log-parse is rejected by the smoke test, not used"
fi

# --- where the helper is looked for --------------------------------------------------------------
# Only under the YB home YBA passes; a copy under the run user's home is never picked up.
GOOD_YB_HOME="$TMP_ROOT/good-yb-home"
mkdir -p "$GOOD_YB_HOME/bin"
# shellcheck disable=SC2016 # expanded by the stub's own shell
printf '#!/bin/sh\n[ "$1" = add-days ] && echo "$2"\n' > "$GOOD_YB_HOME/bin/yb-log-parse"
chmod +x "$GOOD_YB_HOME/bin/yb-log-parse"
FAKE_HOME="$TMP_ROOT/fake-home"
mkdir -p "$FAKE_HOME/bin"
cp "$GOOD_YB_HOME/bin/yb-log-parse" "$FAKE_HOME/bin/yb-log-parse"
output="$(HOME="$FAKE_HOME" bash "$SB_UTILS" --yb_home_dir "$GOOD_YB_HOME" \
  prefer_yb_log_parse_bin 2>&1)"
check_contains "log-parse: the helper under --yb_home_dir is used" "$output" \
  "Using yb-log-parse helper: $GOOD_YB_HOME/bin/yb-log-parse"
output="$(HOME="$FAKE_HOME" bash "$SB_UTILS" prefer_yb_log_parse_bin 2>&1)"
check_contains "log-parse: a copy in \$HOME/bin is not used" "$output" \
  "not found at <no --yb_home_dir>"
output="$(bash "$SB_UTILS" --run_timeout_secs 30 --yb_home_dir "$GOOD_YB_HOME" \
  prefer_yb_log_parse_bin 2>&1)"
check_contains "log-parse: the flag still works after --run_timeout_secs" "$output" \
  "Using yb-log-parse helper: $GOOD_YB_HOME/bin/yb-log-parse"

# --- estimates without the helper ---------------------------------------------------------------
# An estimate never installs the helper, so without one it must still size only the window's logs:
# 17 + 19 bytes for UniverseLogs, 14 for YbcLogs, never the logs dated after the window.
EST_HOME="$TMP_ROOT/logparse-estimate-home"
mkdir -p "$EST_HOME/master/logs" "$EST_HOME/tserver/logs" "$EST_HOME/ybc/logs"
printf 'master in window\n' > "$EST_HOME/master/logs/$MASTER_LOG"
printf 'tserver in window!\n' > "$EST_HOME/tserver/logs/$TSERVER_LOG"
printf 'master after the window\n' \
  > "$EST_HOME/master/logs/yb-master.u-n1.yugabyte.log.INFO.20260301-120000.2"
printf 'ybc in window\n' > "$EST_HOME/ybc/logs/$YBC_LOG"
printf 'ybc after\n' > "$EST_HOME/ybc/logs/yb-controller.u-n1.log.INFO.20260301-120000.2"
YBC_REGEX='((?:.*)(?:yb-)(?:controller)(?:.*))(\d{8}-\d{6})\.(?:.*)'

estimate_size() {
  grep -o 'ESTIMATE_SIZE=[0-9]*' <<< "$1" | cut -d= -f2
}

for helper in "no" "a broken"; do
  if [ "$helper" = "no" ]; then
    helper_env="YB_LOG_PARSE_SKIP=1"
  else
    helper_env="YB_LOG_PARSE_BIN=$BROKEN_HELPER"
  fi
  output="$(env "$helper_env" bash "$SB_UTILS" estimate_universe_logs_size \
    --node_home_dir "$EST_HOME" \
    --master_logs_path "$EST_HOME/master/logs" \
    --tserver_logs_path "$EST_HOME/tserver/logs" \
    --start_date "2026-01-01" \
    --end_date "2026-01-31" \
    --remote_tar_path "$TMP_ROOT/estimate-universe.tar.gz" \
    --regex "$LOG_REGEX" 2>&1)"
  check "UniverseLogs estimate with $helper yb-log-parse sizes the window" \
    "$(estimate_size "$output")" "36"
  output="$(env "$helper_env" bash "$SB_UTILS" estimate_ybc_logs_size \
    --node_home_dir "$EST_HOME" \
    --ybc_logs_path "$EST_HOME/ybc/logs" \
    --start_date "2026-01-01" \
    --end_date "2026-01-31" \
    --remote_tar_path "$TMP_ROOT/estimate-ybc.tar.gz" \
    --regex "$YBC_REGEX" 2>&1)"
  check "YbcLogs estimate with $helper yb-log-parse sizes the window" \
    "$(estimate_size "$output")" "14"
done

# ===============================================================================================
# pg_upgrade logs
# ===============================================================================================
# Only <mount>/pg_upgrade_data*/pg_upgrade_output.d/<ts>/log/<file>, as v1; a deep walk of the mount
# would read every SST and WAL directory. The decoy under yb-data shows the scan stays at the top.

PGU_MOUNT="$TMP_ROOT/pgu-mount"
PGU_LOG1="pg_upgrade_data/pg_upgrade_output.d/20260115T120000.000/log/pg_upgrade_server.log"
PGU_LOG2="pg_upgrade_data_15/pg_upgrade_output.d/20260116T120000.000/log/pg_upgrade_internal.log"
PGU_DECOY="yb-data/tserver/data/rocksdb/pg_upgrade_data/pg_upgrade_output.d/t/log/decoy.log"
PGU_NESTED="pg_upgrade_data/pg_upgrade_output.d/20260115T120000.000/log/sub/nested.log"
PGU_LINK="pg_upgrade_data/pg_upgrade_output.d/20260115T120000.000/log/link.log"
mkdir -p "$PGU_MOUNT/yb-data/master/logs" "$PGU_MOUNT/yb-data/tserver/logs"
for rel in "$PGU_LOG1" "$PGU_LOG2" "$PGU_DECOY" "$PGU_NESTED"; do
  mkdir -p "$(dirname "$PGU_MOUNT/$rel")"
done
printf 'upgrade server\n' > "$PGU_MOUNT/$PGU_LOG1"
printf 'upgrade internal\n' > "$PGU_MOUNT/$PGU_LOG2"
printf 'decoy\n' > "$PGU_MOUNT/$PGU_DECOY"
printf 'nested\n' > "$PGU_MOUNT/$PGU_NESTED"
ln -s "$PGU_MOUNT/$PGU_DECOY" "$PGU_MOUNT/$PGU_LINK"
EMPTY_MOUNT="$TMP_ROOT/pgu-empty-mount"
mkdir -p "$EMPTY_MOUNT/yb-data/master/logs" "$EMPTY_MOUNT/yb-data/tserver/logs"

run_pg_upgrade_logs() {
  local entrypoint=$1 mount=$2 tar_path=$3
  YB_LOG_PARSE_SKIP=1 bash "$SB_UTILS" "$entrypoint" \
    --node_home_dir "$mount/yb-data" \
    --master_logs_path "$mount/yb-data/master/logs" \
    --tserver_logs_path "$mount/yb-data/tserver/logs" \
    --mount_path "$mount" \
    --start_date "2026-01-01" \
    --end_date "2026-01-31" \
    --remote_tar_path "$tar_path" \
    --regex "$LOG_REGEX" 2>&1
}

output="$(run_pg_upgrade_logs create_universe_logs_bundle "$PGU_MOUNT" "$TMP_ROOT/pgu.tar.gz")"
check_contains "pg_upgrade: collection reports OK" "$output" "BUNDLE_STATUS=OK"
pgu_entries="$(tar -tzf "$TMP_ROOT/pgu.tar.gz" 2>/dev/null | grep -v '/$' | sed 's|^\./||' \
  | LC_ALL=C sort | tr '\n' ' ')"
check "pg_upgrade: only the two logs under the mount's pg_upgrade_data* are collected" \
  "$pgu_entries" "$PGU_LOG1 $PGU_LOG2 "

output="$(run_pg_upgrade_logs estimate_universe_logs_size "$PGU_MOUNT" "$TMP_ROOT/pgu-est.tar.gz")"
check "pg_upgrade: the estimate sizes the same two logs (15 + 17 bytes)" \
  "$(estimate_size "$output")" "32"

output="$(run_pg_upgrade_logs estimate_universe_logs_size "$EMPTY_MOUNT" \
  "$TMP_ROOT/pgu-empty-est.tar.gz")"
check "pg_upgrade: a mount without pg_upgrade_data adds nothing" \
  "$(estimate_size "$output")" "0"

# ===============================================================================================
# data_dir_map
# ===============================================================================================
# One find per root and no per-file commands: the old ls/dirname/basename/stat per SST took minutes
# at a few thousand SSTs under the CPU cap. The first data dir's rocksdb is inside node_home_dir, so
# it is listed once; the second data dir's is listed as its own root.

DDM_D0="$TMP_ROOT/ddm/d0/yb-data"
DDM_D1="$TMP_ROOT/ddm/d1/yb-data"
DDM_T1="$DDM_D0/tserver/data/rocksdb/table-a/tablet-1"
DDM_T2="$DDM_D1/tserver/data/rocksdb/table-a/tablet-2"
mkdir -p "$DDM_T1" "$DDM_T1.intents" "$DDM_T2" "$DDM_D0/tserver/wals/table-a/tablet-1"
ddm_file() { head -c "$2" /dev/zero > "$1"; }
ddm_file "$DDM_T1/000001.sst" 10
ddm_file "$DDM_T1/000001.sst.sblock.0" 100
ddm_file "$DDM_T1/000002.sst" 10
ddm_file "$DDM_T1/000002.sst.sblock.0" 200
ddm_file "$DDM_T1/CURRENT" 16
ddm_file "$DDM_T1.intents/000003.sst" 5
ddm_file "$DDM_T1.intents/000003.sst.sblock.0" 50
ddm_file "$DDM_T2/000004.sst" 7
ddm_file "$DDM_T2/000004.sst.sblock.0" 70
ddm_file "$DDM_D0/tserver/wals/table-a/tablet-1/wal-000000001" 3

DDM_CALL_LOG="$TMP_ROOT/ddm-calls.log"
: > "$DDM_CALL_LOG"
ddm_stub_path=""
for cmd in stat dirname basename du ls; do
  real="$(command -v "$cmd")"
  dir="$(stub_command "$cmd" <<EOF
#!/bin/sh
echo $cmd "\$@" >> "$DDM_CALL_LOG"
exec $real "\$@"
EOF
)"
  ddm_stub_path="$dir:$ddm_stub_path"
done

run_data_dir_map() {
  local tar_path=$1
  shift
  PATH="$ddm_stub_path$PATH" bash "$SB_UTILS" collect_data_dir_map \
    --remote_tar_path "$tar_path" \
    --node_home_dir "$DDM_D0" \
    --rocksdb_scan_roots \
    "$DDM_D0/tserver/data/rocksdb,$DDM_D1/tserver/data/rocksdb,$TMP_ROOT/ddm/none" "$@" 2>&1
}

ddm_output_file() {
  local tar_path=$1 dest="$TMP_ROOT/ddm-x-$RANDOM"
  mkdir -p "$dest"
  tar -xzf "$tar_path" -C "$dest" 2>/dev/null
  find "$dest" -name 'data_dir_map_*.txt' | head -1
}

output="$(run_data_dir_map "$TMP_ROOT/ddm.tar.gz")"
check_contains "data_dir_map: reports OK" "$output" "BUNDLE_STATUS=OK"
ddm_txt="$(ddm_output_file "$TMP_ROOT/ddm.tar.gz")"
check "data_dir_map: a first-data-dir SST is listed once" \
  "$(grep -c " $DDM_T1/000001.sst.sblock.0\$" "$ddm_txt")" "1"
check "data_dir_map: a second-data-dir SST is listed once" \
  "$(grep -c " $DDM_T2/000004.sst\$" "$ddm_txt")" "1"
check "data_dir_map: the first data dir's rocksdb gets no listing of its own" \
  "$(grep -c "^===== Directory map: $DDM_D0/tserver/data/rocksdb =====" "$ddm_txt")" "0"
check "data_dir_map: a listing line carries the size in bytes" \
  "$(awk -v p="$DDM_T1/000001.sst.sblock.0" '$NF == p { print $6 }' "$ddm_txt")" "100"
check "data_dir_map: first data dir inventory totals" \
  "$(sed -n "/^===== SST inventory: ${DDM_D0//\//\\/}/,/^\$/p" "$ddm_txt" | grep '^total_' |
    tr '\n' ' ')" "total_sst_files=3 total_sst_bytes=375 "
check_contains "data_dir_map: per-tablet count and bytes, data blocks included" \
  "$(cat "$ddm_txt")" "table-a/tablet-1 sst_count=2 sst_bytes=320"
check_contains "data_dir_map: intents are their own directory" \
  "$(cat "$ddm_txt")" "table-a/tablet-1.intents sst_count=1 sst_bytes=55"
check_contains "data_dir_map: second data dir inventory" \
  "$(cat "$ddm_txt")" "table-a/tablet-2 sst_count=1 sst_bytes=77"
check "data_dir_map: no stat, dirname, basename, du or ls on anything under rocksdb" \
  "$(grep -c '/data/rocksdb' "$DDM_CALL_LOG")" "0"

output="$(run_data_dir_map "$TMP_ROOT/ddm-cap.tar.gz" --max_listing_lines 3)"
ddm_txt="$(ddm_output_file "$TMP_ROOT/ddm-cap.tar.gz")"
check "data_dir_map: the node_home_dir listing stops at the cap" \
  "$(sed -n "/^===== Directory map: ${DDM_D0//\//\\/} =====/,/^\$/p" "$ddm_txt" |
    grep -c '^[0-9]')" "3"
check "data_dir_map: a cut listing says so" \
  "$(grep -c '^(truncated at 3 lines)$' "$ddm_txt")" "2"
check_contains "data_dir_map: the inventory is complete under the cap" \
  "$(cat "$ddm_txt")" "total_sst_files=3"

# The walk itself stops at the cap: find is ended by SIGPIPE (rc 141). The listing is well past the
# 64 KiB pipe buffer, so find cannot finish before the reader exits.
DDM_BIG="$TMP_ROOT/ddm-big/yb-data/many"
mkdir -p "$DDM_BIG"
(cd "$DDM_BIG" && touch file-with-a-long-name-to-fill-the-pipe-{0001..1500})
DDM_FIND_LOG="$TMP_ROOT/ddm-find.log"
DDM_FIND_STUB="$(stub_command find <<EOF
#!/bin/bash
$(command -v find) "\$@"
echo "rc=\$?" >> "$DDM_FIND_LOG"
EOF
)"
: > "$DDM_FIND_LOG"
PATH="$DDM_FIND_STUB:$PATH" bash "$SB_UTILS" collect_data_dir_map \
  --remote_tar_path "$TMP_ROOT/ddm-big.tar.gz" --node_home_dir "$TMP_ROOT/ddm-big/yb-data" \
  --max_listing_lines 3 > /dev/null 2>&1
check "data_dir_map: the capped walk is stopped, not just its output" \
  "$(head -1 "$DDM_FIND_LOG")" "rc=141"

output="$(run_data_dir_map "$TMP_ROOT/ddm-bad.tar.gz" --max_listing_lines 0)"
check_contains "data_dir_map: a zero cap is refused" "$output" "BUNDLE_STATUS=FAILED"

# ===============================================================================================
# top/ps/pmap process selection
# ===============================================================================================
# By process name: a tail of a log or a shell naming /yb-tserver or /postgres must not get a pmap.
# Backends keep the process name postgres, so the postmaster is taken as the tserver's own child.
# Real processes, not stubs: a script's process name is its file name.

PS_FAKE="$TMP_ROOT/psfake"
PS_LOGS="$TMP_ROOT/pslogs"
mkdir -p "$PS_FAKE/backend" "$PS_FAKE/other" "$PS_LOGS"
cp "$(command -v sleep)" "$PS_FAKE/backend/postgres"
cp "$(command -v sleep)" "$PS_FAKE/other/postgres"
cat > "$PS_FAKE/postgres" <<EOF
#!/bin/bash
(exec -a "postgres: yugabyte yugabyte idle" "$PS_FAKE/backend/postgres" 60) &
wait
EOF
cat > "$PS_FAKE/yb-tserver" <<EOF
#!/bin/bash
"$PS_FAKE/postgres" &
wait
EOF
chmod +x "$PS_FAKE/postgres" "$PS_FAKE/yb-tserver"
for log in yb-tserver.INFO yb-master.INFO postgresql-1.log; do : > "$PS_LOGS/$log"; done

# The stray starts first, so a pick of the oldest postgres would get it.
"$PS_FAKE/other/postgres" 60 &
ps_stray=$!
sleep 0.2
"$PS_FAKE/yb-tserver" &
ps_tserver=$!
ps_decoys=""
for log in yb-tserver.INFO yb-master.INFO postgresql-1.log; do
  tail -f "$PS_LOGS/$log" > /dev/null 2>&1 &
  ps_decoys="$ps_decoys $!"
done
ps_postmaster=""
ps_backend=""
for _ in $(seq 50); do
  ps_postmaster=$(pgrep -P "$ps_tserver" || true)
  [ -n "$ps_postmaster" ] && ps_backend=$(pgrep -P "$ps_postmaster" || true)
  [ -n "$ps_backend" ] && break
  sleep 0.1
done

output="$(bash "$SB_UTILS" collect_top_ps_pmap_stats \
  --remote_tar_path "$TMP_ROOT/ps.tar.gz" 2>&1)"
check_contains "ps/pmap: reports OK" "$output" "BUNDLE_STATUS=OK"
ps_dir="$TMP_ROOT/ps-x"
mkdir -p "$ps_dir"
tar -xzf "$TMP_ROOT/ps.tar.gz" -C "$ps_dir" 2>/dev/null
ps_txt="$(find "$ps_dir" -name 'top_ps_pmap_stats_*.txt' | head -1)"

# The pids listed under one section, as "--- pid=N ---" headers.
ps_section_pids() {
  awk -v want="===== $1 =====" '
    /^===== / { in_section = ($0 == want); next }
    in_section && /^--- pid=/ { gsub(/[^0-9]/, ""); print }' "$ps_txt" | tr '\n' ' '
}

check "ps/pmap: the tserver section is the tserver" \
  "$(ps_section_pids yb-tserver | grep -cw "$ps_tserver")" "1"
check "ps/pmap: the postgres section is only the postmaster" \
  "$(ps_section_pids postgres)" "$ps_postmaster "
check "ps/pmap: the postmaster gets a pmap -x" \
  "$(grep -A8 -- "--- pid=$ps_postmaster ---" "$ps_txt" | grep -c '^Address  *Kbytes  *RSS')" "1"
for pid in $ps_backend $ps_stray $ps_decoys; do
  check "ps/pmap: pid $pid (backend, stray postgres or log tail) gets no section" \
    "$(grep -c -- "--- pid=$pid ---" "$ps_txt")" "0"
done
kill $ps_backend $ps_postmaster $ps_tserver $ps_stray $ps_decoys 2>/dev/null
wait 2>/dev/null

# ===============================================================================================
# Staging location, copy failures, cores tarred in place
# ===============================================================================================
# YBA passes the bundle's own directory on the node as --tmp_dir and removes it when the bundle
# ends, so the work of a run killed with SIGKILL (no EXIT trap) is left there, not loose in /tmp.
# A file that fails to copy is reported and left out; the rest is collected. Cores aren't copied.

ST_HOME="$TMP_ROOT/st-home"
mkdir -p "$ST_HOME/tserver"
printf 'tserver out\n' > "$ST_HOME/tserver/tserver.out"
printf 'bad out\n' > "$ST_HOME/tserver/bad.out"
ST_BUNDLE_DIR="$TMP_ROOT/st-bundle"
mkdir -p "$ST_BUNDLE_DIR"
ST_TAR_LOG="$TMP_ROOT/st-tar.log"
ST_STUBS="$TMP_ROOT/st-stubs"
mkdir -p "$ST_STUBS/tar" "$ST_STUBS/cp-fail" "$ST_STUBS/cp-log"
# Records the staging dir it was asked to tar, then hangs when ST_TAR_HANG is set.
cat > "$ST_STUBS/tar/tar" <<EOF
#!/bin/bash
while [ \$# -gt 0 ]; do
  if [ "\$1" = "-C" ]; then echo "\$2" >> "$ST_TAR_LOG"; fi
  shift
done
if [ -n "\${ST_TAR_HANG:-}" ]; then echo "\$\$ \$PPID" > "$TMP_ROOT/st-tar.pid"; exec sleep 30; fi
EOF
# Fails on bad.out after writing part of it, as a read error would.
cat > "$ST_STUBS/cp-fail/cp" <<EOF
#!/bin/bash
for arg in "\$@"; do
  if [[ "\$arg" == *bad.out ]]; then
    printf 'partial' > "\${@: -1}"
    echo "cp: error reading '\$arg': Input/output error" >&2
    exit 1
  fi
done
exec $(command -v cp) "\$@"
EOF
cat > "$ST_STUBS/cp-log/cp" <<EOF
#!/bin/bash
echo "\$*" >> "$TMP_ROOT/st-cp.log"
exec $(command -v cp) "\$@"
EOF
chmod +x "$ST_STUBS/tar/tar" "$ST_STUBS/cp-fail/cp" "$ST_STUBS/cp-log/cp"

run_output_files_in() {
  local tmp_dir=$1 tar_path=$2
  shift 2
  bash "$SB_UTILS" --tmp_dir "$tmp_dir" create_output_files_bundle \
    --node_home_dir "$ST_HOME" --remote_tar_path "$tar_path" "$@" 2>&1
}

: > "$ST_TAR_LOG"
ST_TAR_HANG=1 PATH="$ST_STUBS/tar:$PATH" run_output_files_in "$ST_BUNDLE_DIR" \
  "$ST_BUNDLE_DIR/output-files.tar.gz" --output_file tserver/tserver.out > /dev/null &
st_pid=$!
for _ in $(seq 50); do [ -s "$TMP_ROOT/st-tar.pid" ] && break; sleep 0.1; done
# The stub's parent is support_bundle_utils.sh itself; the background job is a subshell above it.
read -r st_tar_pid st_script_pid < "$TMP_ROOT/st-tar.pid"
kill -9 "$st_script_pid" 2>/dev/null
kill "$st_tar_pid" 2>/dev/null
wait "$st_pid" 2>/dev/null
st_staged="$(head -1 "$ST_TAR_LOG")"
check "staging: the work directory is under --tmp_dir" \
  "$([[ "$st_staged" == "$ST_BUNDLE_DIR/"* ]] && echo inside || echo "outside: $st_staged")" \
  "inside"
check "staging: a SIGKILLed run leaves its work under --tmp_dir, for YBA's cleanup" \
  "$([ -d "$st_staged" ] && echo left || echo gone)" "left"

: > "$ST_TAR_LOG"
PATH="$ST_STUBS/tar:$PATH" run_output_files_in "$TMP_ROOT/no-such-dir" \
  "$TMP_ROOT/st-fallback.tar.gz" --output_file tserver/tserver.out > /dev/null
st_staged="$(head -1 "$ST_TAR_LOG")"
check "staging: an unusable --tmp_dir leaves the default temp directory in use" \
  "$([[ "$st_staged" == "${TMPDIR:-/tmp}/output-files."* ]] && echo default || echo "$st_staged")" \
  "default"

output="$(PATH="$ST_STUBS/cp-fail:$PATH" run_output_files_in "$ST_BUNDLE_DIR" \
  "$TMP_ROOT/st-copy.tar.gz" --output_file tserver/tserver.out --output_file tserver/bad.out)"
check_contains "copy failure: reported with the file and the error" "$output" \
  "COPY_FAILURE=$ST_HOME/tserver/bad.out: cp: error reading"
check_contains "copy failure: the rest is still collected" "$output" "BUNDLE_STATUS=OK"
check "copy failure: the partial copy is not in the tar" \
  "$(tar -tzf "$TMP_ROOT/st-copy.tar.gz" | grep -v '/$' | sed 's|^\./||' | sort | tr '\n' ' ')" \
  "output_files/tserver.out "

CORES_HOME="$TMP_ROOT/cores-home"
mkdir -p "$CORES_HOME/cores/sub"
printf 'core one\n' > "$CORES_HOME/cores/core.1"
printf 'core two\n' > "$CORES_HOME/cores/sub/core.2"
touch -d "2026-01-15 12:00" "$CORES_HOME/cores/core.1" "$CORES_HOME/cores/sub/core.2"
: > "$TMP_ROOT/st-cp.log"
output="$(PATH="$ST_STUBS/cp-log:$PATH" bash "$SB_UTILS" create_core_files_bundle \
  --node_home_dir "$CORES_HOME" --start_date 2026-01-01 --end_date 2026-01-31 \
  --remote_tar_path "$TMP_ROOT/cores.tar.gz" --max_num_recent_cores 2 2>&1)"
check_contains "cores: collected" "$output" "BUNDLE_STATUS=OK"
check "cores: the tar holds cores/<file name>, as before" \
  "$(tar -tzf "$TMP_ROOT/cores.tar.gz" | LC_ALL=C sort | tr '\n' ' ')" "cores/core.1 cores/core.2 "
check "cores: content intact" \
  "$(tar -xzOf "$TMP_ROOT/cores.tar.gz" cores/core.2)" "core two"
check "cores: tarred where they are, never copied" "$(wc -l < "$TMP_ROOT/st-cp.log")" "0"

# ===============================================================================================
# pg audit filtering
# ===============================================================================================
# The filter is the only thing keeping AUDIT rows out of a bundle collected with pg audit filtering
# on, so what it does when it cannot run decides whether the operator's request is honoured or
# silently inverted. The v1 collector loses the file in that case; this pins v2 to the same posture.

AUDIT_LINE='2026-09-19 10:00:00 UTC [123] LOG:  AUDIT: SESSION,1,1,READ,SELECT,,,SELECT * FROM t;'
PLAIN_LINE='2026-09-19 10:00:01 UTC [123] LOG:  connection authorized: user=yugabyte'

# A failing awk models the realistic triggers: awk absent from a minimal image, or OOM-killed on a
# large log. The redirect has already created the target, so the source is not what fails.
AWK_STUB_DIR="$(stub_command awk <<'STUB'
#!/bin/sh
exit 1
STUB
)"

assert_no_audit() {
  local name=$1 target=$2
  if [ ! -f "$target" ]; then
    pass "$name (file omitted)"
    return
  fi
  if grep -q "AUDIT:" "$target"; then
    fail "$name" "audit rows present in $(basename "$target")" "$(cat "$target")"
    return
  fi
  pass "$name (filtered)"
}

# --- the filter works when awk works ------------------------------------------------------------
PG_SRC="$TMP_ROOT/postgresql-2026-09-19.log"
printf '%s\n%s\n' "$PLAIN_LINE" "$AUDIT_LINE" > "$PG_SRC"
bash "$SB_UTILS" filter_pg_audit_log_file \
  "$PG_SRC" "$TMP_ROOT/out/filtered_ok.log" > /dev/null 2>&1
assert_no_audit "audit rows removed on the normal path" "$TMP_ROOT/out/filtered_ok.log"
if ! grep -q "connection authorized" "$TMP_ROOT/out/filtered_ok.log" 2>/dev/null; then
  fail "normal path dropped the non-audit lines too"
else
  pass "non-audit lines are kept"
fi

# --- the filter cannot run ----------------------------------------------------------------------
out="$(PATH="$AWK_STUB_DIR:$PATH" bash "$SB_UTILS" \
  filter_pg_audit_log_file "$PG_SRC" "$TMP_ROOT/out/filtered_broken.log" 2>&1)"
assert_no_audit "audit rows never ship when the filter fails" "$TMP_ROOT/out/filtered_broken.log"

if ! grep -qi "warn" <<< "$out"; then
  fail "a dropped log file must be reported, got: ${out:-<no output>}"
else
  pass "the dropped file is reported"
fi

# ===============================================================================================
# System logs window
# ===============================================================================================
# A day-granularity bound must resolve to the edge of its day, not to midnight twice.
#
# Nothing YBA ships reaches this today: it sends --end_epoch alongside --end_date, and the shell
# only derives an epoch when the epoch is missing. The path exists for callers that send dates
# only, and the contract is that an end bound covers its whole day -- otherwise a rotation that
# closed later that same day is dropped, which is the v1 behaviour this was written to match.

DAY="2026-09-19"
MIDNIGHT="$(date -u -d "$DAY 00:00:00" +%s)"
END_OF_DAY="$(date -u -d "$DAY 23:59:59" +%s)"
INSTANT="$(date -u -d "${DAY}T20:30:00Z" +%s)"

check_epoch() {
  local name=$1 raw=$2 edge=$3 want=$4 got
  got=$(bash "$SB_UTILS" _system_logs_epoch_from_date "$raw" "$edge" 2>/dev/null)
  check "$name" "$got" "$want"
}

# The whole point of the edge argument: the same input must resolve differently at each end.
check_epoch "a day's start bound is its midnight"      "$DAY" start "$MIDNIGHT"
check_epoch "a day's end bound is its last second"     "$DAY" end   "$END_OF_DAY"

if [ "$MIDNIGHT" = "$END_OF_DAY" ]; then
  fail "the two edges cannot be distinguished"
else
  pass "the two edges differ by $(( END_OF_DAY - MIDNIGHT )) seconds"
fi

# A bound that already carries a time must be taken as given, at either edge.
check_epoch "an instant is unchanged at the end edge"   "${DAY}T20:30:00Z" end   "$INSTANT"
check_epoch "an instant is unchanged at the start edge" "${DAY}T20:30:00Z" start "$INSTANT"

if bash "$SB_UTILS" _system_logs_epoch_from_date "not-a-date" end >/dev/null 2>&1; then
  fail "an unparseable bound should fail rather than invent a window"
else
  pass "an unparseable bound fails"
fi

# ===============================================================================================
# cdc_state failure reasons
# ===============================================================================================
# When a command exits non-zero, YBA shows only its stderr (NodeAgentClient.getResponse,
# ShellProcessHandler), so collect_cdc_state's reasons for failing must be on stderr. On stdout
# they are dropped and YBA logs the failure with an empty "Output:".

CDC_HOME="$TMP_ROOT/cdc-home"
mkdir -p "$CDC_HOME"

# Runs collect_cdc_state with the given helper, capturing stdout and stderr apart into CDC_STDOUT
# and CDC_STDERR. Extra arguments are appended after the required ones.
run_collect_cdc_state() {
  local helper=$1
  shift
  YB_DUMP_CDC_STATE_BIN="$helper" TMPDIR="$TMP_ROOT" bash "$SB_UTILS" collect_cdc_state \
    --remote_tar_path "$TMP_ROOT/cdc-state.tar.gz" --host_ip 127.0.0.1 \
    --tserver_rpc_port 9100 --yb_home_dir "$CDC_HOME" "$@" \
    >"$TMP_ROOT/cdc.stdout" 2>"$TMP_ROOT/cdc.stderr"
  CDC_STDOUT=$(cat "$TMP_ROOT/cdc.stdout")
  CDC_STDERR=$(cat "$TMP_ROOT/cdc.stderr")
}

# Passes the smoke check, then fails the way a real run can.
FAILING_HELPER="$(stub_command yb-dump-cdc-state <<'EOF'
#!/bin/bash
if [ "$1" = smoke ]; then printf ok; exit 0; fi
echo "boom: cannot read cdc_state" >&2
exit 1
EOF
)/yb-dump-cdc-state"

# --- helper absent -----------------------------------------------------------------------------
run_collect_cdc_state "$TMP_ROOT/no-such-helper"
check_contains "missing helper: the reason is on stderr" \
  "$CDC_STDERR" "helper is required but was not found"
check_contains "missing helper: stdout keeps the marker" "$CDC_STDOUT" "BUNDLE_STATUS=FAILED"

# --- helper location ---------------------------------------------------------------------------
# YBA installs it under the --yb_home_dir it passes; a copy under the run user's home doesn't count.
CDC_HOME_HELPER_LOG="$TMP_ROOT/cdc-home-helper.log"
mkdir -p "$TMP_ROOT/cdc-fake-home/bin"
# shellcheck disable=SC2016 # expanded by the stub's own shell
printf '#!/bin/bash\nif [ "$1" = smoke ]; then printf ok; exit 0; fi\necho ran >> "%s"\nexit 1\n' \
  "$CDC_HOME_HELPER_LOG" > "$TMP_ROOT/cdc-fake-home/bin/yb-dump-cdc-state"
chmod +x "$TMP_ROOT/cdc-fake-home/bin/yb-dump-cdc-state"
HOME="$TMP_ROOT/cdc-fake-home" run_collect_cdc_state ""
check_contains "helper location: a copy in \$HOME/bin is not used" "$CDC_STDERR" \
  "not found at $CDC_HOME/bin/yb-dump-cdc-state."
check "helper location: the \$HOME copy never ran" "$(cat "$CDC_HOME_HELPER_LOG" 2>/dev/null)" ""
mkdir -p "$CDC_HOME/bin"
cp "$TMP_ROOT/cdc-fake-home/bin/yb-dump-cdc-state" "$CDC_HOME/bin/yb-dump-cdc-state"
run_collect_cdc_state ""
check "helper location: the copy under --yb_home_dir runs" "$(cat "$CDC_HOME_HELPER_LOG")" "ran"
rm -rf "${CDC_HOME:?}/bin"
# The leading --yb_home_dir every YBA run carries wins over the entrypoint's own.
rm -f "$CDC_HOME_HELPER_LOG"
YB_DUMP_CDC_STATE_BIN="" TMPDIR="$TMP_ROOT" bash "$SB_UTILS" \
  --yb_home_dir "$TMP_ROOT/cdc-fake-home" collect_cdc_state \
  --remote_tar_path "$TMP_ROOT/cdc-state.tar.gz" --host_ip 127.0.0.1 \
  --tserver_rpc_port 9100 --yb_home_dir "$CDC_HOME" >/dev/null 2>&1
check "helper location: the leading --yb_home_dir is used" "$(cat "$CDC_HOME_HELPER_LOG")" "ran"

# --- helper fails ------------------------------------------------------------------------------
run_collect_cdc_state "$FAILING_HELPER"
check_contains "failing helper: its own error is on stderr" \
  "$CDC_STDERR" "boom: cannot read cdc_state"
check_contains "failing helper: the script's reason is on stderr" \
  "$CDC_STDERR" "yb-dump-cdc-state failed with exit 1"
check_contains "failing helper: stdout keeps the marker" "$CDC_STDOUT" "BUNDLE_STATUS=FAILED"

# --- TLS certs ---------------------------------------------------------------------------------
# YBA passes --certs_dir_name with node-to-node TLS; the helper gets it only if the dir exists.
CDC_ARGS_LOG="$TMP_ROOT/cdc-args.log"
RECORDING_HELPER="$(stub_command yb-dump-cdc-state <<EOF
#!/bin/bash
if [ "\$1" = smoke ]; then printf ok; exit 0; fi
echo "\$*" > "$CDC_ARGS_LOG"
exit 1
EOF
)/yb-dump-cdc-state"
CERTS_DIR="$TMP_ROOT/yugabyte-tls-config"
mkdir -p "$CERTS_DIR"
run_collect_cdc_state "$RECORDING_HELPER" --certs_dir_name "$CERTS_DIR"
check_contains "certs: passed to the helper" "$(cat "$CDC_ARGS_LOG")" "--certs_dir_name $CERTS_DIR"
run_collect_cdc_state "$RECORDING_HELPER" --certs_dir_name "$TMP_ROOT/no-such-certs"
check "certs: a missing directory is not passed" \
  "$(grep -c -- --certs_dir_name "$CDC_ARGS_LOG")" "0"
check_contains "certs: the missing directory is reported" "$CDC_STDERR" "not found"
run_collect_cdc_state "$RECORDING_HELPER"
check "certs: none without the flag" "$(grep -c -- --certs_dir_name "$CDC_ARGS_LOG")" "0"

# --- bad argument ------------------------------------------------------------------------------
run_collect_cdc_state "$FAILING_HELPER" --bogus x
check_contains "unknown argument: the reason is on stderr" "$CDC_STDERR" "Unknown argument: --bogus"

# --- dispatch guard -----------------------------------------------------------------------------
# The script runs its first argument, so anything but a function defined in it must be refused.
guard_out=$(bash "$SB_UTILS" bash -c 'echo ran' 2>&1)
guard_rc=$?
check "dispatch: a non-function first argument exits 2" "$guard_rc" "2"
check_contains "dispatch: the refusal names it" "$guard_out" "Unknown function: bash"
if grep -q "^ran$" <<< "$guard_out"; then
  fail "dispatch: the non-function was run" "$guard_out"
else
  pass "dispatch: the non-function is not run"
fi
# An option-like argument must not be read as an option to declare and slip through.
for opt in -f -p; do
  opt_out=$(bash "$SB_UTILS" "$opt" 2>&1)
  opt_rc=$?
  check "dispatch: '$opt' as the first argument exits 2" "$opt_rc" "2"
  check_contains "dispatch: the '$opt' refusal names it" "$opt_out" "Unknown function: $opt"
done
noargs_out=$(bash "$SB_UTILS" 2>&1)
check "dispatch: no arguments is a no-op" "$?:$noargs_out" "0:"

# --- run timeout --------------------------------------------------------------------------------
# YBA passes --run_timeout_secs when the command doesn't run under yb-sb-limits-exec.sh.
TIMEOUT_LOG="$TMP_ROOT/timeout.log"
RECORDING_TIMEOUT="$(stub_command timeout <<EOF
#!/bin/bash
echo "\$*" >> "$TIMEOUT_LOG"
shift 3
exec "\$@"
EOF
)"
rt_out=$(env PATH="$RECORDING_TIMEOUT:$PATH" bash "$SB_UTILS" --run_timeout_secs 330 \
  decode_base64_string MQ== 2>&1)
check "run timeout: the entrypoint still runs" "$rt_out" "1"
check "run timeout: re-run under timeout -k 5" "$(tail -1 "$TIMEOUT_LOG")" \
  "-k 5 330 bash $SB_UTILS decode_base64_string MQ=="

BROKEN_TIMEOUT_DIR="$TMP_ROOT/stubs/broken-timeout"
mkdir -p "$BROKEN_TIMEOUT_DIR"
printf '#!/bin/sh\nexit 125\n' > "$BROKEN_TIMEOUT_DIR/timeout"
chmod +x "$BROKEN_TIMEOUT_DIR/timeout"
rt_out=$(env PATH="$BROKEN_TIMEOUT_DIR:$PATH" bash "$SB_UTILS" --run_timeout_secs 330 \
  decode_base64_string MQ== 2>&1)
check "run timeout: runs as before without a usable timeout" "$rt_out" "1"

rm -f "$TIMEOUT_LOG"
rt_out=$(env PATH="$RECORDING_TIMEOUT:$PATH" bash "$SB_UTILS" --run_timeout_secs 0 \
  decode_base64_string MQ== 2>&1)
check "run timeout: 0 is not a bound" "$rt_out:$(cat "$TIMEOUT_LOG" 2>/dev/null)" "1:"

rt_out=$(env PATH="$RECORDING_TIMEOUT:$PATH" bash "$SB_UTILS" --run_timeout_secs 5 \
  bash -c 'echo ran' 2>&1)
check "run timeout: the dispatch guard still applies" "$?" "2"
check_contains "run timeout: the refusal names it" "$rt_out" "Unknown function: bash"

# --- helpers copied from node_utils.sh ----------------------------------------------------------
# support_bundle_utils.sh keeps its own copy of these two, since node_utils.sh isn't staged with it.
helper_defs() {
  bash -c 'f=$1; shift; fns=("$@"); set --; source "$f" > /dev/null; declare -f "${fns[@]}"' \
    _ "$1" get_paths_and_sizes get_paths_and_sizes_within_dates
}
sb_helpers="$(helper_defs "$SB_UTILS")"
check_contains "copied helpers: defined" "$sb_helpers" "get_paths_and_sizes_within_dates ()"
check "copied helpers: same as node_utils.sh" "$sb_helpers" \
  "$(helper_defs "$DEVOPS_BIN/node_utils.sh")"

finish "support_bundle_utils"
