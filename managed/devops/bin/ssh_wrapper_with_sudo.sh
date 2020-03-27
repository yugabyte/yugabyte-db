#!/bin/bash
#
# Copyright 2019 YugaByte, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

new_args=()
while [[ $# -gt 0 ]]; do
 if [[ $1 == 'scp -t '* ]]; then
   new_args+=( "sudo -u yugabyte $1" )
 else
   new_args+=( "$1" )
 fi
 shift
done

ssh "${new_args[@]}"
