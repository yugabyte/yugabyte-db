#!/bin/bash
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

set -x

# Time marker for both stderr and stdout
date 1>&2

export KUDU_HOME=${KUDU_HOME:-/usr/lib/kudu}

CMD=$1
shift 2

function log {
  timestamp=$(date)
  echo "$timestamp: $1"       #stdout
  echo "$timestamp: $1" 1>&2; #stderr
}

# Reads a line in the format "$host:$key=$value", setting those variables.
function readconf {
  local conf
  IFS=':' read host conf <<< "$1"
  IFS='=' read key value <<< "$conf"
}

log "KUDU_HOME: $KUDU_HOME"
log "CONF_DIR: $CONF_DIR"
log "CMD: $CMD"

# Make sure we've got the main gflagfile.
GFLAG_FILE="$CONF_DIR/gflagfile"
if [ ! -r "$GFLAG_FILE" ]; then
  log "Could not find $GFLAG_FILE, exiting"
  exit 1
fi

# Make sure we've got a file describing the master config.
MASTER_FILE="$CONF_DIR/master.properties"
if [ ! -r "$MASTER_FILE" ]; then
  log "Could not find $MASTER_FILE, exiting"
  exit 1
fi

# Parse the master config.
MASTER_IPS=
for line in $(cat "$MASTER_FILE")
do
  readconf "$line"
  case $key in
    server.address)
      # Fall back to the host only if there's no defined value.
      if [ -n "$value" ]; then
        actual_value="$value"
      else
        actual_value="$host"
      fi

      # Append to comma-separated MASTER_IPS.
      if [ -n "$MASTER_IPS" ]; then
        MASTER_IPS="${MASTER_IPS},"
      fi
      MASTER_IPS="${MASTER_IPS}${actual_value}"
      ;;
  esac
done
log "Found master(s) on $MASTER_IPS"

# Enable core dumping if requested.
if [ "$ENABLE_CORE_DUMP" == "true" ]; then
  # The core dump directory should already exist.
  if [ -z "$CORE_DUMP_DIRECTORY" -o ! -d "$CORE_DUMP_DIRECTORY" ]; then
    log "Could not find core dump directory $CORE_DUMP_DIRECTORY, exiting"
    exit 1
  fi
  # It should also be writable.
  if [ ! -w "$CORE_DUMP_DIRECTORY" ]; then
    log "Core dump directory $CORE_DUMP_DIRECTORY is not writable, exiting"
    exit 1
  fi

  ulimit -c unlimited
  cd "$CORE_DUMP_DIRECTORY"
  STATUS=$?
  if [ $STATUS != 0 ]; then
    log "Could not change to core dump directory to $CORE_DUMP_DIRECTORY, exiting"
    exit $STATUS
  fi
fi

if [ "$CMD" = "master" ]; then
  # Only pass --master_addresses if there's more than one master.
  #
  # Need to use [[ ]] for regex support.
  if [[ "$MASTER_IPS" =~ , ]]; then
    MASTER_ADDRESSES="--master_addresses=$MASTER_IPS"
  fi

  exec "$KUDU_HOME/sbin/kudu-master" \
    $MASTER_ADDRESSES \
    --flagfile="$GFLAG_FILE"
elif [ "$CMD" = "tserver" ]; then
  exec "$KUDU_HOME/sbin/kudu-tserver" \
    --tserver_master_addrs="$MASTER_IPS" \
    --flagfile="$GFLAG_FILE"
else
  log "Unknown command: $CMD"
  exit 2
fi
