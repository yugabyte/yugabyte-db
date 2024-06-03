#!/bin/bash

set -e

currentUser="$(whoami)"
SOFTWARE_ROOT="$1"
shift
DATA_ROOT="$1"
shift
externalPort="$1"
shift
maxConcurrency="$1"
shift
maxSamples="$1"
shift
timeout="$1"
shift
restartSeconds="$1"
shift
promVersion="$1"

#Process name that need to be monitored
process_name="prometheus"

#Location of pgrep utility
PGREP="/usr/bin/pgrep"

#Initially, on startup do create a testfile to indicate that the process
#need to be monitored. If you dont want the process to be monitored, then
#delete this file or stop this service. This is the infinite while loop for non-root
#monitoring, which we can break out of as needed.
touch $SOFTWARE_ROOT/$process_name/testfile

while true;
do

if [ ! -f $SOFTWARE_ROOT/$process_name/testfile ]; then
        break
fi

if ! $PGREP $process_name >/dev/null 2>&1 ;
then

# restart <process>
$SOFTWARE_ROOT/yba_installer/packages/prometheus-${promVersion}.linux-amd64/prometheus \
--config.file $SOFTWARE_ROOT/prometheus/conf/prometheus.yml \
--storage.tsdb.path $DATA_ROOT/prometheus/storage/ \
--web.console.templates=$SOFTWARE_ROOT/prometheus/consoles \
--web.console.libraries=$SOFTWARE_ROOT/prometheus/console_libraries \
--web.enable-admin-api \
--web.enable-lifecycle \
--web.listen-address=:$externalPort \
--query.max-concurrency=$maxConcurrency \
--query.max-samples=$maxSamples \
--query.timeout=${timeout}s > $DATA_ROOT/logs/prometheus.log 2>&1 &
fi
sleep ${restartSeconds}

done &
