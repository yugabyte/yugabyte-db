#!/bin/bash

SCRIPT_PATH=$(
  cd "$(dirname "${BASH_SOURCE[0]}")" || exit 1
  pwd -P
)

unset FAILED;

cd "${SCRIPT_PATH}/../test/regression/sql" || exit 1

for f in *.sql;
do
    if ! grep "${f%.*}" "${SCRIPT_PATH}"/../test/regression/schedule >/dev/null 2>&1;
    then
        FAILED=1;
        echo "File '$f' was not declared in the schedule";
    fi
done

if [[ ${FAILED} -eq 1 ]];
then
    echo "Some files were not declared in the schedule, please update the schedule file";
    exit 1;
fi
