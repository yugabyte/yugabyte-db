#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

failing_java_test 'TestPgAlterTable#testAddColumnWithNotNullConstraint'
grep_in_java_test \
  "java.lang.AssertionError: Unexpected Error Message. Got: 'ERROR: column \"c\" of relation \"test_table\" contains null values', Expected to contain one of the error messages: 'column \"c\" contains null values'." \
  'TestPgAlterTable#testAddColumnWithNotNullConstraint'
