#!/usr/bin/env bash
#
# Copyright 2020 YugabyteDB, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

set -euo pipefail

find_python_executable() {
  PYTHON_EXECUTABLES=('python3' 'python3.6' \
    'python3.7' 'python3.8' 'python3.9' 'python' 'python2' 'python2.7')
  for py_executable in "${PYTHON_EXECUTABLES[@]}"; do
    if which "$py_executable" > /dev/null 2>&1; then
      PYTHON_EXECUTABLE="$py_executable"
      return
    fi
  done

  echo "Failed to find python executable."
  exit 1
}

SCRIPT_NAME=$(basename "$0")
USER=$(whoami)
PLATFORM_DUMP_FNAME="platform_dump.sql"
PLATFORM_DUMP_K8S_DEFERRED_PATH="/opt/yugabyte/yugaware/data/restore_platform_dump_yba.sql"
# This will be needed for PA Collector support on K8S. Not used right now.
PA_DUMP_K8S_DEFERRED_PATH="/opt/yugabyte/perf-advisor/data/restore_pa_dump.sql"
PLATFORM_DB_NAME="yugaware"
PA_DUMP_FNAME="pa_ts_dump.sql"
# Marker file included in the tar for --include_pa_config_only backups. Presence of this file on
# restore switches PA restore to the data-only, whitelisted-tables code path.
INCLUDE_PA_CONFIG_ONLY_MARKER_FNAME="pa_ts_config_only.marker"
PA_DATA_DIR="perf-advisor"
PA_DB_NAME="ts"
# Whitelisted PA "configuration" tables for --include_pa_config_only mode. All other PA tables
# (metrics, anomalies, background_task, etc.) are excluded to keep the standby PA's own scraped
# state intact and avoid dragging metric bulk into every HA sync.
#   - customer_metadata: registered customers, including their collection_enabled flag.
#   - universe_metadata: registered universes and their auth_details JSON blob (per-universe
#     DB credentials PA uses to talk to the universe). This is the sensitive table on the
#     PA side - see restore_include_pa_config_only_backup for the log-safety measures.
#   - universe_details: JSON per-universe topology (clusters, nodes, software version). Not
#     itself sensitive, but needed on the standby so the newly promoted PA can start
#     collecting the rest of the per-universe metadata (query metadata, tablet metadata,
#     etc.) right away instead of waiting for its own UniverseDetailsQuery to run first.
#   - node_metadata: per-node identity (customer/universe/name/cluster/master/tserver). PA
#     joins against this for every node-scoped filter in the UI, and it's only refreshed
#     by the UniverseDetails scrape - so without syncing, all node filters stay empty on
#     the promoted PA until the next scrape lands.
#   - runtime_config_entry: scope-keyed runtime config values.
#   - users / user_auth_token: PA UI auth. Kept in sync so operators don't have to re-login
#     against the newly promoted PA.
#   - scheduled_tasks: task-runner schedule. Copying this makes the newly promoted PA pick
#     up in-flight schedules immediately; picked / picked_by / last_heartbeat get reset in
#     restore_include_pa_config_only_backup so the promoted PA can claim tasks right away
#     rather than waiting for the ex-active's heartbeats to time out.
PA_CONFIG_TABLES=(customer_metadata universe_metadata universe_details node_metadata \
                  runtime_config_entry users user_auth_token scheduled_tasks)
PROMETHEUS_SNAPSHOT_DIR="prometheus_snapshot"
# Prefix of the per-invocation staging directory created under the data directory. Anything
# an invocation has to materialise before archiving it goes there, so two overlapping
# invocations cannot overwrite or delete each other's files. Also used to keep one
# invocation's find sweep out of another's staging directory.
BACKUP_STAGING_PREFIX=".yb_platform_backup_staging_"
MIGRATION_BACKUP_DIR="migration_backup"
VERSION_METADATA="version_metadata.json"
VERSION_METADATA_BACKUP="version_metadata_backup.json"
PYTHON_EXECUTABLE=""
find_python_executable
# This is the UID for nobody user which is used by the prometheus container as the default user.
NOBODY_UID=65534
# When false, we won't stop/start platform and prometheus services when executing the script
RESTART_PROCESSES=true
# When true, we will ignore the pgrestore_path and use pg_restore found on the system
USE_SYSTEM_PG=false
K8S_BACKUP_DIR="/opt/yugabyte"

set +e
# Check whether the script is being run from a VM running replicated-based Yugabyte Platform.
docker ps -a 2> /dev/null | grep yugabyte-yugaware > /dev/null 2>&1
DOCKER_CHECK="$?"

if [[ $DOCKER_CHECK -eq 0 ]]; then
  DOCKER_BASED=true
else
  DOCKER_BASED=false
fi


# Check whether the script is being run from within a Yugabyte Platform docker container.
grep -E 'kubepods|docker' /proc/1/cgroup > /dev/null 2>&1
CONTAINER_CHECK="$?"

if [[ $CONTAINER_CHECK -eq 0 ]] && [[ "$DOCKER_BASED" = false ]]; then
  INSIDE_CONTAINER=true
else
  INSIDE_CONTAINER=false
fi
set -e

# Assume the script is being run from a systemctl-based Yugabyte Platform installation otherwise.
set +u # Allow checking undefined variables, mostly for the global env kubernetes_service_host
if [[ "$DOCKER_BASED" = false ]] && [[ "$INSIDE_CONTAINER" = false ]] && [[ "$KUBERNETES_SERVICE_HOST" = "" ]]; then
  SERVICE_BASED=true
else
  SERVICE_BASED=false
fi
set -u # Disallow undefined variables

# Takes docker container and command as arguments. Executes docker cmd if docker-based or not.
docker_aware_cmd() {
  if [[ "$DOCKER_BASED" = false ]]; then
    sh -c "$2"
  else
    docker exec -i "${1}" sh -c "$2"
  fi
}

# Check whether the script is being run from a Kubernetes pod of Yugabyte Platform
INSIDE_K8S_POD=false
if env | grep -q 'KUBERNETES_SERVICE_HOST' && [[ "$DOCKER_BASED" = false ]]; then
  INSIDE_K8S_POD=true
fi

run_sudo_cmd() {
  if sudo -n true 2>/dev/null; then
    sudo $1
  else
    $1
  fi
}

# Replace prometheus config with provided job config
run_prom_reload() {
  new_job_config="$1"
  prometheus_config="$2"
  prometheus_host="$3"
  prometheus_port="$4"
  prometheus_protocol="$5"
  # Only for K8s currently
  if [[ "$INSIDE_K8S_POD" = true ]] && [[ -f "${new_job_config}" ]]; then
    run_sudo_cmd "cp ${new_job_config} ${prometheus_config}"
    prom_reload_cmd="curl -k -X POST \
      ${prometheus_protocol}://${prometheus_host}:${prometheus_port}/-/reload"
    if [[ -n "${PROMETHEUS_USERNAME:-}" ]] && [[ -n "${PROMETHEUS_PASSWORD:-}" ]]; then
      prom_reload_cmd="${prom_reload_cmd} -u ${PROMETHEUS_USERNAME}:${PROMETHEUS_PASSWORD}"
    fi
    run_sudo_cmd "$prom_reload_cmd"
  fi
}

# Wait for prometheus to be up
wait_for_prom() {
  prometheus_host="$1"
  prometheus_port="$2"
  prometheus_protocol="$3"

  timeout=180 # Wait for 180 seconds before timing out
  start_time=$(date +%s)
  while true; do
    http_status=$(curl -k -s -o /dev/null -w "%{http_code}" \
      "${prometheus_protocol}://${prometheus_host}:${prometheus_port}/-/ready" || echo "000")
    if [[ "${http_status}" != 200 ]]; then
      if (( $(date +%s) - start_time > timeout )); then
        echo "Prometheus not ready after several retries"
        exit 1
      else
        echo "Waiting for Prometheus to be up..."
        sleep 10
      fi
    else
      break
    fi
  done
}

# Query prometheus for it's data directory and set as env var
set_prometheus_data_dir() {
  prometheus_host="$1"
  prometheus_port="$2"
  data_dir="$3"
  prometheus_protocol="$4"
  if [[ "$DOCKER_BASED" = true ]]; then
    PROMETHEUS_DATA_DIR="${data_dir}/prometheusv2"
  else
    curl_cmd="curl -k \
      ${prometheus_protocol}://${prometheus_host}:${prometheus_port}/api/v1/status/flags"
    if [[ -n "${PROMETHEUS_USERNAME:-}" ]] && [[ -n "${PROMETHEUS_PASSWORD:-}" ]]; then
      curl_cmd="${curl_cmd} -u ${PROMETHEUS_USERNAME}:${PROMETHEUS_PASSWORD}"
    fi
    PROMETHEUS_DATA_DIR=$($curl_cmd | ${PYTHON_EXECUTABLE} -c \
      "import sys, json; print(json.load(sys.stdin)['data']['storage.tsdb.path'])")
  fi
  if [[ -z "$PROMETHEUS_DATA_DIR" ]]; then
    echo "Failed to find prometheus data directory"
    exit 1
  fi
}

# Modify service status if the script is being run against a service-based Yugabyte Platform
# Name of the lock file and of the companion file naming its holder, both under the data
# directory. YBA's HA backup and yba-ctl's pre-upgrade backup are the two schedules that overlap
# in practice; yba-installer takes this same lock in Go before it runs an older copy of this
# script, so an upgrade still serializes against a backup started by YBA itself.
BACKUP_LOCK_FNAME=".yb_platform_backup.lock"
# Marks the messages below. yba-ctl buffers this script's output and logs it only once the script
# exits, so it picks these lines out and logs them as they arrive - a backup queued behind another
# one would otherwise be indistinguishable from a hang. Keep in step with backupLockLogPrefix in
# yba-installer/cmd/backup.go.
BACKUP_LOCK_LOG_PREFIX="[backup-lock] "
BACKUP_LOCK_HOLDER_FNAME=".yb_platform_backup.lock.holder"
# 0 waits forever. Overridable so a caller that would rather fail fast than queue can.
BACKUP_LOCK_WAIT_SECS="${YB_PLATFORM_BACKUP_LOCK_WAIT_SECS:-1800}"

