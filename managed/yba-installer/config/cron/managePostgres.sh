#!/bin/bash

set -e

currentUser="$(whoami)"

# Take in inputs
SOFTWARE_ROOT="$1"
shift
DATA_ROOT="$1"
shift
restartSeconds="$1"


#Process name that need to be monitored
process_name="postgres"

#Location of pgrep utility
PGREP="/usr/bin/pgrep"

#Initially, on startup do create a testfile to indicate that the process
#need to be monitored. If you dont want the process to be monitored, then
#delete this file or stop this service. This is the infinite while loop for non-root
#monitoring, which we can break out of as needed.
touch $SOFTWARE_ROOT/pgsql/testfile

while true;
do

if [ ! -f $SOFTWARE_ROOT/pgsql/testfile ]; then
        break
fi

if ! $PGREP $process_name >/dev/null 2>&1 ;
then

# restart <process>
$SOFTWARE_ROOT/pgsql/bin/pg_ctl \
-D $SOFTWARE_ROOT/pgsql/conf \
-o "-k $DATA_ROOT/pgsql/run/postgresql/" \
-m smart start > $DATA_ROOT/logs/$process_name.log 2>&1 &
fi
sleep ${restartSeconds}

done &
