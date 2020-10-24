#!/bin/bash

# Based on https://gist.github.com/petere/6023944

set -E -e -u -o pipefail 

#
# NOTE: you can control what tests run by setting the TARGETS environment
# variable for a particular branch in the Travis console
#

# You can set this to higher levels for more debug output
#export DEBUG=9
#set -x

BASEDIR=`dirname $0`
if ! . $BASEDIR/util.sh; then
  echo "FATAL: error sourcing $BASEDIR/util.sh" 1>&2
  exit 99
fi
trap err_report ERR

# For sanity sake, ensure that we run from the top level directory
cd "$BASEDIR"/.. || die 3 "Unable to cd to $BASEDIR/.."

export UPGRADE_TO=${UPGRADE_TO:-}
FAIL_FAST=${FAIL_FAST:-}
failed=''
tests_run=0

get_packages() {
    echo "libtap-parser-sourcehandler-pgtap-perl postgresql-$1 postgresql-server-dev-$1"
}
get_path() {
    # See also test/test_MVU.sh
    echo "/usr/lib/postgresql/$1/bin/"
}

# Do NOT use () here; we depend on being able to set failed
test_cmd() {
local status rc
if [ "$1" == '-s' ]; then
    status="$2"
    shift 2
else
    status="$1"
fi

# NOTE! While this script is under tools/, we expect to be running from the main directory

# NOTE: simply aliasing a local variable to "$@" does not work as desired,
# probably because by default the variable isn't an array. If it ever becomes
# an issue we can figure out how to do it.

echo
echo #############################################################################
echo "PG-TRAVIS: running $@"
echo #############################################################################
tests_run=$((tests_run + 1))
# Use || so as not to trip up -e, and a sub-shell to be safe.
rc=0
( "$@" ) || rc=$?
if [ $rc -ne 0 ]; then
    echo
    echo '!!!!!!!!!!!!!!!! FAILURE !!!!!!!!!!!!!!!!'
    echo "$@ returned $rc"
    echo '!!!!!!!!!!!!!!!! FAILURE !!!!!!!!!!!!!!!!'
    echo
    failed="$failed '$status'"
    [ -z "$FAIL_FAST" ] || die 1 "command failed and \$FAIL_FAST is not empty"
fi
}

# Ensure test_cmd sets failed properly
old_FAST_FAIL=$FAIL_FAST
FAIL_FAST=''
test_cmd false > /dev/null # DO NOT redirect stderr, otherwise it's horrible to debug problems here!
if [ -z "$failed" ]; then
    echo "code error: test_cmd() did not set \$failed"
    exit 91
fi
failed=''
FAIL_FAST=$old_FAST_FAIL

test_make() {
    # Many tests depend on install, so just use sudo for all of them
    test_cmd -s "$*" sudo make "$@"
}

########################################################
# TEST TARGETS
sanity() {
    test_make clean regress
}

update() {
    # pg_regress --launcher not supported prior to 9.1
    # There are some other failures in 9.1 and 9.2 (see https://travis-ci.org/decibel/pgtap/builds/358206497).
    echo $PGVERSION | grep -qE "8[.]|9[.][012]" || test_make clean updatecheck
}

tests_run_by_target_all=11 # 1 + 5 * 2
all() {
    local tests_run_start=$tests_run 
    # the test* targets use pg_prove, which assumes it's making a default psql
    # connection to a database that has pgTap installed, so we need to set that
    # up.
    test_cmd -s "all(): psql create extension" psql -Ec 'CREATE EXTENSION pgtap'

    # TODO: install software necessary to allow testing 'html' target
    # UPDATE tests_run_by_target_all IF YOU ADD ANY TESTS HERE!
    for t in all install test test-serial test-parallel ; do
        # Test from a clean slate...
        test_make uninstall clean $t
        # And then test again
        test_make $t
    done
    local commands_run=$(($tests_run - $tests_run_start))
    [ $commands_run -eq $tests_run_by_target_all ] || die 92 "all() expected to run $tests_run_by_target_all but actually ran $commands_run tests"
}

upgrade() {
if [ -n "$UPGRADE_TO" ]; then
    # We need to tell test_MVU.sh to run some steps via sudo since we're
    # actually installing from pgxn into a system directory.  We also use a
    # different port number to avoid conflicting with existing clusters.
    test_cmd test/test_MVU.sh -s 55667 55778 $PGVERSION $UPGRADE_TO "$(get_path $PGVERSION)" "$(get_path $UPGRADE_TO)"
fi
}


########################################################
# Install packages
packages="python-setuptools postgresql-common $(get_packages $PGVERSION)"

if [ -n "$UPGRADE_TO" ]; then
    packages="$packages $(get_packages $UPGRADE_TO)"
fi

sudo apt-get update

# bug: https://www.postgresql.org/message-id/20130508192711.GA9243@msgid.df7cb.de
sudo update-alternatives --remove-all postmaster.1.gz

# stop all existing instances (because of https://github.com/travis-ci/travis-cookbooks/pull/221)
sudo service postgresql stop
# and make sure they don't come back
echo 'exit 0' | sudo tee /etc/init.d/postgresql
sudo chmod a+x /etc/init.d/postgresql

sudo apt-get -o Dpkg::Options::="--force-confdef" -o Dpkg::Options::="--force-confold" install $packages

# Need to explicitly set which pg_config we want to use
export PG_CONFIG="$(get_path $PGVERSION)pg_config"
[ "$PG_CONFIG" != 'pg_config' ]

# Make life easier for test_MVU.sh
sudo usermod -a -G postgres $USER


# Setup cluster
export PGPORT=55435
export PGUSER=postgres
sudo pg_createcluster --start $PGVERSION test -p $PGPORT -- -A trust

sudo easy_install pgxnclient

set +x
total_tests=$((3 + $tests_run_by_target_all))
for t in ${TARGETS:-sanity update upgrade all}; do
    $t
done

# You can use this to check tests that are failing pg_prove
#pg_prove -f --pset tuples_only=1 test/sql/unique.sql test/sql/check.sql || true

if [ $tests_run -eq $total_tests ]; then
    echo Ran $tests_run tests
elif [ $tests_run -gt 0 ]; then
    echo "WARNING! ONLY RAN $tests_run OUT OF $total_tests TESTS!"
    # We don't consider this an error...
else
    echo No tests were run!
    exit 2
fi

if [ -n "$failed" ]; then
    echo
    # $failed will have a leading space if it's not empty
    echo "These test targets failed:$failed"
    exit 1
fi
