#!/bin/bash
#
# A shell script to load some pre generated data file to a DB using ldb tool
# ./ldb needs to be avaible to be executed.
#
# Usage: <SCRIPT> <input_data_path> <DB Path>

#
# The following only applies to changes made to this file as part of YugaByte development.
#
# Portions Copyright (c) YugaByte, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
# in compliance with the License.  You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software distributed under the License
# is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
# or implied.  See the License for the specific language governing permissions and limitations
# under the License.
#
if [ "$#" -lt 2 ]; then
  echo "usage: $BASH_SOURCE <input_data_path> <DB Path>"
  exit 1
fi

input_data_dir=$1
db_dir=$2
rm -rf $db_dir

echo == Loading data from $input_data_dir to $db_dir

declare -a compression_opts=("no" "snappy" "zlib" "bzip2")

set -e

n=0

for f in `ls -1 $input_data_dir`
do
  echo == Loading $f with compression ${compression_opts[n % 4]}
  ./ldb load --db=$db_dir --compression_type=${compression_opts[n % 4]} --bloom_bits=10 --auto_compaction=false --create_if_missing < $input_data_dir/$f
  let "n = n + 1"
done
