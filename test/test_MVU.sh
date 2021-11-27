#!/usr/bin/env bash

# Test performing a Major Version Upgrade via pg_upgrade.
#
# MVU can be problematic due to catalog changes. For example, if the extension
# contains a view that references a catalog column that no longer exists,
# pg_upgrade itself will break.

set -E -e -u -o pipefail 

BASEDIR=`dirname $0`
if ! . $BASEDIR/../tools/util.sh; then
    echo "FATAL: error sourcing $BASEDIR/../tools/util.sh" 1>&2
    exit 99
fi
trap err_report ERR

debug 19 "Arguments: $@"

rc=0

byte_len() (
    [ $# -eq 1 ] || die 99 "Expected 1 argument, not $# ($@)"
    LANG=C LC_ALL=C
    debug 99 "byte_len($@) = ${#1}"
    echo ${#1}
)

check_bin() {
    for f in pg_ctl psql initdb; do
        [ -x "$1/$f" ] || die 1 "$1/$f does not exist or is not executable"
    done
}

# mktemp on OS X results is a super-long path name that can cause problems, ie:
#   connection to database failed: Unix-domain socket path "/private/var/folders/rp/mv0457r17cg0xqyw5j7701892tlc0h/T/test_pgtap_upgrade.upgrade.7W4BLF/.s.PGSQL.50432" is too long (maximum 103 bytes)
#
# This function looks for that condition and replaces the output with something more legible
short_tmpdir() (
    [ $# -eq 1 ] || die 99 "Expected 1 argument, not $# ($@)"
    [ "$TMPDIR" != "" ] || die 99 '$TMPDIR not set'
    out=$(mktemp -p '' -d $1.XXXXXX)
    if echo "$out" | egrep -q '^(/private)?/var/folders'; then
        newout=$(echo "$out" | sed -e "s#.*/$TMPDIR#$TMPDIR#")
        debug 19 "replacing '$out' with '$newout'"
    fi

    debug 9 "$0($@) = $out"
    # Postgres blows up if this is too long. Technically the limit is 103 bytes,
    # but need to account for the socket name, plus the fact that OS X might
    # prepend '/private' to what we return. :(
    [ $(byte_len "$out") -lt 75 ] || die 9 "short_tmpdir($@) returning a value >= 75 bytes ('$out')"
    echo "$out"
)

banner() {
    echo
    echo '###################################'
    echo "$@"
    echo '###################################'
    echo
}

modify_config() (
    # See below for definition of ctl_separator
    if [ -z "$ctl_separator" ]; then
        confDir=$PGDATA
        conf=$confDir/postgresql.conf
        debug 6 "$0: conf = $conf"

        debug 0 "Modifying NATIVE $conf"

        echo "port = $PGPORT" >> $conf
    else
        confDir="/etc/postgresql/$1/$cluster_name"
        conf="$confDir/postgresql.conf"
        debug 6 "$0: confDir = $confDir conf=$conf"
        debug_ls 9 -la $confDir

        debug 0 "Modifying DEBIAN $confDir and $PGDATA"

        debug 2 ln -s $conf $PGDATA/
        ln -s $conf $PGDATA/
        # Some versions also have a conf.d ...
        if [ -e "$confDir/conf.d" ]; then
            debug 2 ln -s $confDir/conf.d $PGDATA/
            ln -s $confDir/conf.d $PGDATA/
        fi
        debug_ls 8 -la $PGDATA

        # Shouldn't need to muck with PGPORT...

        # GUC changed somewhere between 9.1 and 9.5, so read config to figure out correct value
        guc=$(grep unix_socket_director $conf | sed -e 's/^# *//' | cut -d ' ' -f 1)
        debug 4 "$0: guc = $guc"
        echo "$guc = '/tmp'" >> $conf
    fi

    echo "synchronous_commit = off" >> $conf
)

#############################
# Argument processing
keep=''
if [ "$1" == "-k" ]; then
    debug 1 keeping results after exit
    keep=1
    shift
fi

sudo=''
if [ "$1" == '-s' ]; then
    # Useful error if we can't find sudo
    command -v sudo > /dev/null || echo "sudo not found"
    sudo=$(command -v sudo)
    debug 2 "sudo located at $sudo"
    shift
fi

OLD_PORT=$1
NEW_PORT=$2
OLD_VERSION=$3
NEW_VERSION=$4
OLD_PATH="${5:-/usr/lib/postgresql/$OLD_VERSION/bin}"
NEW_PATH="${5:-/usr/lib/postgresql/$NEW_VERSION/bin}"

export PGDATABASE=test_pgtap_upgrade

check_bin "$OLD_PATH"
check_bin "$NEW_PATH"

export TMPDIR=${TMPDIR:-${TEMP:-${TMP:-/tmp}}}
debug 9 "\$TMPDIR=$TMPDIR"
[ $(byte_len "$TMPDIR") -lt 50 ] || die 9 "\$TMPDIR ('$TMPDIR') is too long; please set it" '(or $TEMP, or $TMP) to a value less than 50 bytes'
upgrade_dir=$(short_tmpdir test_pgtap_upgrade.upgrade)
old_dir=$(short_tmpdir test_pgtap_upgrade.old)
new_dir=$(short_tmpdir test_pgtap_upgrade.new)

# Note: if this trap fires and removes the old directories with databases still
# running we'll get a bunch of spew on STDERR. It'd be nice to have a trap that
# knew what databases might actually be running.
exit_trap() {
    # No point in stopping on error in here...
    set +e

    # Force sudo on a debian system (see below)
    [ -z "$ctl_separator" ] || sudo=$(command -v sudo)

    # Attempt to shut down any running clusters, otherwise we'll get log spew
    # when the temporary directories vanish.
    $old_pg_ctl stop > /dev/null 2>&1
    $new_pg_ctl stop > /dev/null 2>&1

    # Do not simply stick this command in the trap command; the quoting gets
    # tricky, but the quoting is also damn critical to make sure rm -rf doesn't
    # hose you if the temporary directory names have spaces in them!
    $sudo rm -rf "$upgrade_dir" "$old_dir" "$new_dir"
}
[ -n "$keep" ] || trap exit_trap EXIT
debug 5 "traps: $(trap -p)"

cluster_name=test_pg_upgrade 
if command -v pg_ctlcluster > /dev/null; then
    # Looks like we're running in a apt / Debian / Ubuntu environment, so use their tooling
    ctl_separator='--'

    # Force socket path to normal for pg_upgrade
    export PGHOST=/tmp

    # And force current user
    export PGUSER=${USER:-$(whoami)}

    old_initdb="$sudo pg_createcluster $OLD_VERSION $cluster_name -u $PGUSER -p $OLD_PORT -d $old_dir -- -A trust"
    old_pg_ctl="$sudo pg_ctlcluster $OLD_VERSION test_pg_upgrade"
    new_initdb="$sudo pg_createcluster $NEW_VERSION $cluster_name -u $PGUSER -p $NEW_PORT -d $new_dir -- -A trust"
    new_pg_ctl="$sudo pg_ctlcluster $NEW_VERSION test_pg_upgrade"

    # See also ../.github/workflows/test.yml
    new_pg_upgrade="/usr/lib/postgresql/$NEW_VERSION/bin/pg_upgrade"
else
    ctl_separator=''
    old_initdb="$(find_at_path "$OLD_PATH" initdb) -D $old_dir -N"
    old_pg_ctl=$(find_at_path "$OLD_PATH" pg_ctl)
    new_initdb="$(find_at_path "$NEW_PATH" initdb) -D $new_dir -N"
    new_pg_ctl=$(find_at_path "$NEW_PATH" pg_ctl)

    new_pg_upgrade=$(find_at_path "$NEW_PATH" pg_upgrade)
fi


##################################################################################################
banner "Creating old version temporary installation at $old_dir on port $OLD_PORT (in the background)"
echo "Creating new version temporary installation at $new_dir on port $NEW_PORT (in the background)"
$old_initdb &
$new_initdb &

echo Waiting...
wait

##################################################################################################
banner "Starting OLD $OLD_VERSION postgres via $old_pg_ctl"
export PGDATA=$old_dir
export PGPORT=$OLD_PORT
modify_config $OLD_VERSION

$old_pg_ctl start $ctl_separator -w # older versions don't support --wait

echo "Creating database"
createdb # Note this uses PGPORT, so no need to wrap.

echo "Installing pgtap"
# If user requested sudo then we need to use it for the install step. TODO:
# it'd be nice to move this into the Makefile, if the PGXS make stuff allows
# it...
$sudo make clean install

banner "Loading extension"
psql -c 'CREATE EXTENSION pgtap' # Also uses PGPORT

echo "Stopping OLD postgres via $old_pg_ctl"
$old_pg_ctl stop $ctl_separator -w # older versions don't support --wait

##################################################################################################
banner "Running pg_upgrade"
export PGDATA=$new_dir
export PGPORT=$NEW_PORT
modify_config $NEW_VERSION

(
    cd $upgrade_dir
    if [ $DEBUG -ge 9 ]; then
        echo $old_dir; ls -la $old_dir; egrep 'director|unix|conf' $old_dir/postgresql.conf
        echo $new_dir; ls -la $new_dir; egrep 'director|unix|conf' $new_dir/postgresql.conf
    fi
    echo $new_pg_upgrade -d "$old_dir" -D "$new_dir" -b "$OLD_PATH" -B "$NEW_PATH"
    $new_pg_upgrade -d "$old_dir" -D "$new_dir" -b "$OLD_PATH" -B "$NEW_PATH" || rc=$?
    if [ $rc -ne 0 ]; then
        # Dump log, but only if we're not keeping the directory
        if [ -z "$keep" ]; then
            for f in `ls *.log`; do
                echo; echo; echo; echo; echo; echo
                echo "`pwd`/$f:"
                cat "$f"
            done
            ls -la
        else
            error "pg_upgrade logs are at $upgrade_dir"
        fi
        die $rc "pg_upgrade returned $rc"
    fi
)

##################################################################################################
banner "Testing UPGRADED cluster"

# Run our tests against the upgraded cluster, but first make sure the old
# cluster is still down, to ensure there's no chance of testing it instead.
# Note that some versions of pg_ctl return different exit codes when the server
# isn't running.
echo ensuring OLD cluster is stopped
rc=0
status=$($old_pg_ctl status) || rc=$?
[ "$status" == 'pg_ctl: no server running' ] || die 3 "$old_pg_ctl status returned '$status' and exited with $?"
debug 4 "$old_pg_ctl status exited with $rc"

# TODO: send log output to a file so it doesn't mix in with STDOUT
echo starting NEW cluster
$new_pg_ctl start $ctl_separator -w || die $? "$new_pg_ctl start $ctl_separator -w returned $?"
$new_pg_ctl status # Should error if not running on most versions

psql -E -c '\dx'
psql -E -c 'SELECT pgtap_version(), pg_version_num(), version();'

# We want to make sure to use the NEW pg_config
export PG_CONFIG=$(find_at_path "$NEW_PATH" pg_config)
[ -x "$PG_CONFIG" ] || ( debug_ls 1 "$NEW_PATH"; die 4 "unable to find executable pg_config at $NEW_PATH" )

# When crossing certain upgrade boundaries we need to exclude some tests
# because the test functions are not available in the previous version.
int_ver() {
    local ver
    ver=$(echo $1 | tr -d .)
    # "multiply" versions less than 7.0 by 10 so that version 10.x becomes 100,
    # 11 becomes 110, etc.
    [ $ver -ge 70 ] || ver="${ver}0"
    echo $ver
}
EXCLUDE_TEST_FILES=''
add_exclude() {
    local old new
    old=$(int_ver $1)
    new=$(int_ver $2)
    shift 2
    if [ $(int_ver $OLD_VERSION) -le $old -a $(int_ver $NEW_VERSION) -ge $new ]; then
        EXCLUDE_TEST_FILES="$EXCLUDE_TEST_FILES $@"
    fi
}

add_exclude 9.1 9.2 test/sql/resultset.sql
add_exclude 9.1 9.2 test/sql/valueset.sql
add_exclude 9.1 9.2 test/sql/throwtap.sql
add_exclude 9.4 9.5 test/sql/policy.sql test/sql/throwtap.sql 
add_exclude 9.6 10 test/sql/partitions.sql

# Use this if there's a single test failing .github/workflows/test.yml that you can't figure out...
#(cd $(dirname $0)/..; pg_prove -v --pset tuples_only=1 test/sql/throwtap.sql)

export EXCLUDE_TEST_FILES
$sudo make clean
make test

if [ -n "$EXCLUDE_TEST_FILES" ]; then
    banner "Rerunning test after a reinstall due to version differences"
    echo "Excluded tests: $EXCLUDE_TEST_FILES"
    export EXCLUDED_TEST_FILES=''

    # Need to build with the new version, then install
    $sudo make install

    psql -E -c 'DROP EXTENSION pgtap; CREATE EXTENSION pgtap;'

    make test
fi
