#!/bin/bash

set -e

currentUser="$(whoami)"
SOFTWARE_ROOT="$1"
shift
DATA_ROOT="$1"
shift
containerExposedPort="$1"
shift
restartSeconds="$1"

#Process name that need to be monitored
process_name="yb-platform"

#Location of pgrep utility
PGREP="/usr/bin/pgrep"

export JAVA_HOME="$SOFTWARE_ROOT/yba_installer/jdk-17.0.7+7"

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

if ! $PGREP -f $process_name >/dev/null 2>&1 ;
then


# restart <process>
$SOFTWARE_ROOT/yb-platform/yugaware/bin/yugaware \
-Dconfig.file=$SOFTWARE_ROOT/yb-platform/conf/yb-platform.conf \
-Dhttp.port=disabled \
-Dhttps.port=$containerExposedPort > $SOFTWARE_ROOT/$process_name/yugaware/bin/platform.log 2>&1 &

fi
sleep ${restartSeconds}
done &
