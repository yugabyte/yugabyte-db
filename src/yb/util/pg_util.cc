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

#include "yb/util/pg_util.h"

#include <algorithm>
#include <string>

#include "yb/util/logging.h"

#include "yb/util/format.h"
#include "yb/util/hash_util.h"

DECLARE_string(tmp_dir);

namespace yb {

// Derive a socket directory name for tserver-postgres authentication purposes.
// The directory should be unique to each tserver (and its postgres) running on the same machine.
// Accomplish this by putting the bind address host and port in the directory name.  To avoid the
// 107 character limit for the socket path (see UNIXSOCK_PATH_BUFLEN - 1 in postgres fe-connect.c),
// truncate and use a hash of the bind address if needed.
//
// The derived path comprises of three main components:
// <prefix> - $FLAGS_tmp_dir/.yb or /tmp/.yb as fallback
// <host>   - the host_address or <truncated_host_address>#<host_hash> if truncated
// <port>   - the port
//
// <prefix> and <host> are separated by a dot (.)
// <host> and <port> are separated by a colon (:)
// Hence the overall structure is <prefix>.<host>:<port>
std::string PgDeriveSocketDir(const HostPort& host_port) {
  const std::string& host = host_port.host();
  const uint16_t port = host_port.port();

  constexpr size_t kSocketMaxChars = 107;
  // "/.yb";
  constexpr size_t kYbSuffixForPrefixChars = 4;
  std::string prefix = FLAGS_tmp_dir + "/.yb";
  // Hash (64-bit uint) can only be at most 20 digits long, plus 1 character for pound(#).
  constexpr size_t kMinHostCharsWithHash = 21;

  // Port (16-bit int) can only be at most 5 digits long.
  constexpr size_t kPortMaxChars = 5;
  DCHECK_LE(std::to_string(port).size(), kPortMaxChars);
  // ".s.PGSQL.<port>" = 9 chars + max 5 chars
  constexpr size_t kSocketFileChars = 14;
  DCHECK_EQ(strlen(".s.PGSQL.") + kPortMaxChars, kSocketFileChars);
  // directory name: 1 dot, 1 colon; 1 slash separating socket dir and socket file.
  constexpr size_t kSeparatorChars = 3;
  constexpr size_t kMaxTmpDirChars = 60;
  DCHECK_EQ(
      kSocketMaxChars - (kMinHostCharsWithHash + kPortMaxChars + kSocketFileChars +
                         kSeparatorChars + kYbSuffixForPrefixChars),
      kMaxTmpDirChars);

  // Check if we need to fallback to the /tmp directory. We fallback to /tmp directory if there
  // isn't enough space for port, separators, socket file name & ('#' + hash_of_host).
  if (FLAGS_tmp_dir.size() > kMaxTmpDirChars) {
    LOG(WARNING)
        << "Failed to use " << FLAGS_tmp_dir
        << " as the socket directory path for tserver-postgres authentication purposes. The temp "
        << "directory path must be <= 60 characters. Falling back to the default /tmp path.";
    prefix = "/tmp/.yb";
  }

  size_t prefix_chars = prefix.size();
  size_t host_max_chars =
      kSocketMaxChars - (prefix_chars + kPortMaxChars + kSeparatorChars + kSocketFileChars);
  DCHECK_GE(host_max_chars, kMinHostCharsWithHash);

  // Make socket directory path.
  std::string path;
  if (host.size() <= host_max_chars) {
    // Without host truncation: "$FLAGS_tmp_dir/.yb.<host>:<port>" or "/tmp/.yb.<host>:<port>" in
    // case of fallback.
    path = Format("$0.$1:$2", prefix, host, port);
  } else {
    // With host truncation: "$FLAGS_tmp_dir/.yb.<truncated_host>#<host_hash>:<port>" or
    // "/tmp/.yb.<truncated_host>#<host_hash>:<port>" in case of fallback.
    const uint64_t hash = HashUtil::MurmurHash2_64(host.c_str(), host.size(), 0 /* seed */);
    // Hash (64-bit uint) can only be at most 20 digits long.
    constexpr size_t kHashMaxChars = 20;
    DCHECK_LE(std::to_string(hash).size(), kHashMaxChars);
    // directory name: 1 dot, 1 pound, 1 colon; 1 slash separating socket dir and socket file.
    constexpr size_t kSeparatorCharsWithPound = kSeparatorChars + 1;
    size_t truncated_host_max_chars =
        kSocketMaxChars - (prefix_chars + kPortMaxChars + kSeparatorCharsWithPound +
                           kSocketFileChars + kHashMaxChars);
    DCHECK_GE(truncated_host_max_chars, 0);
    const std::string& host_substring = host.substr(0, truncated_host_max_chars);
    path = Format("$0.$1#$2:$3", prefix, host_substring, hash, port);
  }
  DCHECK_LE(path.size(), kSocketMaxChars);
  return path;
}

} // namespace yb
