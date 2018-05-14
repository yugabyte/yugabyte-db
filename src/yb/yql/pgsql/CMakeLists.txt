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

# RPC service / connection.
add_subdirectory(server)

# Entry for all proxy processing tasks.
add_subdirectory(processor)

# Syntax analysis.
add_subdirectory(syn)

# Semantic analysis.
add_subdirectory(sem)
add_subdirectory(ptree)

# Code definition
add_subdirectory(proto)

# Code generation.
add_subdirectory(pbgen)

# Code execution.
add_subdirectory(pbexec)

# Utility.
add_subdirectory(util)
add_subdirectory(ybpostgres)

# Test.
# add_subdirectory(test)
