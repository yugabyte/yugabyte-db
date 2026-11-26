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

#include "yb/yql/pgwrapper/logical_replication_test_utils.h"

#include <memory>
#include <optional>

#include "yb/gutil/endian.h"
#include "yb/gutil/strings/join.h"

#include "yb/util/format.h"
#include "yb/util/status_format.h"

using namespace std::chrono_literals;

namespace yb::pgwrapper {

Status StreamSlotAndAcknowledge(
    const std::string& conn_str, const std::string& slot_name, const std::string& publication_name,
    const std::string& needle, MonoDelta timeout, std::vector<std::string>* inserted_rows) {
  const auto replication_conn_str = conn_str + " replication=database";
  PGConnPtr conn(PQconnectdb(replication_conn_str.c_str()));
  SCHECK_EQ(PQstatus(conn.get()), CONNECTION_OK, IllegalState, PQerrorMessage(conn.get()));
  PGResultPtr result(PQexec(
      conn.get(),
      Format(
          "START_REPLICATION SLOT $0 LOGICAL 0/0 (proto_version '1', publication_names '$1')",
          slot_name, publication_name).c_str()));
  SCHECK_EQ(
      PQresultStatus(result.get()), PGRES_COPY_BOTH, IllegalState, PQerrorMessage(conn.get()));

  // XLogData: 'w', start LSN, end LSN, send time, message. Commit: 'C', flags, commit LSN, end
  // LSN, commit time.
  constexpr size_t kXLogDataHeaderSize = 25;
  constexpr size_t kCommitEndLsnOffset = 10;
  bool found = false;
  std::optional<uint64_t> ack_lsn;
  auto deadline = CoarseMonoClock::now() + timeout;
  auto next_ack_time = CoarseMonoClock::now();
  while (CoarseMonoClock::now() < deadline) {
    char* buffer = nullptr;
    auto length = PQgetCopyData(conn.get(), &buffer, /*async=*/1);
    std::unique_ptr<char, decltype(&PQfreemem)> buffer_holder(buffer, &PQfreemem);
    if (length > 0) {
      std::string_view message(buffer, length);
      if (message[0] == 'w' && message.size() > kXLogDataHeaderSize) {
        auto payload = message.substr(kXLogDataHeaderSize);
        if (inserted_rows && payload[0] == 'I') {
          inserted_rows->push_back(VERIFY_RESULT(DecodePgOutputInsert(payload)));
        }
        found = found || payload.find(needle) != std::string_view::npos;
        if (found && !ack_lsn && payload[0] == 'C') {
          ack_lsn = BigEndian::Load64(payload.data() + kCommitEndLsnOffset);
          deadline = CoarseMonoClock::now() + 3s;
        }
      }
    } else if (length == 0) {
      SCHECK_GE(PQconsumeInput(conn.get()), 1, IllegalState, PQerrorMessage(conn.get()));
      SleepFor(10ms);
    } else {
      // Virtual WAL init errors arrive after COPY BOTH mode is entered.
      if (length == -1) {
        result.reset(PQgetResult(conn.get()));
      }
      return STATUS(IllegalState, PQerrorMessage(conn.get()));
    }
    if (ack_lsn && CoarseMonoClock::now() >= next_ack_time) {
      RETURN_NOT_OK(SendStandbyStatusUpdate(conn.get(), *ack_lsn));
      next_ack_time = CoarseMonoClock::now() + 200ms;
    }
  }
  SCHECK_FORMAT(
      ack_lsn, TimedOut, "Slot $0 did not stream a committed change containing $1", slot_name,
      needle);
  return Status::OK();
}

// pgoutput insert: 'I', relation id, 'N', column count, then per column a kind byte and, for 't',
// the value's length and text.
Result<std::string> DecodePgOutputInsert(std::string_view message) {
  constexpr size_t kTupleOffset = 6;
  SCHECK_GE(message.size(), kTupleOffset + 2, IllegalState, "Truncated insert message");
  const auto num_columns = BigEndian::Load16(message.data() + kTupleOffset);
  size_t pos = kTupleOffset + 2;
  std::vector<std::string> values;
  for (uint16_t i = 0; i < num_columns; ++i) {
    SCHECK_LT(pos, message.size(), IllegalState, "Truncated insert message");
    const auto kind = message[pos++];
    if (kind != 't') {
      values.push_back(kind == 'n' ? "NULL" : "UNCHANGED");
      continue;
    }
    SCHECK_LE(pos + 4, message.size(), IllegalState, "Truncated insert message");
    const auto length = BigEndian::Load32(message.data() + pos);
    pos += 4;
    SCHECK_LE(pos + length, message.size(), IllegalState, "Truncated insert message");
    values.emplace_back(message.substr(pos, length));
    pos += length;
  }
  return JoinStrings(values, ",");
}

Status SendStandbyStatusUpdate(PGconn* conn, uint64_t lsn) {
  // 'r', written, flushed and applied LSN, send time since 2000-01-01, reply requested.
  constexpr int64_t kPostgresEpochUnixMicros = 946684800000000;
  char message[34];
  message[0] = 'r';
  BigEndian::Store64(message + 1, lsn);
  BigEndian::Store64(message + 9, lsn);
  BigEndian::Store64(message + 17, lsn);
  BigEndian::Store64(message + 25, GetCurrentTimeMicros() - kPostgresEpochUnixMicros);
  message[33] = 0;
  SCHECK_EQ(PQputCopyData(conn, message, sizeof(message)), 1, IllegalState, PQerrorMessage(conn));
  SCHECK_EQ(PQflush(conn), 0, IllegalState, PQerrorMessage(conn));
  return Status::OK();
}

} // namespace yb::pgwrapper
