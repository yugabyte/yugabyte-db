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

set(YB_PCH_PREFIX vtables)
set(YB_PCH_DEP_LIBS opid_proto)

set(VTABLE_DEPS
  cdc_consumer_proto
  cdc_service_proto
  consensus_metadata_proto
  consensus_proto
  encryption_proto
  master_proto
  tserver_proto
  tserver_service_proto
  yb_ql_common
  yrpc)

set(VTABLE_SRCS
  yql_aggregates_vtable.cc
  yql_auth_resource_role_permissions_index.cc
  yql_auth_role_permissions_vtable.cc
  yql_auth_roles_vtable.cc
  yql_columns_vtable.cc
  yql_empty_vtable.cc
  yql_functions_vtable.cc
  yql_indexes_vtable.cc
  yql_keyspaces_vtable.cc
  yql_local_vtable.cc
  yql_partitions_vtable.cc
  yql_peers_vtable.cc
  yql_size_estimates_vtable.cc
  yql_tables_vtable.cc
  yql_triggers_vtable.cc
  yql_types_vtable.cc
  yql_views_vtable.cc
  yql_virtual_table.cc
  yql_vtable_helpers.cc
  yql_vtable_iterator.cc)

ADD_YB_LIBRARY(vtables
  SRCS ${VTABLE_SRCS}
  DEPS ${VTABLE_DEPS})
