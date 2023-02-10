#!/usr/bin/env bash

set -e

echo "WARNING: all running instances of postgresql and odyssey will be terminated. Continue (y/n)?"
read choice
case "$choice" in
  y|Y ) echo "Ok, proceeding";;
  n|N ) echo "exiting" && exit 1;;
  * ) echo "invalid" && exit 1;;
esac

if pgrep -x "pgbouncer" > /dev/null
then
    echo "PGBouncer running, please stop it"
    exit 1
else
    echo "PGBouncer is stopped, OK"
fi

if [[ -z $PGSRC ]]; then
  echo "ERROR: \$PGSRC environment variable must point to PostgreSQL source code."
  exit 1
fi

if [[ -z $PGINSTALL ]]; then
  echo "ERROR: \$PGINSTALL environment variable must point to PostgreSQL installation."
  exit 1
fi

if [ -z "$(ls -A $PGSRC)" ]; then
   START_WD=$PWD
   echo "PGSRC dir is empty, aquiring PostgreSQL sources REL_11_STABLE"
   git clone --depth=5 --single-branch --branch=REL_11_STABLE https://github.com/postgres/postgres $PGSRC
   cd $PGSRC
   echo "configure"
   ./configure --prefix=$PGINSTALL --enable-depend > /dev/null
   echo "make"
   make -j4 install>/dev/null

   cd $START_WD
fi

U=`whoami`

soft_cleanup(){
    pkill -9 postgres || true
    rm -rf tmpbuild || true
}

cleanup () {
    echo "Cleanup"
    soft_cleanup
    pkill -9 odyssey || true
}

cleanup
trap cleanup ERR INT TERM

echo "Make temp build"
mkdir tmpbuild
cd tmpbuild
cmake -DCMAKE_BUILD_TYPE=Release ../.. >/dev/null
make -j 4 >/dev/null

echo "Make temp DB"
$PGINSTALL/bin/initdb datadir >/dev/null
$PGINSTALL/bin/pg_ctl -D datadir start >/dev/null

echo "Start Odyssey"
./sources/odyssey ../../odyssey.conf>/dev/null &
cd ..


PGHOST=127.0.0.1 PGPORT=6432 make -C $PGSRC installcheck

echo "Stop Odyssey"
kill %1
soft_cleanup
