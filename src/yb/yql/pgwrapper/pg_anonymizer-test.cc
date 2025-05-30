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

#include "yb/yql/pgwrapper/libpq_test_base.h"
#include "yb/yql/pgwrapper/libpq_utils.h"

namespace yb::pgwrapper {

namespace {

YB_DEFINE_ENUM(SecurityLabelType, (kFunction)(kValue)(kRole));
YB_DEFINE_ENUM(ObjectType, (kColumn)(kRole));

class SecurityLabel {
 public:
  SecurityLabel(
      ObjectType object_type,
      const std::string& object_name,
      SecurityLabelType label_type,
      std::optional<const std::string> label_value = std::nullopt)
      : object_type_(object_type),
        object_name_(object_name),
        label_(CreateLabel(label_type, label_value)) {}

  std::string GetCommand() const {
    return Format("SECURITY LABEL FOR anon ON $0 $1 IS '$2'",
        ToString(object_type_).erase(0, 1), object_name_, label_);
  }

  std::string GetLabel() const {
    return label_;
  }

 private:
  std::string CreateLabel(
      SecurityLabelType label_type, std::optional<const std::string> label_value) {
    switch (label_type) {
      case SecurityLabelType::kFunction:
        return Format("MASKED WITH FUNCTION $0", label_value);
      case SecurityLabelType::kValue:
        return Format("MASKED WITH VALUE $0", label_value);
      case SecurityLabelType::kRole:
        DCHECK(!label_value.has_value());
        return "MASKED";
    }
    FATAL_INVALID_ENUM_VALUE(SecurityLabelType, label_type);
  }

  const ObjectType object_type_;
  const std::string object_name_;
  const std::string label_;
};

class PgAnonymizerTest : public LibPqTestBase {
 public:
  virtual ~PgAnonymizerTest() = default;

  void SetUp() override {
    LibPqTestBase::SetUp();
    conn_ = ASSERT_RESULT(Connect());
    ASSERT_OK(conn_->Execute("CREATE EXTENSION anon"));
  }

  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->extra_tserver_flags.push_back(
        "--enable_pg_anonymizer=true");
  }

