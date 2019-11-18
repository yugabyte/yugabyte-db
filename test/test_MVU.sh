#!/usr/bin/env bash

# Test performing a Major Version Upgrade via pg_upgrade.
#
# MVU can be problematic due to catalog changes. For example, if the extension
# contains a view that references a catalog column that no longer exists,
# pg_upgrade itself will break.

set -E -e -u -o pipefail 

rc=0

# ###########
# TODO: break these functions into a library shell script so they can be used elsewhere

err_report() {
  echo "errexit on line $(caller)" >&2
}
trap err_report ERR

error() {
    echo "$@" >&2
}

die() {
    local rc # Kinda silly here, but be safe...
    rc=$1
    shift
    error "$@"
    exit $rc
}

DEBUG=${DEBUG:-0}
debug() {
    local level
    level=$1
    shift
    [ $level -gt $DEBUG ] || error "$@"
}

debug_do() {
    local level
    level=$1
    shift
    [ $level -gt $DEBUG ] || ( "$@" )
}

debug_ls() {
# Reverse test since we *exit* if we shouldn't debug! Also, note that unlike
# `exit`, `return` does not default to 0.
[ $1 -le $DEBUG ] || return 0
(
level=$1
shift

# Look through each argument and see if more than one exist. If so, we don't
# need to print what it is we're listing.
location=''
for a in "$@"; do
    if [ -e "$a" ]; then
        if [ -n "$location" ]; then
            location=''
            break
        else
            location=$a
        fi
    fi
done

error # blank line
[ -z "$location" ] || error "$location"
ls "$@" >&2
)
}

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
# This function looks for that condition and replaces the output with something more sane
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

find_at_path() (
export PATH="$1:$PATH" # Unfortunately need to maintain old PATH to be able to find `which` :(
out=$(which $2)
[ -n "$out" ] || die 2 "unable to find $2"
echo $out
)

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
    which sudo > /dev/null
    sudo=$(which sudo)
    debug 2 "sudo located at $sudo"
    shift
fi

OLD_PORT=$1
NEW_PORT=$2
OLD_VERSION=$3
NEW_VERSION=$4
OLD_PATH=$5
NEW_PATH=$6

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
    [ -z "$ctl_separator" ] || sudo=$(which sudo)

    # Do not simply stick this command in the trap command; the quoting gets
    # tricky, but the quoting is also damn critical to make sure rm -rf doesn't
    # hose you if the temporary directory names have spaces in them!
    $sudo rm -rf "$upgrade_dir" "$old_dir" "$new_dir"
}
[ -n "$keep" ] || trap exit_trap EXIT
debug 5 "traps: $(trap -p)"

cluster_name=test_pg_upgrade 
if which pg_ctlcluster > /dev/null 2>&1; then
    # Looks like we're running in a apt / Debian / Ubuntu environment, so use their tooling
    ctl_separator='--'

    # Force socket path to normal for pg_upgrade
    export PGHOST=/tmp

    # And force current user
    export PGUSER=$USER

    old_initdb="sudo pg_createcluster $OLD_VERSION $cluster_name -u $USER -p $OLD_PORT -d $old_dir -- -A trust"
    new_initdb="sudo pg_createcluster $NEW_VERSION $cluster_name -u $USER -p $NEW_PORT -d $new_dir -- -A trust"
    old_pg_ctl="sudo pg_ctlcluster $PGVERSION test_pg_upgrade"
    new_pg_ctl=$old_pg_ctl
    # See also ../pg-travis-test.sh
    new_pg_upgrade=/usr/lib/postgresql/$NEW_VERSION/bin/pg_upgrade
else
    ctl_separator=''
    old_initdb="$(find_at_path "$OLD_PATH" initdb) -D $old_dir -N"
    new_initdb="$(find_at_path "$NEW_PATH" initdb) -D $new_dir -N"
    # s/initdb/pg_ctl/g
    old_pg_ctl=$(find_at_path "$OLD_PATH" pg_ctl)
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
( cd $(dirname $0)/.. && $sudo make clean install )


banner "Loading extension"
psql -c 'CREATE EXTENSION pgtap' # Also uses PGPORT

echo "Stopping OLD postgres via $old_pg_ctl"
$old_pg_ctl stop $ctl_separator -w # older versions don't support --wait



##################################################################################################
banner "Running pg_upgrade"
export PGDATA=$new_dir
export PGPORT=$NEW_PORT
modify_config $NEW_VERSION

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



# TODO: turn this stuff on. It's pointless to test via `make regress` because
# that creates a new install; need to figure out the best way to test the new
# cluster
exit
##################################################################################################
banner "Testing UPGRADED cluster"

# Run our tests against the upgraded cluster, but first make sure the old
# cluster is still down, to ensure there's no chance of testing it instead.
status=$($old_pg_ctl status) || die 3 "$old_pg_ctl status exited with $?"
debug 1 "$old_pg_ctl status returned $status"
[ "$status" == 'pg_ctl: no server running' ] || die 3 "old cluster is still running

$old_pg_ctl status returned
$status"
( cd $(dirname $0)/..; $sudo make clean regress )
