#!/usr/bin/env bash

set -e

echo "WARNING: PGBouncer instance is expected to be running on port 6432 and connected to server. Continue (y/n)?"
read choice
case "$choice" in
  y|Y ) echo "Ok, proceeding";;
  n|N ) echo "exiting" && exit 1;;
  * ) echo "invalid" && exit 1;;
esac

if pgrep -x "pgbouncer" > /dev/null
then
    echo "PGBouncer running, OK"
else
    echo "PGBouncer is stopped, please run it"
    exit 1
fi

#suppress pgbench warnings in benchmarks loop
PGHOST=127.0.0.1 PGPORT=6432 $PGINSTALL/bin/pgbench -i postgres

declare -a resultset_sizes=("1" "100" "10000" "10000000")

mkdir tmpbuild

## now loop through the above array
for size in "${resultset_sizes[@]}"
do
    echo "resultset_size $size"
    echo "select generate_series(1,$size);"> tmpbuild/shot.sql
    PGHOST=127.0.0.1 PGPORT=6432 $PGINSTALL/bin/pgbench --no-vacuum -T 10 -c 4 -j 4 -f tmpbuild/shot.sql postgres| grep " = "
done

echo "mixed resultset"
echo "select generate_series(1,(random()*random()*10000)::int);"> tmpbuild/shot.sql
PGHOST=127.0.0.1 PGPORT=6432 $PGINSTALL/bin/pgbench --no-vacuum -T 10 -c 4 -j 4 -f tmpbuild/shot.sql postgres| grep " = "

rm -rf tmpbuild