  Status AnonStartup(const std::string& function_name) {
    RETURN_NOT_OK(conn_->StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
    // function_name updates pg_seclabel and is not a DDL, so
    // yb_non_ddl_txn_for_sys_tables_allowed is needed
    RETURN_NOT_OK(conn_->Execute("SET yb_non_ddl_txn_for_sys_tables_allowed = TRUE"));
    auto res = VERIFY_RESULT(conn_->FetchRow<bool>(Format("SELECT $0", function_name)));
    SCHECK_EQ(res, true, IllegalState, Format("$0 returned false", function_name));
    RETURN_NOT_OK(conn_->Execute("SET yb_non_ddl_txn_for_sys_tables_allowed = FALSE"));
    return conn_->CommitTransaction();
  }

 protected:
  std::optional<PGConn> conn_;
};

} // anonymous namespace

TEST_F(PgAnonymizerTest, TestMaskedUser) {
  constexpr auto kTableName = "test_table";
  constexpr auto kMaskedUser = "masked_user";
  constexpr auto kFirstCol = "first_name";
  constexpr auto kSecondCol = "last_name";
  constexpr auto kThirdCol = "phone";
  constexpr auto kFirstName = "John";
  constexpr auto kLastName = "Doe";
  constexpr auto kPhone = "1234567890";

  ASSERT_OK(AnonStartup("anon.start_dynamic_masking()"));

  ASSERT_OK(conn_->ExecuteFormat(
      "CREATE TABLE $0 ($1 TEXT, $2 TEXT, $3 TEXT)",
      kTableName, kFirstCol, kSecondCol, kThirdCol));
  ASSERT_OK(conn_->ExecuteFormat(
      "INSERT INTO $0 VALUES ('$1', '$2', '$3')",
      kTableName, kFirstName, kLastName, kPhone));

  ASSERT_OK(conn_->ExecuteFormat("CREATE ROLE $0 LOGIN", kMaskedUser));
  ASSERT_OK(conn_->ExecuteFormat("GRANT pg_read_all_data TO $0", kMaskedUser));

  SecurityLabel role_label(ObjectType::kRole, kMaskedUser, SecurityLabelType::kRole);
  ASSERT_OK(conn_->Execute(role_label.GetCommand()));

  std::vector<SecurityLabel> column_labels = {
      SecurityLabel(
          ObjectType::kColumn, Format("$0.$1", kTableName, kFirstCol),
          SecurityLabelType::kFunction, "anon.fake_first_name()"),
      SecurityLabel(
          ObjectType::kColumn, Format("$0.$1", kTableName, kSecondCol),
          SecurityLabelType::kFunction, "anon.fake_last_name()"),
      SecurityLabel(
          ObjectType::kColumn, Format("$0.$1", kTableName, kThirdCol),
          SecurityLabelType::kValue, "$$CONFIDENTIAL$$")};

  for (const auto& label : column_labels) {
    ASSERT_OK(conn_->Execute(label.GetCommand()));
  }

  // masked_user should read fake values
  for (auto* ts : cluster_->tserver_daemons()) {
    auto conn = ASSERT_RESULT(ConnectToTsAsUser(*ts, kMaskedUser));
    const auto row = ASSERT_RESULT((conn.FetchRow<std::string, std::string, std::string>(
        Format("SELECT * FROM $0", kTableName))));
    ASSERT_NE(row, (decltype(row){kFirstName, kLastName, kPhone}));
  }

  auto is_unmasked = ASSERT_RESULT(conn_->FetchRow<bool>(
      Format("SELECT anon.unmask_role('$0')", kMaskedUser)));
  ASSERT_EQ(is_unmasked, true);

  // now, we should be able to read the original values
  for (auto* ts : cluster_->tserver_daemons()) {
    auto conn = ASSERT_RESULT(ConnectToTsAsUser(*ts, kMaskedUser));
    const auto row = ASSERT_RESULT((conn.FetchRow<std::string, std::string, std::string>(
        Format("SELECT * FROM $0", kTableName))));
    ASSERT_EQ(row, (decltype(row){kFirstName, kLastName, kPhone}));
  }
}

TEST_F(PgAnonymizerTest, TestCatalogTables) {
  constexpr auto kTableName = "test_table";
  constexpr auto kUser = "test_user";

  ASSERT_OK(AnonStartup("anon.start_dynamic_masking()"));
  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLE $0 (k TEXT, v TEXT)", kTableName));
  ASSERT_OK(conn_->ExecuteFormat("CREATE ROLE $0 LOGIN", kUser));

  SecurityLabel role_label(ObjectType::kRole, kUser, SecurityLabelType::kRole);
  ASSERT_OK(conn_->Execute(role_label.GetCommand()));

  std::vector<SecurityLabel> sec_labels = {
      SecurityLabel(
          ObjectType::kColumn, Format("$0.k", kTableName),
          SecurityLabelType::kFunction, "anon.fake_first_name()"),
      SecurityLabel(
          ObjectType::kColumn, Format("$0.v", kTableName),
          SecurityLabelType::kValue, "$$CONFIDENTIAL$$")};

  for (const auto& label : sec_labels) {
    ASSERT_OK(conn_->Execute(label.GetCommand()));
  }

  auto masked_role = ASSERT_RESULT((conn_->FetchRow<std::string, std::string>(
      "SELECT rolname, label FROM pg_roles JOIN pg_shseclabel ON objoid = oid")));

  // check that the role label is present on pg_shseclabel
  ASSERT_EQ(masked_role, (decltype(masked_role){kUser, role_label.GetLabel()}));

  const auto column_labels = ASSERT_RESULT((conn_->FetchRows<std::string, std::string>(
      "SELECT relname, label FROM pg_class JOIN pg_seclabel ON objoid = oid")));

  // check that the column labels are present on pg_seclabel
  for (size_t i = 0; i < column_labels.size(); ++i) {
    ASSERT_EQ(column_labels[i],
        (decltype(column_labels[i]){kTableName, sec_labels[i].GetLabel()}));
  }
}

// this is a slightly modified cpp version of the python + sql regress test populate.sql
TEST_F(PgAnonymizerTest, TestPopulate) {
  constexpr auto kNumRows = 500;
  constexpr auto kTableName = "anon.email";

  ASSERT_OK(AnonStartup("anon.init()"));
  ASSERT_OK(conn_->ExecuteFormat("TRUNCATE $0", kTableName));

  for (int i = 1; i <= kNumRows; ++i) {
    ASSERT_OK(conn_->ExecuteFormat(
        "INSERT INTO $0 VALUES ($1, 'email$1@example.com')", kTableName, i));
  }

  const auto res = ASSERT_RESULT(conn_->FetchRow<bool>(Format(
      "SELECT count(DISTINCT val) = 500 FROM $0", kTableName)));
  ASSERT_EQ(res, true);
}

} // namespace yb::pgwrapper
