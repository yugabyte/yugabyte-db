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

#include <boost/algorithm/string.hpp>

#include "yb/client/yb_table_name.h"
#include "yb/master/master_util.h"
#include "yb/tools/yb-generate_partitions.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"

DEFINE_UNKNOWN_string(master_addresses, "", "Comma-separated list of YB Master server addresses");
DEFINE_UNKNOWN_string(table_name, "", "Name of the table to generate partitions for");
DEFINE_UNKNOWN_string(namespace_name, "", "Namespace of the table");

using std::string;

int main(int argc, char** argv) {
  yb::ParseCommandLineFlags(&argc, &argv, true);
  yb::InitGoogleLoggingSafe(argv[0]);
  if (FLAGS_master_addresses.empty() || FLAGS_table_name.empty() || FLAGS_namespace_name.empty()) {
    LOG (ERROR) << "Need to specify both --master_addresses, --table_name and --namespace_name";
    return 1;
  }

  // Convert table_name to lowercase since we store table names in lowercase.
  string table_name_lower = boost::to_lower_copy(FLAGS_table_name);

  yb::client::YBTableName yb_table_name(yb::master::GetDefaultDatabaseType(FLAGS_namespace_name),
                                        FLAGS_namespace_name,
                                        table_name_lower);
  std::vector<string> master_addresses = { FLAGS_master_addresses };
  yb::tools::YBPartitionGenerator partition_generator(yb_table_name, master_addresses);
  yb::Status s = partition_generator.Init();
  if (!s.ok()) {
    LOG(FATAL) << "Could not initialize partition generator: " << s.ToString();
  }

  for (string line; std::getline(std::cin, line);) {
    // Trim the line.
    boost::algorithm::trim(line);

    // Lookup tablet id.
    string tablet_id;
    string partition_key;
    s = partition_generator.LookupTabletId(line, &tablet_id, &partition_key);
    if (!s.ok()) {
      LOG (FATAL) << "Error parsing line: " << line << ", error: " << s.ToString();
    }

    // Output the tablet id.
    std::cout << tablet_id << "\t" << line << std::endl;
  }
  return 0;
}
