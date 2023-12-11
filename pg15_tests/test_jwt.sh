#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

java_test 'org.yb.pgsql.TestJWTAuth'
