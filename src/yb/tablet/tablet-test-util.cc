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

#include "yb/tablet/tablet-test-util.h"

#include "yb/qlexpr/ql_expr.h"
#include "yb/common/ql_value.h"

#include "yb/docdb/ql_rowwise_iterator_interface.h"

#include "yb/dockv/reader_projection.h"

#include "yb/gutil/strings/join.h"

#include "yb/tablet/operations/change_metadata_operation.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"

#include "yb/tserver/tserver_admin.pb.h"

#include "yb/util/status_log.h"

using std::string;

DECLARE_bool(enable_data_block_fsync);

namespace yb {
namespace tablet {

YBTabletTest::YBTabletTest(const Schema& schema, TableType table_type)
  : schema_(schema),
    client_schema_(schema),
    table_type_(table_type) {
  const_cast<Schema&>(schema_).InitColumnIdsByDefault();
  // Keep unit tests fast, but only if no one has set the flag explicitly.
  if (google::GetCommandLineFlagInfoOrDie("enable_data_block_fsync").is_default) {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_data_block_fsync) = false;
  }
}

void YBTabletTest::SetUp() {
  YBTest::SetUp();

  SetUpTestTablet();
}

void YBTabletTest::CreateTestTablet(const std::string& root_dir) {
  string dir = root_dir.empty() ? GetTestPath("fs_root") : root_dir;
  TabletHarness::Options opts(dir);
  opts.enable_metrics = true;
  opts.table_type = table_type_;
  bool first_time = harness_ == NULL;
  harness_.reset(new TabletHarness(schema_, opts));
  CHECK_OK(harness_->Create(first_time));
}

void YBTabletTest::SetUpTestTablet(const std::string& root_dir) {
  CreateTestTablet(root_dir);
  CHECK_OK(harness_->Open());
}

void YBTabletTest::AlterSchema(const Schema& schema) {
  ThreadSafeArena arena;
  LWChangeMetadataRequestPB req(&arena);
  req.set_schema_version(tablet()->metadata()->schema_version() + 1);

  ChangeMetadataOperation operation(nullptr, nullptr, &req);
  ASSERT_OK(tablet()->CreatePreparedChangeMetadata(
      &operation, &schema, IsLeaderSide::kTrue));
  ASSERT_OK(tablet()->AlterSchema(&operation));
  operation.Release();
}

Status IterateToStringList(
    docdb::YQLRowwiseIteratorIf* iter, const Schema& schema, std::vector<std::string> *out,
    int limit) {
  out->clear();
  int fetched = 0;
  std::vector<std::pair<QLValue, std::string>> temp;
  qlexpr::QLTableRow row;
  while (VERIFY_RESULT(iter->FetchNext(&row)) && fetched < limit) {
    QLValue key;
    RETURN_NOT_OK(row.GetValue(schema.column_id(0), &key));
    temp.emplace_back(key, row.ToString(schema));
    fetched++;
  }
  std::sort(temp.begin(), temp.end(), [](const auto& lhs, const auto& rhs) {
    return lhs.first < rhs.first;
  });
  for (auto& p : temp) {
    out->push_back(std::move(p.second));
  }
  return Status::OK();
}

// Dump all of the rows of the tablet into the given vector.
Status DumpTablet(const Tablet& tablet, std::vector<std::string>* out) {
  const auto& schema = *tablet.schema();
  dockv::ReaderProjection reader_projection(schema);
  auto iter = tablet.NewRowIterator(reader_projection);
  RETURN_NOT_OK(iter);
  std::vector<string> rows;
  RETURN_NOT_OK(IterateToStringList(iter->get(), schema, &rows));
  std::sort(rows.begin(), rows.end());
  out->swap(rows);
  return Status::OK();
}

} // namespace tablet
} // namespace yb
