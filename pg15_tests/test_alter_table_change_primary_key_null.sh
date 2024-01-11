#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

failing_java_test 'TestPgAlterTableChangePrimaryKey#nulls'
grep_in_java_test \
  "java.lang.AssertionError: Unexpected Error Message. Got: 'ERROR: Missing/null value for primary key column', Expected to contain one of the error messages: 'column \"id\" of relation \"nopk\" contains null values'." \
  'TestPgAlterTableChangePrimaryKey#nulls'