# Describes whoever holds the lock, for the messages below. Best effort: the holder writes this
# file after it takes the lock, so it can legitimately be missing or stale.
backup_lock_holder() {
  local holder_file="$1"
  if [[ -s "${holder_file}" ]]; then
    tr -d '\n' < "${holder_file}"
  else
    echo "another process"
  fi
}

# Takes the backup lock, waiting - out loud, so a caller that appears to hang has a reason on
# screen - for whoever holds it. Skipped rather than fatal when the lock cannot be created: a
# Kubernetes or ad-hoc invocation may have no writable data directory on this host, and refusing
# to back up at all would be worse than not serializing.
acquire_backup_lock() {
  local lock_dir="$1"
  local operation="$2"
  local lock_file="${lock_dir}/${BACKUP_LOCK_FNAME}"
  local holder_file="${lock_dir}/${BACKUP_LOCK_HOLDER_FNAME}"

  if ! command -v flock > /dev/null 2>&1; then
    echo "${BACKUP_LOCK_LOG_PREFIX}WARNING: flock is not available, running ${operation}" \
         "without the backup lock." >&2
    return 0
  fi
  if ! mkdir -p "${lock_dir}" 2>/dev/null || ! ( : >> "${lock_file}" ) 2>/dev/null; then
    echo "${BACKUP_LOCK_LOG_PREFIX}WARNING: cannot write ${lock_file}, running ${operation}" \
         "without the backup lock." >&2
    return 0
  fi

  # Fd 9 stays open for the lifetime of the process, which is what holds the lock. The kernel
  # releases it when the process dies, so a killed backup leaves nothing to clean up.
  exec 9>>"${lock_file}"
  if ! flock -n 9; then
    echo "${BACKUP_LOCK_LOG_PREFIX}Another platform backup is in progress:" \
         "$(backup_lock_holder "${holder_file}")."
    if (( BACKUP_LOCK_WAIT_SECS > 0 )); then
      echo "${BACKUP_LOCK_LOG_PREFIX}Waiting up to ${BACKUP_LOCK_WAIT_SECS}s for it to" \
           "finish before ${operation} starts."
    else
      echo "${BACKUP_LOCK_LOG_PREFIX}Waiting for it to finish before ${operation} starts."
    fi
    local waited=0
    while ! flock -n 9; do
      sleep 5
      waited=$(( waited + 5 ))
      if (( BACKUP_LOCK_WAIT_SECS > 0 )) && (( waited >= BACKUP_LOCK_WAIT_SECS )); then
        echo "${BACKUP_LOCK_LOG_PREFIX}ERROR: gave up after ${waited}s waiting for the" \
             "platform backup held by" \
             "$(backup_lock_holder "${holder_file}")." >&2
        exit 1
      fi
      if (( waited % 30 == 0 )); then
        echo "${BACKUP_LOCK_LOG_PREFIX}Still waiting (${waited}s) for" \
             "$(backup_lock_holder "${holder_file}")..."
      fi
    done
    echo "${BACKUP_LOCK_LOG_PREFIX}The other backup finished, starting ${operation}."
  fi
  printf 'pid %s, %s, started %s' "$$" "${operation}" "$(date -u +'%Y-%m-%dT%H:%M:%SZ')" \
    > "${holder_file}" 2>/dev/null || true
}

modify_service() {
  if [[ "$SERVICE_BASED" = true ]] && [[ "$RESTART_PROCESSES" = true ]]; then
    set +e
    service="$1"
    operation="$2"
    echo "Performing operation $operation on service $service"
    run_sudo_cmd "systemctl ${operation} ${service}"
    set -e
  fi
}

# Returns 0 if the named database exists, non-zero otherwise. Mirrors the
# binary-discovery logic of create_postgres_backup / create_ybdb_backup so the
# probe always targets the same instance the dump would run against.
#
# Args: db_name db_username db_host db_port yba_installer ybdb pgdump_path ysql_dump_path
db_exists() {
  local db_name="$1"
  local db_username="$2"
  local db_host="$3"
  local db_port="$4"
  local yba_installer="$5"
  local ybdb="$6"
  # Arg 7 is any PG helper binary (pg_dump / pg_restore) - psql lives in the same directory
  # in the yba-installer layout, so we only use it to derive the psql path. Callers on the
  # backup path pass pg_dump; callers on the restore path pass pg_restore. Either works.
  local pg_helper_path="$7"
  local ysql_dump_path="$8"

  local probe="psql"
  if [[ "$ybdb" = true ]]; then
    probe="ysqlsh"
    if [[ "${ysql_dump_path}" != "" ]] && [[ -f "${ysql_dump_path}" ]]; then
      # ysqlsh ships next to ysql_dump in yba-installer layouts.
      probe="$(dirname "${ysql_dump_path}")/ysqlsh"
    fi
  elif [[ "${yba_installer}" = true ]] && [[ "${pg_helper_path}" != "" ]] && \
       [[ -f "${pg_helper_path}" ]]; then
    probe="$(dirname "${pg_helper_path}")/psql"
  fi

  local probe_cmd="${probe} -h ${db_host} -p ${db_port} -U ${db_username} -tAc \
\"SELECT 1 FROM pg_database WHERE datname='${db_name}'\""
  local probe_out
  probe_out=$(docker_aware_cmd "postgres" "${probe_cmd}" 2>/dev/null) || return 1
  [[ "${probe_out}" = "1" ]]
}

pa_config_schema_present() {
  local db_name="$1"
  local db_username="$2"
  local db_host="$3"
  local db_port="$4"
  local yba_installer="$5"
  local ybdb="$6"
  local pg_helper_path="$7"
  local ysql_dump_path="$8"

  local probe="psql"
  if [[ "$ybdb" = true ]]; then
    probe="ysqlsh"
    if [[ "${ysql_dump_path}" != "" ]] && [[ -f "${ysql_dump_path}" ]]; then
      probe="$(dirname "${ysql_dump_path}")/ysqlsh"
    fi
  elif [[ "${yba_installer}" = true ]] && [[ "${pg_helper_path}" != "" ]] && \
       [[ -f "${pg_helper_path}" ]]; then
    probe="$(dirname "${pg_helper_path}")/psql"
  fi

  # to_regclass() returns NULL when the relation does not exist (instead of erroring), so a
  # single query safely distinguishes "table present" from "empty database".
  local probe_cmd="${probe} -h ${db_host} -p ${db_port} -U ${db_username} -d ${db_name} -tAc \
\"SELECT to_regclass('public.customer_metadata') IS NOT NULL\""
  local probe_out
  probe_out=$(docker_aware_cmd "postgres" "${probe_cmd}" 2>/dev/null) || return 1
  [[ "${probe_out}" = "t" ]]
}

# Creates a Postgres DB backup of the given database.
# When ensure_db_exists is true, no special platform-specific behavior is applied;
# the database name and target file are the only data points that vary across callers.
create_postgres_backup() {
  backup_path="$1"
  db_username="$2"
  db_host="$3"
  db_port="$4"
  verbose="$5"
  yba_installer="$6"
  pgdump_path="$7"
  plain_sql="$8"
  db_name="$9"
  pg_dump="pg_dump"

  format="c"
  if [[ "${plain_sql}" = true ]]; then
      # pg_dump creates a plain-text SQL script file.
      format="p"
  fi
  # Determine pg_dump path in yba-installer cases where postgres is installed in data_dir.
  if [[ "${yba_installer}" = true ]] && \
     [[ "${pgdump_path}" != "" ]] && \
     [[ -f "${pgdump_path}" ]]; then
    pg_dump="${pgdump_path}"
  fi

  if [[ "${verbose}" = true ]]; then
    backup_cmd="${pg_dump} -h ${db_host} -p ${db_port} -U ${db_username} -F${format} -v --clean \
      ${db_name}"
  else
    backup_cmd="${pg_dump} -h ${db_host} -p ${db_port} -U ${db_username} -F${format} --clean \
      ${db_name}"
  fi
  echo "Creating Postgres DB ${db_name} backup ${backup_path}..."
  if [[ "${yba_installer}" = true ]]; then
    # -f flag does not work for docker based installs. Tries to dump inside postgres container but
    # we need output on the host itself.
    ybai_backup_cmd="${backup_cmd} -f ${backup_path}"
    docker_aware_cmd "postgres" "${ybai_backup_cmd}"
  else
    docker_aware_cmd "postgres" "${backup_cmd}" > "${backup_path}"
  fi
  echo "Done"
}

