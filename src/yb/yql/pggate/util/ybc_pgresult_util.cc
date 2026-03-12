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

#include "yb/yql/pggate/util/ybc_pgresult_util.h"

#include <vector>
#include <google/protobuf/io/coded_stream.h>

#include "libpq-fe.h" // NOLINT

#include "yb/common/common.pb.h"

#include "yb/gutil/casts.h"

#include "yb/util/logging.h"

extern "C" void YBCPgSaveMessageField(PGresult* res, char code, const char* value);

namespace yb {

namespace {

void SetPGresultError(PGresult* pgresult, const char* message) {
  YBCPgSaveMessageField(pgresult, PG_DIAG_MESSAGE_PRIMARY, message);
  YBCPgSaveMessageField(pgresult, PG_DIAG_SEVERITY, "ERROR");
  YBCPgSaveMessageField(pgresult, PG_DIAG_SEVERITY_NONLOCALIZED, "ERROR");
}

} // namespace

// References:
// - Encoding Format: https://developers.google.com/protocol-buffers/docs/encoding#structure
// - Structure: [Tag (Varint)] + [Payload Length (Varint)] + [Payload Data]
// - Varint Size Rules: https://developers.google.com/protocol-buffers/docs/encoding#varints
// -- 1 byte for  0-127
// -- 2 bytes for 128-16,383
// - Tag Calculation: (field_number << 3) | wire_type.
// For field IDs 1-15, Tag is 1 byte.
//
// ByteSizeLong() is O(n) where n is the number of elements
// For rows, we will be iterating over the columns twice -
// - once while encoding them
// - once while doing row_pb->ByteSizeLong()
// So the cost of adding rows are O(2n) = O(n) where n is the number of columns
//
// For PgResultPB, if we were to rely on ByteSizeLong(), we would have to
// iterate over all the rows to get the size, for each new row.
// That means, if the cost of adding a single row is 1
// The cost of adding all the rows would be --
// 1 + 2 + 3 + 4 + ... + n = n^2 where n is the number of rows
// That's why we are maintaing the pb size only for PgResultPB, and not rows
bool PgResultToPB(PGresult* pg_result, PgResultPB* result_pb, size_t max_resp_size) {
  result_pb->set_exec_status(PGRES_TUPLES_OK);
  int nrows = PQntuples(pg_result);
  int ncols = PQnfields(pg_result);

  size_t data_size = result_pb->ByteSizeLong();

  for (int row = 0; row < nrows; ++row) {
    auto* row_pb = result_pb->add_rows();
    for (int col = 0; col < ncols; ++col) {
      auto* col_pb = row_pb->add_columns();
      if (!PQgetisnull(pg_result, row, col)) {
        int len = PQgetlength(pg_result, row, col);
        const char* value = PQgetvalue(pg_result, row, col);
        col_pb->set_value(value, len);
      }
    }
    // Each repeated field entry is: tag (varint) + length (varint) + serialized bytes.
    using google::protobuf::io::CodedOutputStream;
    size_t row_size = row_pb->ByteSizeLong();
    constexpr size_t kTagSize = 1;
    size_t length_size = CodedOutputStream::VarintSize32(narrow_cast<uint32_t>(row_size));
    data_size += kTagSize + length_size + row_size;
    if (data_size > max_resp_size) {
      result_pb->mutable_rows()->RemoveLast();
      return false;
    }
  }
  return true;
}

PGresult* PgResultFromPB(const PgResultPB& result_pb) {
  auto exec_status = static_cast<ExecStatusType>(result_pb.exec_status());
  PGresult* pgresult = PQmakeEmptyPGresult(nullptr, exec_status);
  if (!pgresult) {
    return nullptr;
  }

  if (exec_status != PGRES_TUPLES_OK) {
    if (!result_pb.error_message().empty()) {
      SetPGresultError(pgresult, result_pb.error_message().c_str());
    }
    return pgresult;
  }

  int nrows = result_pb.rows_size();
  int ncols = nrows > 0 ? result_pb.rows(0).columns_size() : 0;

  std::vector<PGresAttDesc> attrs(ncols, PGresAttDesc{});
  PQsetResultAttrs(pgresult, ncols, attrs.data());

  for (int row = 0; row < nrows; ++row) {
    const auto& row_pb = result_pb.rows(row);
    for (int col = 0; col < ncols; ++col) {
      const auto& col_pb = row_pb.columns(col);
      if (!col_pb.has_value()) {
        PQsetvalue(pgresult, row, col, nullptr, -1);
      } else {
        PQsetvalue(pgresult, row, col,
                   const_cast<char*>(col_pb.value().data()),
                   static_cast<int>(col_pb.value().size()));
      }
    }
  }
  return pgresult;
}

} // namespace yb

extern "C" {

struct pg_result* YBCPgResultFromPB(const uint8_t* buf, size_t size) {
  if (!buf || size == 0) {
    return nullptr;
  }

  yb::PgResultPB pb;
  if (!pb.ParseFromArray(buf, static_cast<int>(size))) {
    LOG(WARNING) << "Failed to parse PgResultPB from buffer";
    return nullptr;
  }
  return yb::PgResultFromPB(pb);
}

}  // extern "C"
