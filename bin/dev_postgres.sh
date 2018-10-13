#!/usr/bin/env bash

set -euo pipefail

. "${BASH_SOURCE%/*}"/../build-support/common-test-env.sh

print_usage() {
  cat <<-EOT
${0##*/}: manage a development PostgreSQL server (running outside of a YB tserver)
Usage: ${0##*/} [options] [<command>]
Options:
  -h, --help
    Print usage
  --recreate
    Destroy and re-create the local cluster and PostgreSQL data directory
Commands:
  postgres - start the local YugaByte PostgreSQL server
  psql - start psql and connect to the local YugaByte PostgreSQL server
EOT
}

set_common_postgres_env() {
  export PGDATA=/tmp/ybpgdata

  # Needed when running on our LDAP/NFS-based GCP devservers.
  export YB_PG_FALLBACK_SYSTEM_USER_NAME=$USER

  export YB_ENABLED_IN_POSTGRES=1

  export FLAGS_pggate_master_addresses=127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100
  YB_PG_PORT=54320

  YB_PG_BIN_DIR=$YB_SRC_ROOT/build/latest/postgres/bin
}

print_usage_and_exit() {
  print_usage
  exit 1
}

parse_cmd_line_args() {
  recreate=false
  command=""
  while [[ $# -gt 0 ]]; do
    case "$1" in
      -h|--help)
        print_usage_and_exit ;;
      --recreate)
        recreate=true
      ;;
      psql|postgres)
        if [[ -n $command && $command != $1 ]]; then
          fatal "Different commands specified: '$command' and '$1'"
        fi
        command=$1
      ;;
      *)
        log "Invalid option: $1"
        exit 1
    esac
    shift
  done

  if [[ -z $command ]]; then
    print_usage
    log "No command specified"
    exit 1
  fi
}

kill_postgres_server() {
  set +e
  ( set -x; pkill -f "postgres --port=$YB_PG_PORT" )
  set -e
}

cleanup() {
  kill_postgres_server
}

manage_postgres_server() {
  kill_postgres_server
  if "$recreate"; then
    heading "Re-creating the local cluster and PostgreSQL directory"
    rm -rf "$PGDATA"
    "$YB_SRC_ROOT/bin/yb-ctl" destroy
    "$YB_SRC_ROOT/bin/yb-ctl" create
  else
    "$YB_SRC_ROOT/bin/yb-ctl" start
  fi

  if "$recreate" || [[ ! -d $PGDATA ]]; then
    heading "Running initdb"
    $YB_PG_BIN_DIR/initdb -U postgres
  fi

  cd "$PGDATA"

  set -m  # enable job control
  heading "Starting the PostgreSQL server"
  trap cleanup EXIT
  "$YB_PG_BIN_DIR/postgres" --port=$YB_PG_PORT &

  heading "Bringing the PostgreSQL server back into foreground"
  fg %1
}

start_psql() {
  "$YB_PG_BIN_DIR/psql" --port=$YB_PG_PORT -h localhost -U postgres
}

# -------------------------------------------------------------------------------------------------
# Main script
# -------------------------------------------------------------------------------------------------

set_common_postgres_env
parse_cmd_line_args "$@"

case $command in
  postgres)
    manage_postgres_server
  ;;
  psql)
    start_psql
  ;;
  *)
    fatal "Unknown command: $command"
esac