# Restores a Postgres DB backup of the given database. The target database is created if
# missing prior to restore (no-op if it already exists).
# When dump_check_table is non-empty, the dump file is sanity-checked for a COPY entry
# of the given table (e.g. "customer" for the platform DB, "customer_metadata" for PA).
# When copy_dump_file_path is non-empty and we're inside a K8s pod, the dump is copied
# to that path for deferred restore on restart instead of being restored in-place.
restore_postgres_backup() {
  backup_path="$1"
  db_username="$2"
  db_host="$3"
  db_port="$4"
  verbose="$5"
  yba_installer="$6"
  pgrestore_path="$7"
  dump_check_table="$8"
  copy_dump_file_path="$9"
  single_transaction="${10}"
  db_name="${11}"
  pg_restore="pg_restore"
  psql="psql"
  createdb="createdb"

  # Determine pg_restore path in yba-installer cases where postgres is installed in data_dir.
  if [[ "${yba_installer}" = true ]] && \
     [[ "${pgrestore_path}" != "" ]] && \
     [[ "${USE_SYSTEM_PG}" != true ]] && \
     [[ -f "${pgrestore_path}" ]]; then
    pg_restore=${pgrestore_path}
  fi

  if [[ "${pg_restore}" == "${pgrestore_path}" ]]; then
    psql=$(dirname "${pgrestore_path}")/psql
    createdb=$(dirname "${pgrestore_path}")/createdb
  fi

  echo "Ensuring database ${db_name} exists..."
  set +e
  create_db_cmd="${createdb} -h ${db_host} -p ${db_port} -U ${db_username} ${db_name}"
  docker_aware_cmd "postgres" "${create_db_cmd}" 2>/dev/null
  set -e

  # Optional sanity check that the dump actually contains data for the expected table.
  if [[ -n "${dump_check_table}" ]]; then
    if ! grep -iq "COPY.*${dump_check_table}" "${backup_path}"; then
      echo "${backup_path} potentially might be empty (no COPY for ${dump_check_table}), \
skipping restore"
      return
    fi
  fi

  # Optional K8s deferred-restore-on-restart path. The dump is staged to a known location
  # and consumed by yugaware on next start.
  if [[ -n "${copy_dump_file_path}" ]] && [[ "${INSIDE_K8S_POD}" = true ]]; then
    echo "Will restore ${db_name} DB backup on restart"
    echo "Copying SQL dump file to ${copy_dump_file_path}"
    run_sudo_cmd "cp ${backup_path} ${copy_dump_file_path}"
    return
  fi

  # Drop public schema so it is guaranteed to be a clean restore
  drop_cmd="${psql} -h ${db_host} -p ${db_port} -U ${db_username} -d ${db_name} \
    -c \"DROP SCHEMA IF EXISTS public CASCADE;CREATE SCHEMA public;\""
  docker_aware_cmd "postgres" "${drop_cmd}"
  restore_cmd=("${pg_restore} -h ${db_host} -p ${db_port} -U ${db_username} -c --if-exists \
      -d ${db_name}")
  if [[ "${verbose}" = true ]]; then
    restore_cmd+=( -v )
  fi
  if [[ "$single_transaction" = true ]]; then
    restore_cmd+=( --single_transaction )
  fi
  echo "Restoring Postgres DB ${db_name} backup ${backup_path}..."
  docker_aware_cmd "postgres" "${restore_cmd[@]}" < "${backup_path}"
  echo "Done"
}

# Creates a YBDB backup of the given database (yba-installer only).
create_ybdb_backup() {
  backup_path="$1"
  db_username="$2"
  db_host="$3"
  db_port="$4"
  verbose="$5"
  yba_installer="$6"
  ysql_dump_path="$7"
  db_name="$8"
  ysql_dump="ysql_dump"

  if [[ "$yba_installer" != true ]]; then
    echo "YBDB backup is only supported for yba-installer"
    return 1
  fi

  if [[ "${ysql_dump_path}" != "" ]] && [[ -f "${ysql_dump_path}" ]]; then
    ysql_dump="${ysql_dump_path}"
  fi

  if [[ "${verbose}" = true ]]; then
    backup_cmd="${ysql_dump} -h ${db_host} -p ${db_port} -U ${db_username} -f ${backup_path} -v \
     --clean ${db_name}"
  else
    backup_cmd="${ysql_dump} -h ${db_host} -p ${db_port} -U ${db_username} -f ${backup_path} \
     --clean ${db_name}"
  fi
  echo "Creating YBDB DB ${db_name} backup ${backup_path}..."
  ${backup_cmd}
  echo "Done"
}

# Restores a YBDB backup of the given database (yba-installer only). The target database
# is created if missing prior to restore (no-op if it already exists).
restore_ybdb_backup() {
  backup_path="$1"
  db_username="$2"
  db_host="$3"
  db_port="$4"
  verbose="$5"
  yba_installer="$6"
  ysqlsh_path="$7"
  db_name="$8"
  single_transaction="$9"
  ysqlsh="ysqlsh"

  if [[ "$yba_installer" != true ]]; then
    echo "YBDB restore is only supported for yba-installer"
    return 1
  fi

  if [[ "${ysqlsh_path}" != "" ]] && [[ -f "${ysqlsh_path}" ]]; then
    ysqlsh="${ysqlsh_path}"
  fi

  echo "Ensuring YBDB database ${db_name} exists..."
  set +e
  create_db_cmd="${ysqlsh} -h ${db_host} -p ${db_port} -U ${db_username} -c \
    \"CREATE DATABASE ${db_name};\""
  eval "${create_db_cmd}" 2>/dev/null
  set -e

  # Note that we use ysqlsh and not pg_restore to perform the restore,
  # as ysql reads plain-text SQL file to support restore from both ybdb and postgres,
  # which is necessary for postgres->ybdb migration in the future.
  restore_cmd=("${ysqlsh}" -h "${db_host}" -p "${db_port}" -U "${db_username}" \
    -d "${db_name}" -f "${backup_path}")
  if [[ "${verbose}" != true ]]; then
    restore_cmd+=( -q )
  fi
  if [[ "$single_transaction" = true ]]; then
    restore_cmd+=( --single-transaction )
  fi
  echo "Restoring YBDB DB ${db_name} backup ${backup_path}..."
  "${restore_cmd[@]}"
  echo "Done"
}

# Deletes a Yugabyte Platform DB backup.
delete_db_backup() {
  backup_path="$1"
  echo "Deleting Yugabyte Platform DB backup ${backup_path}..."
  if [[ -f "${backup_path}" ]]; then
    cleanup "${backup_path}"
    echo "Done"
  else
    echo "${backup_path} does not exist. Cannot delete"
  fi
}

# Creates a data-only pg_dump of the PA "configuration" tables (see PA_CONFIG_TABLES).
# Intended for the HA sync path so that the standby PA sees the same customer / universe
# metadata and runtime configuration as the active PA, without dragging in metrics data.
# The dump is a Postgres custom-format archive (-Fc); the restore side (see
# restore_include_pa_config_only_backup) TRUNCATEs the whitelisted tables and then loads
# them via pg_restore --data-only.
create_include_pa_config_only_backup() {
  backup_path="$1"
  db_username="$2"
  db_host="$3"
  db_port="$4"
  verbose="$5"
  yba_installer="$6"
  pgdump_path="$7"
  pg_dump="pg_dump"

  if [[ "${yba_installer}" = true ]] && \
     [[ "${pgdump_path}" != "" ]] && \
     [[ -f "${pgdump_path}" ]]; then
    pg_dump="${pgdump_path}"
  fi

  # Explicitly qualify with the public schema. `-t <name>` in pg_dump matches any table
  # named <name> in any schema, and while PA only creates its tables under public today,
  # being explicit avoids surprises if a future migration adds a shadow schema.
  local table_args=""
  for t in "${PA_CONFIG_TABLES[@]}"; do
    table_args+=" -t public.${t}"
  done

  local verbose_flag=""
  if [[ "${verbose}" = true ]]; then
    verbose_flag=" -v"
  fi

  # Note: pg_dump -v prints table names ("dumping contents of table
  # public.universe_metadata") and any COPY error path could echo the offending row.
  # universe_metadata carries universe DB credentials in auth_details, so we redirect
  # pg_dump's stderr to /dev/null in this code path even in verbose mode - operators who
  # need to debug the config sync can rerun pg_dump manually against a scratch DB.
  local backup_cmd="${pg_dump} -h ${db_host} -p ${db_port} -U ${db_username} -Fc${verbose_flag} \
    --data-only${table_args} ${PA_DB_NAME}"
  echo "Creating PA config-only backup ${backup_path}..."
  if [[ "${yba_installer}" = true ]]; then
    ybai_backup_cmd="${backup_cmd} -f ${backup_path}"
    docker_aware_cmd "postgres" "${ybai_backup_cmd}" 2>/dev/null
  else
    docker_aware_cmd "postgres" "${backup_cmd}" 2>/dev/null > "${backup_path}"
  fi
  echo "Done"
}

# Restores a data-only PA config-only backup produced by create_include_pa_config_only_backup.
# Truncates the whitelisted tables first, then loads them via pg_restore --data-only. Every
# other PA table (metrics, anomalies, background_tasks, ...) is left untouched on the
# standby. Finally sets collection_enabled=FALSE on customer_metadata so the standby PA
# immediately stops scraping / running anomaly detection - the recurring
# EmbeddedCollectorInitializer on the standby YBA will confirm this with a PUT within
# ~1 minute, but doing it here removes the window between restore and that PUT.
#
# The TRUNCATE ... CASCADE up front is required for two reasons:
#   1. pg_restore --clean is a no-op in --data-only mode (--clean only drops schema objects,
#      which are skipped in data-only), so without the explicit TRUNCATE the standby would
#      keep any rows the active no longer has (e.g. universes/customers that were
#      unregistered on the active, or leftovers from a previous promotion).
#   2. Fresh COPY on top of surviving rows would collide on primary keys.
# CASCADE is only there for FKs *within* the whitelist (universe_details -> universe_metadata,
# user_auth_token -> users). No PA data table (metrics, anomalies, node_metadata, ...) FKs any
# whitelisted config table today, so CASCADE doesn't touch scraped state on the standby.
restore_include_pa_config_only_backup() {
  backup_path="$1"
  db_username="$2"
  db_host="$3"
  db_port="$4"
  verbose="$5"
  yba_installer="$6"
  pgrestore_path="$7"
  pg_restore="pg_restore"
  psql="psql"

  if [[ "${yba_installer}" = true ]] && \
     [[ "${pgrestore_path}" != "" ]] && \
     [[ "${USE_SYSTEM_PG}" != true ]] && \
     [[ -f "${pgrestore_path}" ]]; then
    pg_restore=${pgrestore_path}
    psql=$(dirname "${pgrestore_path}")/psql
  fi

  # Build a comma-separated table list from the same whitelist used to create the dump.
  local truncate_targets=""
  for t in "${PA_CONFIG_TABLES[@]}"; do
    if [[ -n "${truncate_targets}" ]]; then
      truncate_targets+=","
    fi
    truncate_targets+=" ${t}"
  done

  # Don't echo the individual table list - universe_metadata carries universe DB
  # credentials in auth_details, and even the table name is sensitive enough that we
  # would prefer it not surface in HA sync logs.
  echo "Wiping PA config tables on standby..."
  truncate_cmd="${psql} -h ${db_host} -p ${db_port} -U ${db_username} -d ${PA_DB_NAME} \
    -q -v ON_ERROR_STOP=1 -c \"TRUNCATE${truncate_targets} RESTART IDENTITY CASCADE;\""
  # -q silences the "TRUNCATE TABLE" banner. Errors still surface via ON_ERROR_STOP.
  # stderr is redirected because a TRUNCATE constraint failure would include the row
  # detail (universe_metadata.auth_details JSON carries per-universe DB credentials) which
  # we must not leak.
  docker_aware_cmd "postgres" "${truncate_cmd}" 2>/dev/null

  echo "Restoring PA config-only backup ${backup_path}..."
  # Never pass -v to pg_restore here even if the caller asked for verbose: pg_restore -v
  # would log lines like `processing data for table "public"."universe_details"` and,
  # on any COPY failure, dump the offending row - the universe_details JSON blob contains
  # per-universe DB credentials.
  restore_cmd="${pg_restore} -h ${db_host} -p ${db_port} -U ${db_username} \
    --data-only --no-owner --no-acl -d ${PA_DB_NAME}"
  docker_aware_cmd "postgres" "${restore_cmd}" < "${backup_path}" 2>/dev/null

  # Post-restore fixups: two independent UPDATEs, kept in a single psql invocation so we
  # only pay for one round-trip.
  #   1. collection_enabled=FALSE on every customer_metadata row. The dump was produced on
  #      the active (where collection_enabled defaults to TRUE), so a plain data-only
  #      restore would flip the standby's collection_enabled to TRUE and briefly let it
  #      scrape / run anomaly detection until the next EmbeddedCollectorInitializer PUT
  #      corrects it.
  #   2. Clear picked / picked_by / last_heartbeat on scheduled_tasks. These fields describe
  #      *who* is currently running the task. On the active they point at the active's
  #      hostname; if we left them alone the newly promoted PA's task runner would treat
  #      those tasks as owned by someone else and wait for a heartbeat timeout before
  #      taking them over. Resetting them lets the promoted PA claim work immediately.
  echo "Marking customer_metadata rows as collection_enabled=FALSE and resetting" \
    "scheduled_tasks picks..."
  post_restore_sql="\
UPDATE customer_metadata SET collection_enabled = FALSE; \
UPDATE scheduled_tasks SET picked = FALSE, picked_by = NULL, last_heartbeat = NULL;"
  update_cmd="${psql} -h ${db_host} -p ${db_port} -U ${db_username} -d ${PA_DB_NAME} \
    -q -v ON_ERROR_STOP=1 -c \"${post_restore_sql}\""
  docker_aware_cmd "postgres" "${update_cmd}" 2>/dev/null
  echo "Done"
}

