#!/bin/bash

set -e

currentUser="$(whoami)"
INSTALL_ROOT="/home/"$currentUser"/yugabyte"

#Process name that need to be monitored
process_name="postgres"

#Location of pgrep utility
PGREP="/usr/bin/pgrep"

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
$INSTALL_ROOT"/postgres/bin/pg_ctl" \
"-D$INSTALL_ROOT/postgres/data" \
"-o \"-k $INSTALL_ROOT/postgres/run/postgresql/\"" \
-m smart start > $INSTALL_ROOT/$process_name/logfile 2>&1 &
fi

done &
