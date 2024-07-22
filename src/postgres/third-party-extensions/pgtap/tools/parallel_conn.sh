#!/bin/sh

# Find the maximum safe connections for parallel execution

error () {
    echo $@ 1>&2
}
die () {
    error $@
    exit 1
}

[ $# -le 1 ] || die "$0: Invalid number of arguments"

PARALLEL_CONN=$1

if [ -n "$PARALLEL_CONN" ]; then
    [ $PARALLEL_CONN -ge 1 ] 2>/dev/null || die "Invalid value for PARALLEL_CONN ($PARALLEL_CONN)"
    echo $PARALLEL_CONN
    exit
fi

COMMAND="SELECT greatest(1, current_setting('max_connections')::int - current_setting('superuser_reserved_connections')::int - (SELECT count(*) FROM pg_stat_activity) - 2)"

if PARALLEL_CONN=`psql -d ${PGDATABASE:-postgres} -P pager=off -P tuples_only=true -qAXtc "$COMMAND" 2> /dev/null`; then
    if [ $PARALLEL_CONN -ge 1 ] 2>/dev/null; then
        # We know it's a number at this point
        [ $PARALLEL_CONN -eq 1 ] && error "NOTICE: unable to run tests in parallel; not enough connections"
        echo $PARALLEL_CONN
        exit
    fi
fi

error "Problems encountered determining maximum parallel test connections; forcing serial mode"
echo 1
