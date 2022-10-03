#!/bin/bash

set -e

currentUser="$(whoami)"
INSTALL_ROOT="/home/"$currentUser"/yugabyte"
INSTALL_VERSION_DIR="$1"
containerExposedPort="$2"

#Process name that need to be monitored
process_name="yb-platform"

#Location of pgrep utility
PGREP="/usr/bin/pgrep"

export JAVA_HOME="$INSTALL_VERSION_DIR/jdk8u345-b01"

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

if ! $PGREP -f $process_name >/dev/null 2>&1 ;
then


# restart <process>
$INSTALL_ROOT"/yb-platform/yugaware/bin/yugaware" \
"-Dconfig.file="$INSTALL_ROOT"/yb-platform/conf/yb-platform.conf" \
"-Dhttp.port=disabled" \
"-Dhttps.port=$containerExposedPort" > $INSTALL_ROOT/$process_name/yugaware/bin/platform.log 2>&1 &

fi
done &
