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
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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
#ifndef YB_CLIENT_TABLE_INTERNAL_H_
#define YB_CLIENT_TABLE_INTERNAL_H_

#include <string>

#include "yb/common/index.h"
#include "yb/common/partition.h"
#include "yb/client/client.h"

namespace yb {

namespace client {

struct YBTable::Info {
  YBTableName table_name;
  std::string table_id;
  std::string indexed_table_id;
  YBSchema schema;
  PartitionSchema partition_schema;
  IndexMap index_map;
};

class YBTable::Data {
 public:
  Data(std::shared_ptr<YBClient> client, Info info);
  ~Data();

  CHECKED_STATUS Open();

  std::shared_ptr<YBClient> client_;
  YBTableType table_type_;
  const Info info_;
  std::vector<std::string> partitions_;

 private:
  DISALLOW_COPY_AND_ASSIGN(Data);
};

}  // namespace client
}  // namespace yb

#endif  // YB_CLIENT_TABLE_INTERNAL_H_