create_backup() {
  now=$(date -u +"%y-%m-%d-%H-%M")
  output_path="${1}"
  data_dir="${2}"
  exclude_prometheus="${3}"
  exclude_releases="${4}"
  db_username="${5}"
  db_host="${6}"
  db_port="${7}"
  verbose="${8}"
  prometheus_host="${9}"
  prometheus_port="${10}"
  k8s_namespace="${11}"
  k8s_pod="${12}"
  pgdump_path="${13}"
  plain_sql="${14}"
  ybdb="${15}"
  ysql_dump_path="${16}"
  prometheus_protocol="${17}"
  exclude_pa_database="${18}"
  exclude_pa_files="${19}"
  include_pa_config_only="${20:-false}"
  include_releases_flag="**/releases/**"
  include_uploaded_releases_flag="**/upload/release_artifacts/**"

  mkdir -p "${output_path}"
  # Canonicalize only after attempting to create the dir; realpath fails on
  # non-existent paths, and callers may pass an output dir that hasn't been created yet.
  output_path=$(realpath "${output_path}")

  # Perform K8s backup.
  if [[ -n "${k8s_namespace}" ]] || [[ -n "${k8s_pod}" ]]; then
    # Run backup script in container.
    verbose_flag=""
    if [[ "${verbose}" == true ]]; then
      verbose_flag="-v"
    fi
    backup_script="/opt/yugabyte/devops/bin/yb_platform_backup.sh"
    # Currently, this script does not support backup/restore of Prometheus data for K8s deployments.
    # On K8s deployments (unlike Replicated deployments) the prometheus data volume for snapshots is
    # not shared between the yugaware and prometheus containers.
    exclude_flags=""
    if [[ "$exclude_releases" = true ]]; then
      exclude_flags="--exclude_releases"
    fi
    if [[ "$exclude_prometheus" = true ]]; then
      exclude_flags+=" --exclude_prometheus"
    fi
    if [[ "$exclude_pa_database" = true ]]; then
      exclude_flags+=" --exclude_pa_database"
    fi
    if [[ "$exclude_pa_files" = true ]]; then
      exclude_flags+=" --exclude_pa_files"
    fi
    if [[ "$include_pa_config_only" = true ]]; then
      exclude_flags+=" --include_pa_config_only"
    fi
    kubectl -n "${k8s_namespace}" exec -it "${k8s_pod}" -c yugaware -- /bin/bash -c \
      "${backup_script} create ${verbose_flag} ${exclude_flags} --output ${K8S_BACKUP_DIR}"
    # Determine backup archive filename.
    # Note: There is a slight race condition here. It will always use the most recent backup file.
    backup_file=$(kubectl -n "${k8s_namespace}" -c yugaware exec -it "${k8s_pod}" -c yugaware -- \
      /bin/bash -c "cd ${K8S_BACKUP_DIR} && ls -1 backup*.tgz | tail -n 1")
    backup_file=${backup_file%$'\r'}
    # Ensure backup succeeded.
    if [[ -z "${backup_file}" ]]; then
      echo "Failed"
      return
    fi

    echo "Copying backup from container"
    # Copy backup archive from container to local machine.
    kubectl -n "${k8s_namespace}" -c yugaware cp --request-timeout="${k8s_timeout}" \
      "${k8s_pod}:${K8S_BACKUP_DIR}/${backup_file}" "${output_path}/${backup_file}"

    # Delete backup archive from container.
    kubectl -n "${k8s_namespace}" exec -it "${k8s_pod}" -c yugaware -- \
      /bin/bash -c "rm ${K8S_BACKUP_DIR}/backup*.tgz"
    echo "Done"
    return
  fi



  # Everything this invocation materialises - the database dumps, the PA marker, the version
  # metadata copy, the Prometheus snapshot - goes under here. These used to be written under
  # fixed names directly in the data directory, so an HA backup and a yba-ctl upgrade backup
  # running at the same time overwrote each other's dumps and then deleted them from under each
  # other, leaving an archive with a truncated or missing dump and no error.
  staging_dir="$(mktemp -d "${data_dir}/${BACKUP_STAGING_PREFIX}XXXXXX")"
  # EXIT as well as RETURN: several of the steps below exit outright on bad input, and a leaked
  # staging directory would sit in the data directory holding a dump-sized file forever.
  trap 'run_sudo_cmd "rm -rf ${staging_dir}"' RETURN EXIT

  if [ "$disable_version_check" != true ]; then

    metadata_regex="**/yugaware/conf/${VERSION_METADATA}"
    metadata_dir="${data_dir}"
    target_dir="${staging_dir}"
    # Hardcode container values for replicated
    if [[ "$DOCKER_BASED" = true ]]; then
      metadata_dir="/opt/yugabyte"
      target_dir="/opt/yugabyte/yugaware/data"
    fi
    if [[ "${yba_installer}" = true ]]; then
      version=$(basename $(realpath ${data_dir}/software/active))
      metadata_regex="**/${version}/**/yugaware/conf/${VERSION_METADATA}"
    fi
    version_path=$(docker_aware_cmd "yugaware" "find ${metadata_dir} -wholename ${metadata_regex}")

    command="cp ${version_path} ${target_dir}/${VERSION_METADATA_BACKUP}"
    docker_aware_cmd "yugaware" "${command}"
  fi

  if [[ "$exclude_releases" = true ]]; then
    include_releases_flag=""
    include_uploaded_releases_flag=""
  fi

  modify_service yb-platform stop

  tar_name="${output_path}/backup_${now}.tar"
  tgz_name="${output_path}/backup_${now}.tgz"
  db_backup_path="${staging_dir}/${PLATFORM_DUMP_FNAME}"
  if [[ "$ybdb" = true ]]; then
    create_ybdb_backup "${db_backup_path}" "${db_username}" "${db_host}" "${db_port}" \
                             "${verbose}" "${yba_installer}" "${ysql_dump_path}" \
                             "${PLATFORM_DB_NAME}"
  else
    create_postgres_backup "${db_backup_path}" "${db_username}" "${db_host}" "${db_port}" \
                         "${verbose}" "${yba_installer}" "${pgdump_path}" "${plain_sql}" \
                         "${PLATFORM_DB_NAME}"
  fi

  # Backup PA (ts) database unless excluded.
  # PA is an optional component, so the ts database may legitimately not exist
  # (e.g. the customer has never deployed Performance Advisor). In that case
  # skip the dump instead of failing the whole backup.
  pa_db_backup_path="${staging_dir}/${PA_DUMP_FNAME}"
  include_pa_config_only_marker_path="${staging_dir}/${INCLUDE_PA_CONFIG_ONLY_MARKER_FNAME}"
  pa_db_present=false
  if [[ "$include_pa_config_only" = true ]] && [[ "$exclude_pa_database" = true ]]; then
    echo "Error: --include_pa_config_only is mutually exclusive with --exclude_pa_database" >&2
    exit 1
  fi
  if [[ "$exclude_pa_database" = false ]]; then
    if db_exists "${PA_DB_NAME}" "${db_username}" "${db_host}" "${db_port}" \
                 "${yba_installer}" "${ybdb}" "${pgdump_path}" "${ysql_dump_path}"; then
      if [[ "$include_pa_config_only" = true ]]; then
        # HA-sync path: dump only the whitelisted PA "configuration" tables (data-only)
        # so the standby PA gets customer / universe metadata and runtime config without
        # inheriting the active's metrics / anomalies / background_tasks data.
        if [[ "$ybdb" = true ]]; then
          echo "Error: --include_pa_config_only is not supported with --ybdb" >&2
          exit 1
        fi
        # The 'ts' database can exist while empty (yba-installer's createTSDatabase creates it
        # unconditionally; the schema only appears once the embedded PA collector migrates it).
        # A data-only dump of the whitelisted tables would fail against an empty database, so
        # only produce the config dump + marker when the schema is actually present.
        if pa_config_schema_present "${PA_DB_NAME}" "${db_username}" "${db_host}" "${db_port}" \
                                    "${yba_installer}" "${ybdb}" "${pgdump_path}" \
                                    "${ysql_dump_path}"; then
          pa_db_present=true
          create_include_pa_config_only_backup "${pa_db_backup_path}" "${db_username}" \
            "${db_host}" "${db_port}" "${verbose}" "${yba_installer}" "${pgdump_path}"
          # Drop a marker so the restore side knows to use the data-only restore path.
          touch "${include_pa_config_only_marker_path}"
        else
          echo "Performance Advisor database '${PA_DB_NAME}' exists but has no PA schema" \
            "- skipping PA config backup."
        fi
      elif [[ "$ybdb" = true ]]; then
        pa_db_present=true
        create_ybdb_backup "${pa_db_backup_path}" "${db_username}" "${db_host}" "${db_port}" \
                           "${verbose}" "${yba_installer}" "${ysql_dump_path}" "${PA_DB_NAME}"
      else
        pa_db_present=true
        create_postgres_backup "${pa_db_backup_path}" "${db_username}" "${db_host}" "${db_port}" \
                               "${verbose}" "${yba_installer}" "${pgdump_path}" \
                               "${plain_sql}" "${PA_DB_NAME}"
      fi
    else
      echo "Performance Advisor database '${PA_DB_NAME}' not found - skipping PA DB backup."
    fi
  fi

  TAR_OPTIONS="-r"
  if [[ "${verbose}" = true ]]; then
     TAR_OPTIONS+="v"
  fi
  TAR_OPTIONS+="f ${tar_name}"
  FIND_OPTIONS=( . \\\( -path  \'**/data/certs/**\' )
  FIND_OPTIONS+=( $(printf " -o -path '%s'"  "**/data/keys/**" "**/data/provision/**" \
              "**/data/licenses/**"  "**/data/yb-platform/keys/**" "**/data/yb-platform/certs/**" \
              "**/swamper_rules/**" "**/swamper_targets/**" "**/prometheus/rules/**"  \
              "**/prometheus/targets/**" "**/data/yb-platform/node-agent/certs/**" \
              "**/data/node-agent/certs/**" "**/provision/**/provision_instance.py" \
              "${include_releases_flag}" "${include_uploaded_releases_flag}") )

  # Include PA collected data files in backup unless excluded. Uses the same printf trick as the
  # block above to embed literal single quotes around the glob, so the pattern survives the
  # `eval find ...` invocation instead of being expanded by the shell first.
  if [[ "$exclude_pa_files" = false ]]; then
    FIND_OPTIONS+=( $(printf " -o -path '%s'" "**/${PA_DATA_DIR}/collected/**") )
  fi

  # Backup prometheus data.
  if [[ "$exclude_prometheus" = false ]]; then
    echo "Creating prometheus snapshot..."
    set_prometheus_data_dir "${prometheus_host}" "${prometheus_port}" "${data_dir}" \
      "${prometheus_protocol}"
    snapshot_cmd="curl -k -X POST \
      ${prometheus_protocol}://${prometheus_host}:${prometheus_port}/api/v1/admin/tsdb/snapshot"
    if [[ -n "${PROMETHEUS_USERNAME:-}" ]] && [[ -n "${PROMETHEUS_PASSWORD:-}" ]]; then
      snapshot_cmd="${snapshot_cmd} -u ${PROMETHEUS_USERNAME}:${PROMETHEUS_PASSWORD}"
    fi
    snapshot_dir=$( $snapshot_cmd | ${PYTHON_EXECUTABLE} -c \
      "import sys, json; print(json.load(sys.stdin)['data']['name'])")
    mkdir -p "$staging_dir/$PROMETHEUS_SNAPSHOT_DIR"
    run_sudo_cmd "cp -aR ${PROMETHEUS_DATA_DIR}/snapshots/${snapshot_dir} \
    ${staging_dir}/${PROMETHEUS_SNAPSHOT_DIR}"
    run_sudo_cmd "rm -rf ${PROMETHEUS_DATA_DIR}/snapshots/${snapshot_dir}"
  fi
  # [PLAT-19026] exclude node-agent releases to prevent k8s overwrite
  FIND_OPTIONS+=( \\\) -not -path \"**/node-agent/releases/**\" )
  # Skip every staging directory, this invocation's included: the staged files are appended
  # below under the archive paths restore_backup expects, and a concurrent invocation's staging
  # directory must never be swept into this archive.
  FIND_OPTIONS+=( -not -path \"**/${BACKUP_STAGING_PREFIX}*\" )
  FIND_OPTIONS+=( -exec tar $TAR_OPTIONS \{} + )
  echo "Creating platform backup package..."
  cd ${data_dir}

  eval find -L ${FIND_OPTIONS[@]}

  # Append the staged files with the names they had when they were written straight into the data
  # directory - './platform_dump.sql', './prometheus_snapshot/...' - because restore_backup looks
  # for them at the root of the extracted archive, and its Prometheus snapshot lookup matches the
  # './' prefix that find produced.
  staged_entries=()
  for staged in "${PLATFORM_DUMP_FNAME}" "${VERSION_METADATA_BACKUP}" "${PA_DUMP_FNAME}" \
                "${INCLUDE_PA_CONFIG_ONLY_MARKER_FNAME}" "${PROMETHEUS_SNAPSHOT_DIR}"; do
    if [[ -e "${staging_dir}/${staged}" ]]; then
      staged_entries+=( "./${staged}" )
    fi
  done
  if (( ${#staged_entries[@]} > 0 )); then
    tar $TAR_OPTIONS -C "${staging_dir}" "${staged_entries[@]}"
  fi

  gzip -9 < ${tar_name} > ${tgz_name}
  cleanup "${tar_name}"

  # Everything else this invocation wrote is under ${staging_dir} and goes with it. The docker
  # path is the exception: the copy is made inside the container, at a path this staging
  # directory does not map to, so it still has to be removed on its own.
  if [[ "$DOCKER_BASED" = true ]]; then
    docker_aware_cmd "yugaware" "rm -f ${target_dir}/${VERSION_METADATA_BACKUP}"
  fi

  echo "Finished creating backup ${tgz_name}"
  modify_service yb-platform restart
}

restore_backup() {
  input_path="${1}"
  destination="${2}"
  db_host="${3}"
  db_port="${4}"
  db_username="${5}"
  verbose="${6}"
  prometheus_host="${7}"
  prometheus_port="${8}"
  data_dir="${9}"
  k8s_namespace="${10}"
  k8s_pod="${11}"
  disable_version_check="${12}"
  pgrestore_path="${13}"
  ybdb="${14}"
  ysqlsh_path="${15}"
  ybai_data_dir="${16}"
  skip_old_files="${17}"
  skip_dump_check="${18}"
  prometheus_protocol="${19}"
  skip_dump_file_delete="${20}"
  single_transaction="${21}"
  exclude_pa_database="${22}"
  exclude_pa_files="${23}"
  prometheus_dir_regex="\.\/${PROMETHEUS_SNAPSHOT_DIR}\/[[:digit:]]{8}T[[:digit:]]{6}Z-[[:alnum:]]{16}\/$"

  # Perform K8s restore.
  if [[ -n "${k8s_namespace}" ]] || [[ -n "${k8s_pod}" ]]; then

    # Copy backup archive to container.
    echo "Copying backup to container"
    kubectl -n "${k8s_namespace}" -c yugaware cp --request-timeout="${k8s_timeout}" \
      "${input_path}" "${k8s_pod}:${K8S_BACKUP_DIR}"
    echo "Done"

    # Determine backup archive filename.
    # Note: There is a slight race condition here. It will always use the most recent backup file.
    backup_file=$(kubectl -n "${k8s_namespace}" -c yugaware exec -it "${k8s_pod}" -c yugaware -- \
      /bin/bash -c "cd ${K8S_BACKUP_DIR} && ls -1 backup*.tgz | tail -n 1")
    backup_file=${backup_file%$'\r'}
    # Run restore script in container.
    verbose_flag=""
    if [[ "${verbose}" == true ]]; then
      verbose_flag="-v"
    fi
    backup_script="/opt/yugabyte/devops/bin/yb_platform_backup.sh"
    # Skip old files as script was called outside of container so may lack permissions to overwrite
    restore_args=(${verbose_flag} --input ${K8S_BACKUP_DIR}/${backup_file} --skip_old_files)
    if [[ "$disable_version_check" = true ]]; then
      restore_args+=( --disable_version_check )
    fi
    if [[ "$single_transaction" = true ]]; then
      restore_args+=( --single_transaction )
    fi
    if [[ "$exclude_pa_database" = true ]]; then
      restore_args+=( --exclude_pa_database )
    fi
    if [[ "$exclude_pa_files" = true ]]; then
      restore_args+=( --exclude_pa_files )
    fi
    cmd=("$backup_script" restore "${restore_args[@]}")
    kubectl -n "${k8s_namespace}" exec -it "${k8s_pod}" -c yugaware -- /bin/bash -c \
        "$(printf '%q ' "${cmd[@]}")"

    # Delete backup archive from container.
    kubectl -n "${k8s_namespace}" exec -it "${k8s_pod}" -c yugaware -- \
      /bin/bash -c "rm ${K8S_BACKUP_DIR}/backup*.tgz"
    return
  fi

  if [ "$disable_version_check" != true ]; then

    current_metadata_path=""

    if [ -f "../../src/main/resources/${VERSION_METADATA}" ]; then

        current_metadata_path="../../src/main/resources/${VERSION_METADATA}"

    else

        metadata_regex="**/yugaware/conf/${VERSION_METADATA}"
        if [[ "${yba_installer}" = true ]]; then
          version=$(basename $(realpath ${data_dir}/software/active))
          metadata_regex="**/${version}/**/yugaware/conf/${VERSION_METADATA}"
        fi
        # Ignore errors in case of directories where we don't have permissions
        set +e
        current_metadata_path=$(find ${destination} -wholename ${metadata_regex})
        set -e

        # At least keep some default as a worst case.
        if [ ! -f ${current_metadata_path} ] || [ -z ${current_metadata_path} ]; then
          current_metadata_path="${data_dir}/yugaware/conf/${VERSION_METADATA}"
        fi

    fi

    command="cat ${current_metadata_path}"

    version_cmd='import json, sys; print(json.load(sys.stdin)["version_number"])'
    build_cmd='import json, sys; print(json.load(sys.stdin)["build_number"])'

    version=$(docker_aware_cmd "yugaware" "${command}" | ${PYTHON_EXECUTABLE} -c "${version_cmd}")
    build=$(docker_aware_cmd "yugaware" "${command}" | ${PYTHON_EXECUTABLE} -c "${build_cmd}")

    curr_platform_version=${version}-${build}

    backup_metadata_path=$(tar -tzf ${input_path} | grep ${VERSION_METADATA_BACKUP} | head -1)
    if [[ "${backup_metadata_path}" == "" ]]; then
      echo "cannot perform version check on backup ${input_path}, no ${VERSION_METADATA_BACKUP}
      found. Please run restore with --disable_version_check or take a new backup with \
      ${VERSION_METADATA_BACKUP}"
      exit 1
    fi
    tar -xzf ${input_path} -C ${destination} ${backup_metadata_path}
    set +e
    backup_metadata_path=$(find ${destination} -name ${VERSION_METADATA_BACKUP} | head -1)
    set -e
    if [ ! -f ${backup_metadata_path} ] || [ -z ${backup_metadata_path} ]; then
      echo "could not find untarred ${VERSION_METADATA_BACKUP}"
      exit 1
    fi
    # The version_metadata.json file is always present in a release package, and it would have
    # been stored during create_backup(), so we don't need to check if the file exists before
    # restoring it from the restore path.
    backup_yba_version=$(cat "${backup_metadata_path}" | ${PYTHON_EXECUTABLE} -c "${version_cmd}")
    backup_yba_build=$(cat "${backup_metadata_path}" | ${PYTHON_EXECUTABLE} -c "${build_cmd}")
    back_plat_version=${backup_yba_version}-${backup_yba_build}

    # Delete the backup metadata path after using it
    rm ${backup_metadata_path}

    if [ ${curr_platform_version} != ${back_plat_version} ]
    then
      echo "Your backups were created on a platform of version ${back_plat_version}, and you are
      attempting to restore these backups on a platform of version ${curr_platform_version},
      which is a mismatch. Please restore your platform instance exactly back to
      ${back_plat_version} to proceed, or override this check by running the script with the
      command line argument --disable_version_check true"
      exit 1
    fi
  fi

  modify_service yb-platform stop

  untar_dir="${destination}"
  tar_cmd="tar -xzf"
  if [[ "${verbose}" = true ]]; then
    tar_cmd="tar -xzvf"
  fi
  if [[ "${migration}" = true ]]; then
    untar_dir="${destination}"/"${MIGRATION_BACKUP_DIR}"
    rm -rf "${untar_dir}"
    mkdir -p "${untar_dir}"
    $tar_cmd "${input_path}" --directory "${untar_dir}"

    # Copy over releases. Need to ignore node-agent/ybc releases
    set +e
    releasesdir=$(find "${untar_dir}" -name "releases" -type d | \
                  grep -v "ybc" | grep -v "node-agent")
    set -e
    if [[ "$releasesdir" != "" ]] && [[ -d "$releasesdir" ]]; then
      cp -R "$releasesdir" "$ybai_data_dir"
    fi
    # Node-agent/ybc foldes can be copied entirely into
    # Copy releases, ybc, certs, keys, over
    # xcerts/keys/licenses can all go directly into data directory
    BACKUP_DIRS=('*ybc' '*data/certs' '*data/keys' '*data/licenses' '*node-agent' \
      '*upload/release_artifacts')
    for d in "${BACKUP_DIRS[@]}"
    do
      set +e
      found_dir=$(find "${untar_dir}" -path "$d" -type d)
      set -e
      if [[ "$found_dir" != "" ]] && [[ -d "$found_dir" ]]; then
        cp -R "$found_dir" "$ybai_data_dir"
      fi
    done
  else
    # Remove swamper targets and rules being used from mounted location
    if [[ "$INSIDE_K8S_POD" = true ]]; then
      run_sudo_cmd "rm -f ${destination}/prometheus/targets/* ${destination}/prometheus/targets/*"
    fi

    rm -f "${destination}/${PA_DUMP_FNAME}" "${destination}/${INCLUDE_PA_CONFIG_ONLY_MARKER_FNAME}"

    $tar_cmd "${input_path}" --directory "${destination}" "${skip_old_files}"
  fi

  db_backup_path="${untar_dir}"/"${PLATFORM_DUMP_FNAME}"
  trap 'delete_db_backup ${db_backup_path}' RETURN
  # When --skip_dump_check is set, bypass the COPY sanity check by passing an empty table name.
  # When --skip_dump_file_delete is set, stage the platform dump for deferred restore on restart.
  platform_dump_check_table="customer"
  if [[ "$skip_dump_check" = true ]]; then
    platform_dump_check_table=""
  fi
  platform_copy_dump_path=""
  if [[ "$skip_dump_file_delete" = true ]]; then
    platform_copy_dump_path="${PLATFORM_DUMP_K8S_DEFERRED_PATH}"
  fi
  if [[ "${ybdb}" = true ]]; then
    restore_ybdb_backup "${db_backup_path}" "${db_username}" "${db_host}" "${db_port}" \
      "${verbose}" "${yba_installer}" "${ysqlsh_path}" "${PLATFORM_DB_NAME}" \
      "${single_transaction}"
  else
    # do we need set +e?
    restore_postgres_backup "${db_backup_path}" "${db_username}" "${db_host}" "${db_port}" \
      "${verbose}" "${yba_installer}" "${pgrestore_path}" "${platform_dump_check_table}" \
      "${platform_copy_dump_path}" "${single_transaction}" "${PLATFORM_DB_NAME}"
  fi

  # Restore PA (ts) database unless excluded
  if [[ "$exclude_pa_database" = false ]]; then
    pa_db_backup_path="${untar_dir}"/"${PA_DUMP_FNAME}"
    include_pa_config_only_marker_path="${untar_dir}"/"${INCLUDE_PA_CONFIG_ONLY_MARKER_FNAME}"
    if [[ -f "${pa_db_backup_path}" ]]; then
      if [[ -f "${include_pa_config_only_marker_path}" ]]; then
        # The dump was produced with --include_pa_config_only. Only load whitelisted tables
        # and flip collection_enabled back to FALSE afterwards; skip when the PA DB doesn't
        # exist yet on this instance - the standby PA's own Flyway migrations will run on
        # first start and the next HA sync will populate config.
        if [[ "${ybdb}" = true ]]; then
          echo "Error: --include_pa_config_only restore is not supported with --ybdb" >&2
          exit 1
        fi
        # On the restore path we don't have pgdump_path. psql lives next to pg_restore in the
        # yba-installer layout, so pass pgrestore_path in the "helper" slot - db_exists /
        # pa_config_schema_present use it purely to derive the psql directory.
        #
        # Two independent guards, both required. The 'ts' database existing is NOT sufficient:
        # yba-installer always provisions an empty 'ts' (createTSDatabase), so on an instance
        # where PA is/was disabled the database is present but has no schema. Restoring a
        # data-only config dump into it (TRUNCATE + COPY) would fail and abort the whole HA
        # restore, so skip unless the collector has already migrated its schema. The standby
        # PA's own Flyway migrations run on first start and the next HA sync then populates it.
        if ! db_exists "${PA_DB_NAME}" "${db_username}" "${db_host}" "${db_port}" \
                       "${yba_installer}" "${ybdb}" "${pgrestore_path}" "${ysqlsh_path}"; then
          echo "Performance Advisor database '${PA_DB_NAME}' not found" \
            "- skipping PA config restore."
        elif ! pa_config_schema_present "${PA_DB_NAME}" "${db_username}" "${db_host}" \
                       "${db_port}" "${yba_installer}" "${ybdb}" "${pgrestore_path}" \
                       "${ysqlsh_path}"; then
          echo "Performance Advisor database '${PA_DB_NAME}' exists but has no PA schema" \
            "(the embedded collector has not migrated it yet) - skipping PA config restore."
        else
          restore_include_pa_config_only_backup "${pa_db_backup_path}" "${db_username}" \
            "${db_host}" "${db_port}" "${verbose}" "${yba_installer}" "${pgrestore_path}"
        fi
        rm -f "${include_pa_config_only_marker_path}"
      else
        pa_dump_check_table="customer_metadata"
        if [[ "$skip_dump_check" = true ]]; then
          pa_dump_check_table=""
        fi
        pa_copy_dump_path=""
        if [[ "$skip_dump_file_delete" = true ]]; then
          pa_copy_dump_path="${PA_DUMP_K8S_DEFERRED_PATH}"
        fi
        if [[ "${ybdb}" = true ]]; then
          restore_ybdb_backup "${pa_db_backup_path}" "${db_username}" "${db_host}" "${db_port}" \
            "${verbose}" "${yba_installer}" "${ysqlsh_path}" "${PA_DB_NAME}" \
            "${single_transaction}"
        else
          restore_postgres_backup "${pa_db_backup_path}" "${db_username}" "${db_host}" \
            "${db_port}" "${verbose}" "${yba_installer}" "${pgrestore_path}" \
            "${pa_dump_check_table}" "${pa_copy_dump_path}" "${single_transaction}" \
            "${PA_DB_NAME}"
        fi
      fi
      delete_db_backup "${pa_db_backup_path}"
    else
      echo "No ${PA_DUMP_FNAME} found in archive, skipping PA DB restore"
    fi
  fi

  # Restore prometheus swamper targets on migration always
  if [[ "${yba_installer}" = true ]] && [[ "${migration}" = true ]]; then
    set +e
    backup_targets=$(find "${untar_dir}" -name swamper_targets -type d)
    set -e
    if  [[ "$backup_targets" != "" ]] && [[ -d "$backup_targets" ]]; then
      run_sudo_cmd "cp -Tr ${backup_targets} ${destination}/data/prometheus/swamper_targets"
    fi
    set +e
    backup_rules=$(find "${untar_dir}" -name swamper_rules -type d)
    set -e
    if  [[ "$backup_rules" != "" ]] && [[ -d "$backup_rules" ]]; then
      run_sudo_cmd "cp -Tr ${backup_rules} ${destination}/data/prometheus/swamper_rules"
    fi
    run_sudo_cmd "chown -R ${yba_user}:${yba_user} ${destination}/data/prometheus"
  fi
  set +e
  prom_snapshot=$(tar -tf "${input_path}" | grep -E $prometheus_dir_regex)
  set -e
  if [[ -n "$prom_snapshot" ]]; then
    echo "Restoring prometheus snapshot..."
    set_prometheus_data_dir "${prometheus_host}" "${prometheus_port}" "${data_dir}" \
      "${prometheus_protocol}"
    modify_service prometheus stop

    # Stop prometheus writes by replacing scrape configs with empty scrape job config
    if [[ "$INSIDE_K8S_POD" = true ]]; then
      empty_scrape_job_config="/default_prometheus_config/no_scrape.yml"
      prometheus_config="/prometheus_configs/prometheus.yml"
      run_prom_reload "${empty_scrape_job_config}" "${prometheus_config}" "${prometheus_host}" \
        "${prometheus_port}" "${prometheus_protocol}"
    fi

    run_sudo_cmd "rm -rf ${PROMETHEUS_DATA_DIR}/*"
    run_sudo_cmd "mv ${untar_dir}/${prom_snapshot:2}* ${PROMETHEUS_DATA_DIR}"

    if [[ "${yba_installer}" = true ]]; then
      run_sudo_cmd "chown -R ${yba_user}:${yba_user} ${destination}/data/prometheus"
    elif [[ "$SERVICE_BASED" = true ]]; then
      run_sudo_cmd "chown -R ${prometheus_user}:${prometheus_user} ${PROMETHEUS_DATA_DIR}"
    elif [[ "$INSIDE_K8S_POD" = true ]]; then
      echo "Skipping the chown for prometheus directory in kubernetes"
    else
      run_sudo_cmd "chown -R ${NOBODY_UID}:${NOBODY_UID} ${PROMETHEUS_DATA_DIR}"
    fi
    # Clean up snapshot after restore
    run_sudo_cmd "rm -rf ${untar_dir}/${PROMETHEUS_SNAPSHOT_DIR}"
    # Manually execute so postgres TRAP executes.
    modify_service prometheus restart
    if [[ "$DOCKER_BASED" = true ]]; then
      run_sudo_cmd "docker restart prometheus"
    fi
    # Reload prometheus for K8s
    if [[ "$INSIDE_K8S_POD" = true ]]; then
      prom_quit_cmd="curl -k -X POST \
        ${prometheus_protocol}://${prometheus_host}:${prometheus_port}/-/quit"
      if [[ -n "${PROMETHEUS_USERNAME:-}" ]] && [[ -n "${PROMETHEUS_PASSWORD:-}" ]]; then
        prom_quit_cmd="${prom_quit_cmd} -u ${PROMETHEUS_USERNAME}:${PROMETHEUS_PASSWORD}"
      fi
      run_sudo_cmd "$prom_quit_cmd"

      wait_for_prom "${prometheus_host}" "${prometheus_port}" "${prometheus_protocol}"

      # Start prometheus writes by replacing scrape configs with scrape job config
      scrape_job_config="/default_prometheus_config/prometheus.yml"
      prometheus_config="/prometheus_configs/prometheus.yml"
      run_prom_reload "${scrape_job_config}" "${prometheus_config}" "${prometheus_host}" \
        "${prometheus_port}" "${prometheus_protocol}"
    fi
  fi
  # Create following directory if it wasn't created yet so restore will succeed.
  if [[ "${yba_installer}" = false ]]; then
    mkdir -p "${destination}/release"
  fi

  if [[ "$migration" = true ]]; then
    rm -rf "${destination}/${MIGRATION_BACKUP_DIR}"
  fi


  # Delete any extra version metadata files. These may not exist, so this is best effort
  rm -f ${data_dir}/${VERSION_METADATA_BACKUP}
  rm -f ${data_dir}/yugaware/${VERSION_METADATA}

  modify_service yb-platform restart

  echo "Finished restoring backup"
}

validate_k8s_args() {
  k8s_namespace="${1}"
  k8s_pod="${2}"
  if [[ -n "${k8s_namespace}" ]] || [[ -n "${k8s_pod}" ]]; then
    if [[ -z "${k8s_namespace}" ]] || [[ -z "${k8s_pod}" ]]; then
      echo "Error: Must specify both --k8s_namespace and --k8s_pod"
      exit 1
    fi
  fi
}

validate_prometheus_args() {
  if [[ $prometheus_protocol != "http" ]] && [[ $prometheus_protocol != "https" ]]; then
    echo "Error: prometheus_protocol must be either http or https"
    exit 1
  fi
  if [[ -n "${PROMETHEUS_USERNAME:-}" ]] && [[ -z "${PROMETHEUS_PASSWORD:-}" ]]; then
    echo "Error: PROMETHEUS_USERNAME is set but PROMETHEUS_PASSWORD is not. Either both must be set or unset."
    exit 1
  fi
  if [[ -z "${PROMETHEUS_USERNAME:-}" ]] && [[ -n "${PROMETHEUS_PASSWORD:-}" ]]; then
    echo "Error: PROMETHEUS_PASSWORD is set but PROMETHEUS_USERNAME is not. Either both must be set or unset."
    exit 1
  fi
}

print_backup_usage() {
  echo "Create: ${SCRIPT_NAME} create [options]"
  echo "options:"
  echo "  -o, --output                   the directory that the platform backup is written to (default: ${HOME})"
  echo "  -m, --exclude_prometheus       exclude prometheus metric data from backup (default: false)"
  echo "  -r, --exclude_releases         exclude Yugabyte releases from backup (default: false)"
  echo "  -d, --data_dir=DIRECTORY       data directory (default: /opt/yugabyte)"
  echo "  -v, --verbose                  verbose output of script (default: false)"
  echo "  -s  --skip_restart             [WARNING: DEPRECATED] don't restart processes during execution (default: false)"
  echo "  --restart                      restart processes during execution (default: false)"
  echo "  -u, --db_username=USERNAME     postgres username (default: postgres)"
  echo "  -h, --db_host=HOST             postgres host (default: localhost)"
  echo "  -P, --db_port=PORT             postgres port (default: 5432)"
  echo "  -n, --prometheus_host=HOST     prometheus host (default: localhost)"
  echo "  -t, --prometheus_port=PORT     prometheus port (default: 9090)"
  echo "  --prometheus_protocol          prometheus protocol (default: http)."
  echo "  --k8s_namespace                kubernetes namespace"
  echo "  --k8s_pod                      kubernetes pod"
  echo "  --k8s_timeout                  kubernetes cp timeout duration (default: 30m)"
  echo "  --yba_installer                yba_installer installation (default: false)"
  echo "  --plain_sql                    output a plain-text SQL script from pg_dump"
  echo "  --ybdb                         ybdb backup (default: false)"
  echo "  --ysql_dump_path               path to ysql_sump to dump ybdb"
  echo "  --exclude_pa_database          exclude Performance Advisor database from backup (default: false)"
  echo "  --exclude_pa_files             exclude Performance Advisor collected data files from backup (default: false)"
  echo "  --include_pa_config_only       dump only Performance Advisor 'configuration' tables"
  echo "                                 (whitelisted subset); mutually exclusive with"
  echo "                                 --exclude_pa_database (default: false)"
  echo "  --disable_version_check        disable the backup version check (default: false)"
  echo "  -?, --help                     show create help, then exit"
  echo
  echo "NOTE: If prometheus authentication is enabled, PROMETHEUS_USERNAME and PROMETHEUS_PASSWORD environment variables must be set"
  echo
}

print_restore_usage() {
  echo "Restore: ${SCRIPT_NAME} restore --input <input_path> [options]"
  echo "<input_path> the path to the platform backup tar.gz"
  echo "options:"
  echo "  -o, --destination=DIRECTORY        where to un-tar the backup (default: /opt/yugabyte)"
  echo "  -d, --data_dir=DIRECTORY           data directory (default: /opt/yugabyte)"
  echo "  -v, --verbose                      verbose output of script (default: false)"
  echo "  -s  --skip_restart                 don't restart processes during execution (default: false)"
  echo "  -u, --db_username=USERNAME         postgres username (default: postgres)"
  echo "  -h, --db_host=HOST                 postgres host (default: localhost)"
  echo "  -P, --db_port=PORT                 postgres port (default: 5432)"
  echo "  -n, --prometheus_host=HOST         prometheus host (default: localhost)"
  echo "  -t, --prometheus_port=PORT         prometheus port (default: 9090)"
  echo "  -e, --prometheus_user=USERNAME     prometheus user (default: prometheus)"
  echo "  --prometheus_protocol              prometheus protocol (default: http)."
  echo "  -U, --yba_user=USERNAME            yugabyte anywhere user (default: yugabyte)"
  echo "  --k8s_namespace                    kubernetes namespace"
  echo "  --k8s_pod                          kubernetes pod"
  echo "  --k8s_timeout                      kubernetes cp timeout duration (default: 30m)"
  echo "  --disable_version_check            disable the backup version check (default: false)"
  echo "  --yba_installer                    yba_installer backup (default: false)"
  echo "  --ybdb                             ybdb restore (default: false)"
  echo "  --ysqlsh_path                      path to ysqlsh to restore ybdb (default: false)"
  echo "  --migration                        migration from Replicated or Yugabundle (default: false)"
  echo "  --ybai_data_dir                    YBA data dir (default: /opt/yugabyte/data/yb-platform)"
  echo "  --skip_old_files                   skip old files when untarring backup"
  echo "  --skip_dump_check                  skip pg dump empty check before restore (default: false)"
  echo "  --skip_dump_file_delete            skip deleting dump file extracted from backup archive (default: false)"
  echo "  --exclude_pa_database              exclude Performance Advisor database from restore (default: false)"
  echo "  --exclude_pa_files                 exclude Performance Advisor collected data files from restore (default: false)"
  echo "  -?, --help                         show restore help, then exit"
  echo
  echo "NOTE: If prometheus authentication is enabled, PROMETHEUS_USERNAME and PROMETHEUS_PASSWORD environment variables must be set"
  echo
}

print_help() {
  echo "Create or restore a Yugabyte Platform backup"
  echo
  echo "Usage: ${SCRIPT_NAME} <command>"
  echo "command:"
  echo "  create                         create a Yugabyte Platform backup"
  echo "  restore                        restore a Yugabyte Platform backup"
  echo "  -?, --help                     show this help, then exit"
  echo
  print_backup_usage
  print_restore_usage
}

cleanup () {
  rm -f "$1"
}

if [[ $# -eq 0 ]]; then
  print_help
  exit 1
fi

command=$1
shift

# Default global options.
db_username=postgres
db_host=localhost
db_port=5432
prometheus_host=localhost
prometheus_port=9090
prometheus_protocol=http
prometheus_user=prometheus
k8s_namespace=""
k8s_pod=""
k8s_timeout="30m"
data_dir=/opt/yugabyte
verbose=false
disable_version_check=false
yba_installer=false
pgdump_path=""
pgpass_path=""
pgrestore_path=""
plain_sql=false
ybdb=false
ysql_dump_path=""
ysqlsh_path=""
migration=false
ybai_data_dir=/opt/yugabyte/data/yb-platform
yba_user=yugabyte
skip_old_files=""
skip_dump_check=false

case $command in
  -?|--help)
    print_help
    exit 0
    ;;
  create)
    # Default create options.
    exclude_prometheus=false
    exclude_releases=false
    exclude_pa_database=false
    exclude_pa_files=false
    include_pa_config_only=false
    output_path="${HOME}"
    RESTART_PROCESSES=false

    if [[ $# -eq 0 ]]; then
      print_backup_usage
      exit 1
    fi

    while (( "$#" )); do
      case "$1" in
        -o|--output)
          output_path="$2"
          shift 2
          ;;
        -m|--exclude_prometheus)
          exclude_prometheus=true
          shift
          ;;
        -r|--exclude_releases)
          exclude_releases=true
          shift
          ;;
        -d|--data_dir)
          data_dir=$(realpath $2)
          shift 2
          ;;
        -v|--verbose)
          verbose=true
          set -x
          shift
          ;;
        -s|--skip_restart)
          echo "--skip_restart flag is deprecated and default behavior skips restart. use --restart"
          shift
          ;;
        --restart)
          RESTART_PROCESSES=true
          shift
          ;;
        -u|--db_username)
          db_username=$2
          shift 2
          ;;
        -h|--db_host)
          db_host=$(echo "$2" | sed 's/^\[\(.*\)\]$/\1/')
          shift 2
          ;;
        -P|--db_port)
          db_port=$2
          shift 2
          ;;
        --plain_sql)
          plain_sql=true
          shift
          ;;
        -n|--prometheus_host)
          prometheus_host=$(echo "$2" | sed 's/^\[\(.*\)\]$/\1/')
          shift 2
          ;;
        -t|--prometheus_port)
          prometheus_port=$2
          shift 2
          ;;
        --prometheus_protocol)
          prometheus_protocol=$2
          shift 2
          ;;
        --k8s_namespace)
          k8s_namespace=$2
          shift 2
          ;;
        --k8s_pod)
          k8s_pod=$2
          shift 2
          ;;
        --k8s_timeout)
          k8s_timeout=$2
          shift 2
          ;;
        --yba_installer)
          yba_installer=true
          shift
          ;;
        --pg_dump_path)
          pgdump_path=$(realpath $2)
          shift 2
          ;;
        --pgpass_path)
          pgpass_path=$(realpath $2)
          shift 2
          ;;
        --ybdb)
          ybdb=true
          shift
          ;;
        --ysql_dump_path)
          ysql_dump_path=$(realpath $2)
          shift 2
          ;;
        --exclude_pa_database)
          exclude_pa_database=true
          shift
          ;;
        --exclude_pa_files)
          exclude_pa_files=true
          shift
          ;;
        --include_pa_config_only)
          include_pa_config_only=true
          shift
          ;;
        --disable_version_check)
          disable_version_check=true
          set -x
          shift
          ;;
        -?|--help)
          print_backup_usage
          exit 0
          ;;
        *)
          echo "${SCRIPT_NAME}: Unrecognized argument ${1}"
          echo
          print_backup_usage
          exit 1
      esac
    done

    validate_k8s_args "${k8s_namespace}" "${k8s_pod}"
    validate_prometheus_args

    if [[ "${pgpass_path}" != "" ]]; then
      export PGPASSFILE=${pgpass_path}
    fi
    acquire_backup_lock "${data_dir}" "backup create"

    create_backup "$output_path" "$data_dir" "$exclude_prometheus" "$exclude_releases" \
    "$db_username" "$db_host" "$db_port" "$verbose" "$prometheus_host" "$prometheus_port" \
    "$k8s_namespace" "$k8s_pod" "$pgdump_path" "$plain_sql" "$ybdb" "$ysql_dump_path" \
    "$prometheus_protocol" "$exclude_pa_database" "$exclude_pa_files" "$include_pa_config_only"
    exit 0
    ;;
  restore)
    # Default restore options.
    destination=/opt/yugabyte
    input_path=""
    skip_dump_file_delete=false
    single_transaction=false
    exclude_pa_database=false
    exclude_pa_files=false

    if [[ $# -eq 0 ]]; then
      print_restore_usage
      exit 1
    fi

    while (( "$#" )); do
      case "$1" in
        -i|--input)
          input_path=$(realpath $2)
          shift 2
          ;;
        -o|--destination)
          destination=$(realpath $2)
          shift 2
          ;;
        -d|--data_dir)
          data_dir=$(realpath $2)
          shift 2
          ;;
        -v|--verbose)
          verbose=true
          set -x
          shift
          ;;
        -s|--skip_restart)
          RESTART_PROCESSES=false
          set -x
          shift
          ;;
        -u|--db_username)
          db_username=$2
          shift 2
          ;;
        -h|--db_host)
          db_host=$(echo "$2" | sed 's/^\[\(.*\)\]$/\1/')
          shift 2
          ;;
        -P|--db_port)
          db_port=$2
          shift 2
          ;;
        -n|--prometheus_host)
          prometheus_host=$(echo "$2" | sed 's/^\[\(.*\)\]$/\1/')
          shift 2
          ;;
        -t|--prometheus_port)
          prometheus_port=$2
          shift 2
          ;;
        -e|--prometheus_user)
          prometheus_user=$2
          shift 2
          ;;
        --prometheus_protocol)
          prometheus_protocol=$2
          shift 2
          ;;
        --k8s_namespace)
          k8s_namespace=$2
          shift 2
          ;;
        --k8s_pod)
          k8s_pod=$2
          shift 2
          ;;
        --k8s_timeout)
          k8s_timeout=$2
          shift 2
          ;;
        --disable_version_check)
          disable_version_check=true
          set -x
          shift
          ;;
        --yba_installer)
          yba_installer=true
          DOCKER_BASED=false
          shift
          ;;
        --pg_restore_path)
          pgrestore_path=$(realpath $2)
          shift 2
          ;;
        --pgpass_path)
          pgpass_path=$(realpath $2)
          shift 2
          ;;
        --ybdb)
          ybdb=true
          shift
          ;;
        --ysqlsh_path)
          ysqlsh_path=$(realpath $2)
          shift 2
          ;;
        --yugabundle)
          echo "--yugabundle is deprecated. Please use --migration instead."
          migration=true
          shift
          ;;
        --migration)
          migration=true
          shift
          ;;
        --ybai_data_dir)
          ybai_data_dir=$(realpath $2)
          shift 2
          ;;
        -U|--yba_user)
          yba_user=$2
          shift 2
          ;;
        --use_system_pg)
          USE_SYSTEM_PG=true
          shift
          ;;
        --skip_old_files)
          skip_old_files="--skip-old-files"
          shift
          ;;
        --skip_dump_check)
          skip_dump_check=true
          shift
          ;;
        --skip_dump_file_delete)
          skip_dump_file_delete=true
          shift
          ;;
        --single_transaction)
          single_transaction=true
          shift
          ;;
        --exclude_pa_database)
          exclude_pa_database=true
          shift
          ;;
        --exclude_pa_files)
          exclude_pa_files=true
          shift
          ;;
        -?|--help)
          print_restore_usage
          exit 0
          ;;
        *)
          echo "${SCRIPT_NAME}: Unrecognized option ${1}"
          echo
          print_restore_usage
          exit 1
      esac
    done

    if [[ -z "$input_path" ]]; then
      echo "${SCRIPT_NAME}: input_path is required"
      echo
      print_restore_usage
      exit 1
    fi

    validate_k8s_args "${k8s_namespace}" "${k8s_pod}"
    validate_prometheus_args

    if [[ "${pgpass_path}" != "" ]]; then
      export PGPASSFILE=${pgpass_path}
    fi

    acquire_backup_lock "${data_dir}" "backup restore"

    restore_backup "$input_path" "$destination" "$db_host" "$db_port" "$db_username" "$verbose" \
    "$prometheus_host" "$prometheus_port" "$data_dir" "$k8s_namespace" "$k8s_pod" \
    "$disable_version_check" "$pgrestore_path" "$ybdb" "$ysqlsh_path" "$ybai_data_dir" \
    "$skip_old_files" "$skip_dump_check" "$prometheus_protocol" "$skip_dump_file_delete" \
    "$single_transaction" "$exclude_pa_database" "$exclude_pa_files"
    exit 0
    ;;
  *)
    echo "${SCRIPT_NAME}: Unrecognized command ${command}"
    echo
    print_help
    exit 1
esac
