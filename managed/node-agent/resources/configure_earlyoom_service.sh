#!/usr/bin/env bash
#
# Copyright 2021 YugabyteDB, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

set -euo pipefail

print_help() {
  cat <<EOT
    Usage: configure_earlyoom_service.sh [<options>]
    Options:
      -a, --action
        Action to perform: enable|disable|configure
      -c, --config
        Earlyoom args
      -h, --help
        Show usage
EOT
}

action="enable"
config=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    -a|--action)
      if [[ "$2" =~ ^(enable|disable|configure)$ ]]; then
        action=$2
      else
        echo "Invalid action: $1" >&2
        print_help
        exit 1
      fi
      shift
    ;;
    -c|--config)
      config=$2
      shift
    ;;
    -f|--file)
      config=$(<"$2")
      shift
    ;;
    -h|--help)
      print_help
      exit 0
    ;;
    *)
      echo "Invalid option: $1" >&2
      print_help
      exit 1
  esac
  shift
done

if [ -z ${YB_HOME_DIR+x} ]; then
  YB_HOME_DIR="/home/yugabyte"
fi

systemctl --user status earlyoom && systemctl --user disable earlyoom --now

if [[ "$action" = "configure" || "$action" = "enable" ]]; then
  if ! [[ -z "${config}" ]]; then
    stripped_config=$(sed -e 's/^"//' -e 's/"$//' <<< "$config")
    echo "Updating config: $stripped_config"
    echo "EARLYOOM_ARGS=\"$stripped_config\"" > $YB_HOME_DIR/bin/earlyoom.config
  elif [ "$action" = "configure" ]; then
    echo "Config should be provided for configure action" >&2
    exit 1
  fi
fi

if [ "$action" = "enable" ]; then
  systemctl --user enable earlyoom --now
fi
