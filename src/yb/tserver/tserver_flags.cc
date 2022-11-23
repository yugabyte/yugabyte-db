// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

#include "yb/tserver/tserver_flags.h"

#include "yb/util/flags.h"

DEFINE_UNKNOWN_string(tserver_master_addrs, "127.0.0.1:7100",
              "Comma separated addresses of the masters which the "
              "tablet server should connect to. The CQL proxy reads this flag as well to "
              "determine the new set of masters");
TAG_FLAG(tserver_master_addrs, stable);

DEFINE_UNKNOWN_uint64(tserver_master_replication_factor, 0,
    "Number of master replicas. By default it is detected based on tserver_master_addrs option, "
    "but could be specified explicitly together with passing one or more master service domain "
    "name and port through tserver_master_addrs for masters auto-discovery when running on "
    "Kubernetes.");
