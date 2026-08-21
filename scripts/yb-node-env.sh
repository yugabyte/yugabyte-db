#!/usr/bin/env bash
#
# yb-node-env.sh — On a YBA-deployed master or tserver node, read the local
# server.conf and either:
#   - (sourced)   export MASTERS and CERTS_DIR into the current shell, or
#   - (executed)  print `export ...` lines suitable for `eval "$(...)"`.
#
# MASTERS    is the value of --master_addresses, in host:port,host:port form,
#            ready for `yb-admin --master_addresses="$MASTERS"`.
# CERTS_DIR  is the value of --certs_dir, ready for
#            `yb-admin --certs_dir_name="$CERTS_DIR"`. Empty when TLS is off.
#
# Usage:
#   source ./scripts/yb-node-env.sh
#   eval   "$(./scripts/yb-node-env.sh)"               # when not sourcing
#   source ./scripts/yb-node-env.sh --role tserver
#   source ./scripts/yb-node-env.sh --conf /path/to/server.conf
#
# Companion to scripts/yb-universe-masters-env.sh, which discovers masters by
# calling yb-admin against a seed address. This script reads the local
# on-disk config instead, so it works on the node itself before any yb-admin
# call is wired up (and without needing TLS material to be loaded yet).
#
# Environment (optional):
#   YB_HOME   Base install dir (default: /home/yugabyte)

_yb_node_env_main() {
  local sourced="$1"; shift

  local home="${YB_HOME:-/home/yugabyte}"
  local role=""
  local conf=""

  while [[ $# -gt 0 ]]; do
    case "$1" in
      --role)   role="$2"; shift 2 ;;
      --conf)   conf="$2"; shift 2 ;;
      --home)   home="$2"; shift 2 ;;
      -h|--help)
        cat >&2 <<EOF
Usage: source ${BASH_SOURCE[0]##*/} [--role master|tserver] [--conf PATH] [--home DIR]
   or: eval "\$(${BASH_SOURCE[0]##*/} [...same args...])"

Reads --master_addresses and --certs_dir from a YugabyteDB server.conf and
exposes them as MASTERS and CERTS_DIR for use with yb-admin.

  --role   Force which conf to read (default: master, falling back to tserver)
  --conf   Explicit path to server.conf
  --home   Override base install dir (default: \$YB_HOME or /home/yugabyte)
EOF
        return 0
        ;;
      *)
        echo "yb-node-env: unknown arg: $1" >&2
        return 2
        ;;
    esac
  done

  if [[ -z "$conf" ]]; then
    case "$role" in
      master)  conf="$home/master/conf/server.conf" ;;
      tserver) conf="$home/tserver/conf/server.conf" ;;
      "")
        local c
        for c in "$home/master/conf/server.conf" "$home/tserver/conf/server.conf"; do
          if [[ -r "$c" ]]; then conf="$c"; break; fi
        done
        ;;
      *)
        echo "yb-node-env: invalid --role: $role (expected master or tserver)" >&2
        return 2
        ;;
    esac
  fi

  if [[ -z "$conf" || ! -r "$conf" ]]; then
    echo "yb-node-env: no readable server.conf (looked under $home/{master,tserver}/conf/)" >&2
    return 1
  fi

  # Last-wins extraction of a gflag value, supporting both `--flag=value` and
  # `--flag value` forms. Trims surrounding whitespace and quote characters.
  local masters certs
  masters=$(awk '
    { sub(/^[[:space:]]+/, ""); sub(/[[:space:]]+$/, "") }
    /^--master_addresses=/             { v = $0; sub(/^--master_addresses=/, "", v); last = v }
    /^--master_addresses[[:space:]]/   { v = $0; sub(/^--master_addresses[[:space:]]+/, "", v); last = v }
    END { gsub(/^["'\'']|["'\'']$/, "", last); print last }
  ' "$conf")

  certs=$(awk '
    { sub(/^[[:space:]]+/, ""); sub(/[[:space:]]+$/, "") }
    /^--certs_dir=/             { v = $0; sub(/^--certs_dir=/, "", v); last = v }
    /^--certs_dir[[:space:]]/   { v = $0; sub(/^--certs_dir[[:space:]]+/, "", v); last = v }
    END { gsub(/^["'\'']|["'\'']$/, "", last); print last }
  ' "$conf")

  if [[ -z "$masters" ]]; then
    echo "yb-node-env: --master_addresses not found in $conf" >&2
    return 1
  fi

  if [[ "$sourced" == "1" ]]; then
    export MASTERS="$masters"
    export CERTS_DIR="$certs"
    {
      echo "yb-node-env: source = $conf"
      echo "  MASTERS   = $MASTERS"
      if [[ -n "$CERTS_DIR" ]]; then
        echo "  CERTS_DIR = $CERTS_DIR"
        echo "  Try: yb-admin --master_addresses=\"\$MASTERS\" --certs_dir_name=\"\$CERTS_DIR\" list_all_masters"
      else
        echo "  CERTS_DIR = (--certs_dir not found; TLS likely disabled)"
        echo "  Try: yb-admin --master_addresses=\"\$MASTERS\" list_all_masters"
      fi
    } >&2
  else
    printf 'export MASTERS=%q\n' "$masters"
    printf 'export CERTS_DIR=%q\n' "$certs"
  fi
}

if (return 0 2>/dev/null); then
  _yb_node_env_main 1 "$@"
  _yb_node_env_status=$?
  unset -f _yb_node_env_main
  return $_yb_node_env_status
else
  _yb_node_env_main 0 "$@"
  exit $?
fi
