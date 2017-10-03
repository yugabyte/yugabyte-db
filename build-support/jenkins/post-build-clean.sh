#!/bin/bash -x
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#
# Script which runs on Jenkins slaves after the build/test to clean up
# disk space used by build artifacts. Our build tends to "make clean"
# before running anyway, so doing the post-build cleanup shouldn't
# hurt our build times. It does, however, save a fair amount of disk
# space in the Jenkins workspace disk. This can help prevent our EC2
# slaves from filling up and causing spurious failures.

ROOT=$(cd $(dirname "$BASH_SOURCE")/../..; pwd)
cd $ROOT

# Note that we use simple shell commands instead of "make clean"
# or "mvn clean". This is more foolproof even if something ends
# up partially compiling, etc.

# Clean up intermediate object files in the src tree
find build/latest/src -name \*.o -exec rm -f {} \;

# Clean up the actual build artifacts
rm -Rf build/latest/bin build/latest/lib

# Clean up any java build artifacts
find java -name \*.jar -delete -o -name \*.class -delete 
