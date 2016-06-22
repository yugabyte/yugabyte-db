// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
#ifndef YB_CLIENT_TABLE_CREATOR_INTERNAL_H
#define YB_CLIENT_TABLE_CREATOR_INTERNAL_H

#include <string>
#include <vector>

#include "yb/client/client.h"
#include "yb/common/common.pb.h"
#include "yb/master/master.pb.h"

namespace yb {

namespace client {

class YBTableCreator::Data {
 public:
  explicit Data(YBClient* client);
  ~Data();

  YBClient* client_;

  std::string table_name_;

  TableType table_type_;

  const YBSchema* schema_;

  std::vector<const YBPartialRow*> split_rows_;

  PartitionSchemaPB partition_schema_;

  int num_replicas_;

  std::vector<master::PlacementBlockPB> placement_blocks_;

  MonoDelta timeout_;

  bool wait_;

  DISALLOW_COPY_AND_ASSIGN(Data);
};

} // namespace client
} // namespace yb

#endif
