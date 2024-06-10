#!/usr/bin/env bash

#
# Copyright (c) YugaByte, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
# in compliance with the License.  You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software distributed under the License
# is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
# or implied.  See the License for the specific language governing permissions and limitations
# under the License.
#
set -uo pipefail

print_help() {
  cat <<EOT
Usage: ${0##*/} [<options>]
  Helper script find failed tablets on each tserver. This script also
  prints command that can be used to tombstone the failed tablets.
Options:
  --master_addresses <master_addresses>
    comma separated list of YB Master server addresses.
  --reason_string <reason_string>
    'reason' string to be passed to the tablet server, used for logging.
  --binary_path <tabletdatadir>
    directory containing yb-admin on local filesystem.
  -h, --help
    Show usage.
EOT
}

master_addresses=""
reason_string="Manually deleted by operator"
binary_path="."
while [[ $# -gt 0 ]]; do
  case "$1" in
    --master_addresses)
      master_addresses=$2
      shift
    ;;
    --reason_string)
      reason_string=$2
      shift
    ;;
    --binary_path)
      binary_path=$2
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

if [[ -z "$master_addresses" ]]; then
  echo "Need to specify -master_addresses" >&2
  print_help >&2
  exit 1
fi

# Generate tserver ip:port-uuid list sorted by tserver ip:port for deterministic behavior.

# Output before grepping:
# Tablet Server UUID                      RPC Host/Port
# 6adfcf3e996a40fba039dcc108e6df66        127.0.0.4:9100
# 33b3a57874f74ae9a6416d29cfee6412        127.0.0.3:9100
# e812de390ca448d9bd30f0cdb6a16ea9        127.0.0.2:9100
# bbc4dc1482b9405d81a8e4c4574e05df        127.0.0.1:9100
ts_uuid_ts_ip_port_s=$("$binary_path"/yb-admin -master_addresses "$master_addresses" \
  list_all_tablet_servers | grep -v "Tablet Server" | awk '{print $2"-"$1}' | sort)
# Output after grepping:
# 127.0.0.1:9100-bbc4dc1482b9405d81a8e4c4574e05df
# 127.0.0.2:9100-e812de390ca448d9bd30f0cdb6a16ea9
# 127.0.0.3:9100-33b3a57874f74ae9a6416d29cfee6412
# 127.0.0.4:9100-6adfcf3e996a40fba039dcc108e6df66

found_failed_tablet=""

# Find failed tablets on each tserver.
for ts_uuid_ts_ip_port in $ts_uuid_ts_ip_port_s; do
  ts_uuid=${ts_uuid_ts_ip_port##*-}
  ts_ip_port=${ts_uuid_ts_ip_port%%-*}

  echo "-- "
  echo "  checking tserver: $ts_uuid - $ts_ip_port:";
  echo

  # Output before grepping:
  # Table name         Tablet ID   Is Leader State   Num SST Files   Num Log Segments
  # cassandrakeyvalue  34490085... 0     SHUTDOWN        0       0
  # cassandrakeyvalue  0fc1cdd5... 0     SHUTDOWN        0       0
  # cassandrakeyvalue  88c29872... 0     SHUTDOWN        0       0
  # cassandrakeyvalue  ce3c59a4... 0     FAILED  0       0
  # cassandrakeyvalue  af739719... 1     RUNNING         1       2
  # cassandrakeyvalue  feb651b8... 0     RUNNING         1       2
  failed_tablet_ids=$("$binary_path"/yb-admin -master_addresses "$master_addresses" \
    list_tablets_for_tablet_server "$ts_uuid" | grep -w FAILED | awk '{print $2}');
  # Output after grepping:
  # ce3c59a4a3834b4cab430b778289ec14

  if [[ -n $failed_tablet_ids ]]; then
    # Print failed tablet ids.
    echo -n "  failed tablet id(s):";
    for failed_tablet_id in $failed_tablet_ids; do
      echo -n " $failed_tablet_id"
      found_failed_tablet="yes"
    done

    echo
    echo
    echo "  Command(s) to tombstone failed tablets (after RCA of tablet failure(s))."

    # Print command(s) to tombstone failed tablets.
    for failed_tablet_id in $failed_tablet_ids; do
      echo "    \"$binary_path\"/yb-ts-cli --server_address=$ts_ip_port delete_tablet "\
        "$failed_tablet_id \"$reason_string\""
    done
    echo
  fi
done

if [[ -n $found_failed_tablet ]]; then
  exit 1
fi

echo "No failed tablets."
exit 0
