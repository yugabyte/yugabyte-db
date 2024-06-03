#!/usr/bin/env bash

#
# Copyright (c) YugaByte, Inc.
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
set -e -u -o pipefail

# Exit on purpose to test jenkins test results reporting.
echo "$0"
# List processes running for debugging purposes. This test will only be executed during
# debugging of build support scripts.
ps -f
exit 1
