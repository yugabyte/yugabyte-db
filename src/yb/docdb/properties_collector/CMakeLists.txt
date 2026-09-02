# Copyright (c) YugabyteDB, Inc.
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

# DocDB-aware RocksDB table-properties collectors. Design: README.md in this directory.
# The library (yb_docdb_properties_collector) is added by the stacked changes that bring its
# sources; it sits below yb_docdb, depending only on dockv / common / util / rocksdb.

set(YB_PCH_PATH ../)
