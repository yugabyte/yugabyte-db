#!/bin/bash

set -e

currentUser="$(whoami)"
INSTALL_ROOT="/home/"$currentUser"/yugabyte"

#Process name that need to be monitored
process_name="prometheus"

#Location of pgrep utility
PGREP="/usr/bin/pgrep"

externalPort="$1"
maxConcurrency="$2"
maxSamples="$3"
timeout="$4"

#Initially, on startup do create a testfile to indicate that the process
#need to be monitored. If you dont want the process to be monitored, then
#delete this file or stop this service. This is the infinite while loop for non-root
#monitoring, which we can break out of as needed.
touch $INSTALL_ROOT/$process_name/testfile

while true;
do

if [ ! -f $INSTALL_ROOT/$process_name/testfile ]; then
        break
fi

if ! $PGREP $process_name >/dev/null 2>&1 ;
then

# restart <process>
$INSTALL_ROOT"/prometheus/bin/prometheus" \
"--config.file" $INSTALL_ROOT"/prometheus/conf/prometheus.yml" \
"--storage.tsdb.path" $INSTALL_ROOT"/prometheus/storage/" \
"--web.console.templates="$INSTALL_ROOT"/prometheus/consoles" \
"--web.console.libraries="$INSTALL_ROOT"/prometheus/console_libraries" \
"--web.enable-admin-api" \
"--web.enable-lifecycle" \
"--web.listen-address=:$externalPort" \
"--query.max-concurrency=$maxConcurrency" \
"--query.max-samples=$maxSamples" \
"--query.timeout=$timeout""s" > $INSTALL_ROOT/$process_name/bin/prometheus.log 2>&1 &
fi

done &
