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

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "yb/gutil/casts.h"

#include "yb/util/status_format.h"
#include "yb/util/test_util.h"
#include "yb/util/write_buffer.h"

#include "yb/yql/pggate/pg_global_view_read.h"

namespace yb::pggate {

namespace {

// One row of cells; nullopt means SQL NULL.
using Row = std::vector<std::optional<std::string>>;

size_t EncodedRowSize(const Row& row) {
  size_t size = 0;
  for (const auto& cell : row) {
    size += PgWireDataHeader::kSerializedSize;
    if (cell) {
      // The cell carries its NUL terminator.
      size += sizeof(uint64_t) + cell->size() + 1;
    }
  }
  return size;
}

// A cell as EncodeGvRow takes it: the value plus its NUL terminator, as libpq
// hands text values to the tserver.
std::optional<Slice> MakeCell(const std::optional<std::string>& value) {
  if (!value) {
    return std::nullopt;
  }
  return Slice(value->c_str(), value->size() + 1);
}

struct EncodeResult {
  bool complete;
  int num_rows;
  std::string data;
};

EncodeResult Encode(int num_cols, const std::vector<Row>& rows, size_t max_size) {
  // Small block size so appends span multiple blocks.
  WriteBuffer buffer(16);
  std::vector<std::optional<Slice>> cells(num_cols);
  int num_rows = 0;
  for (const auto& row : rows) {
    CHECK_EQ(row.size(), make_unsigned(num_cols));
    for (int col = 0; col < num_cols; ++col) {
      cells[col] = MakeCell(row[col]);
    }
    if (!EncodeGvRow(cells, EncodedRowSize(row), &buffer, max_size)) {
      break;
    }
    ++num_rows;
  }
  return {num_rows == narrow_cast<int>(rows.size()), num_rows, buffer.ToBuffer()};
}

// Decodes data and checks it holds the expected rows and nothing more.
Status CheckDecodedRows(const std::string& data, int num_cols, const std::vector<Row>& expected) {
  Slice cursor(data);
  std::vector<const char*> values(num_cols);
  for (const auto& expected_row : expected) {
    RETURN_NOT_OK(DecodeGvRowValues(&cursor, values));
    for (int col = 0; col < num_cols; ++col) {
      const auto& cell = expected_row[col];
      if (cell) {
        SCHECK_FORMAT(values[col] != nullptr, IllegalState,
                      "Unexpected NULL in column $0", col);
        SCHECK_FORMAT(*cell == values[col], IllegalState,
                      "Column $0: expected '$1', got '$2'", col, *cell, values[col]);
      } else {
        SCHECK_FORMAT(values[col] == nullptr, IllegalState,
                      "Expected NULL in column $0, got '$1'", col, values[col]);
      }
    }
  }
  SCHECK_FORMAT(cursor.empty(), IllegalState,
                "Trailing bytes after last row: $0", cursor.size());
  return Status::OK();
}

// One encoded column, as the pg_doc_data writers emit it. Decode tests corrupt
// its bytes. nullopt gives a NULL column. Any other value must carry its own NUL
// terminator, since WriteBinaryColumn adds none.
std::string EncodeCell(const std::optional<Slice>& value) {
  WriteBuffer buffer(16);
  if (value) {
    WriteBinaryColumn(*value, &buffer);
  } else {
    WriteNullColumn(&buffer);
  }
  return buffer.ToBuffer();
}

} // namespace

class PgGlobalViewReadTest : public YBTest {};

TEST_F(PgGlobalViewReadTest, RoundTrip) {
  const int kNumCols = 3;
  const std::vector<Row> rows = {
      {"1", "hello world", "3.14"},
      {std::nullopt, "", "with\nnewline"},
      // UTF-8 values (emoji, accented e): lengths count bytes.
      {"2", "\xF0\x9F\x98\x80", "caf\xC3\xA9"},
      {"0", std::nullopt, std::nullopt},
  };
  size_t total_size = 0;
  for (const auto& row : rows) {
    total_size += EncodedRowSize(row);
  }

  auto encoded = Encode(kNumCols, rows, /* max_size= */ 1024 * 1024);
  ASSERT_TRUE(encoded.complete);
  ASSERT_EQ(encoded.num_rows, 4);
  ASSERT_EQ(encoded.data.size(), total_size);
  ASSERT_OK(CheckDecodedRows(encoded.data, kNumCols, rows));
}

TEST_F(PgGlobalViewReadTest, EmptyResult) {
  auto encoded = Encode(/* num_cols= */ 2, /* rows= */ {}, /* max_size= */ 1024);
  ASSERT_TRUE(encoded.complete);
  ASSERT_EQ(encoded.num_rows, 0);
  ASSERT_TRUE(encoded.data.empty());
}

TEST_F(PgGlobalViewReadTest, TruncationAtRowBoundary) {
  const int kNumCols = 1;
  const Row row = {"aaaa"};
  // 1 (header) + 8 (length) + 4 (value) + 1 (NUL) = 14.
  const auto kRowSize = EncodedRowSize(row);
  ASSERT_EQ(kRowSize, 14);
  const std::vector<Row> rows(5, row);

  // Three rows fit. A row drops only if it would push the buffer past max_size.
  auto encoded = Encode(kNumCols, rows, 3 * kRowSize);
  ASSERT_FALSE(encoded.complete);
  ASSERT_EQ(encoded.num_rows, 3);
  ASSERT_EQ(encoded.data.size(), 3 * kRowSize);
  // The buffer holds whole rows only.
  ASSERT_OK(CheckDecodedRows(encoded.data, kNumCols, {rows.begin(), rows.begin() + 3}));

  // One byte short of three rows drops the third row, not part of it.
  encoded = Encode(kNumCols, rows, 3 * kRowSize - 1);
  ASSERT_FALSE(encoded.complete);
  ASSERT_EQ(encoded.num_rows, 2);
  ASSERT_EQ(encoded.data.size(), 2 * kRowSize);
  ASSERT_OK(CheckDecodedRows(encoded.data, kNumCols, {rows.begin(), rows.begin() + 2}));

  // An exact fit of all rows is not a truncation.
  encoded = Encode(kNumCols, rows, 5 * kRowSize);
  ASSERT_TRUE(encoded.complete);
  ASSERT_EQ(encoded.num_rows, 5);
  ASSERT_OK(CheckDecodedRows(encoded.data, kNumCols, rows));

  // If even the first row does not fit, the buffer stays empty.
  encoded = Encode(kNumCols, rows, kRowSize - 1);
  ASSERT_FALSE(encoded.complete);
  ASSERT_EQ(encoded.num_rows, 0);
  ASSERT_TRUE(encoded.data.empty());
}

// Checks the decoder against the pg_doc_data writers, not against EncodeGvRow.
TEST_F(PgGlobalViewReadTest, DecodeNullColumn) {
  const auto data = EncodeCell(std::nullopt) + EncodeCell(Slice("ok", 3));

  Slice cursor(data);
  const char* values[2] = {"garbage", "garbage"};
  ASSERT_OK(DecodeGvRowValues(&cursor, values));
  ASSERT_EQ(values[0], nullptr);
  ASSERT_STREQ(values[1], "ok");
  ASSERT_TRUE(cursor.empty());
}

TEST_F(PgGlobalViewReadTest, DecodeTruncatedLengthPrefix) {
  // Only 2 of the length's 8 bytes survive.
  auto data = EncodeCell(Slice("x", 2));
  data.resize(PgWireDataHeader::kSerializedSize + 2);

  Slice cursor(data);
  const char* value = nullptr;
  ASSERT_NOK(DecodeGvRowValues(&cursor, {&value, 1}));

  // An empty cursor fails the same way.
  Slice empty_cursor;
  ASSERT_NOK(DecodeGvRowValues(&empty_cursor, {&value, 1}));
}

TEST_F(PgGlobalViewReadTest, DecodeTruncatedPayload) {
  // The length claims 5 bytes, but only 4 follow.
  auto data = EncodeCell(Slice("abcd", 5));
  data.pop_back();

  Slice cursor(data);
  const char* value = nullptr;
  ASSERT_NOK(DecodeGvRowValues(&cursor, {&value, 1}));
}

TEST_F(PgGlobalViewReadTest, DecodeMissingNulTerminator) {
  // Correct total size, but the last byte is not NUL.
  const auto data = EncodeCell(Slice("abcX", 4));

  Slice cursor(data);
  const char* value = nullptr;
  ASSERT_NOK(DecodeGvRowValues(&cursor, {&value, 1}));
}

TEST_F(PgGlobalViewReadTest, DecodeZeroLength) {
  // A length of 0 cannot hold the NUL terminator.
  const auto data = EncodeCell(Slice());

  Slice cursor(data);
  const char* value = nullptr;
  ASSERT_NOK(DecodeGvRowValues(&cursor, {&value, 1}));
}

}  // namespace yb::pggate
