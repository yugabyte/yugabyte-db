// Copyright (c) YugabyteDB, Inc.
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

#include "yb/tserver/remote_bootstrap_info.h"

#include "yb/tserver/tserver_admin.pb.h"

#include "yb/util/format.h"
#include "yb/util/size_literals.h"

namespace yb::tserver {

namespace {
std::string BytesPerSecToHumanReadable(size_t bytes_per_sec) {
  if (bytes_per_sec < 1_KB) {
    return Format("$0 B/s", bytes_per_sec);
  } else if (bytes_per_sec < 1_MB) {
    return Format("$0 KiB/s", bytes_per_sec / 1_KB);
  } else if (bytes_per_sec < 1_GB) {
    return Format("$0 MiB/s", bytes_per_sec / 1_MB);
  } else {
    return Format("$0 GiB/s", bytes_per_sec / 1_GB);
  }
}
} // namespace

std::string GetRemoteBootstrapProgressMessage(const GetActiveRbsInfoResponsePB_RbsInfo& resp) {
  auto completion_percent = (resp.sst_bytes_downloaded() * 100) / resp.sst_bytes_to_download();
  std::string rbs_rate = (resp.sst_download_elapsed_sec() == 0) ?
      "N/A" :
      BytesPerSecToHumanReadable(resp.sst_bytes_downloaded() / resp.sst_download_elapsed_sec());
  return Format(
      "$0 bytes of SSTs ($1%) copied in $2s ($3)", resp.sst_bytes_downloaded(),
      completion_percent, resp.sst_download_elapsed_sec(), rbs_rate);
}

} // namespace yb::tserver
