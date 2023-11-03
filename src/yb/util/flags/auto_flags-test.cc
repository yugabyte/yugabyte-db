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

#include "yb/util/flags/auto_flags_util.h"
#include "yb/util/flags.h"
#include "yb/util/test_util.h"

using std::string;
using std::vector;

DEFINE_RUNTIME_AUTO_int32(test_auto_flag, kLocalVolatile, 0, 100, "Testing");
DEFINE_RUNTIME_AUTO_bool(test_auto_bool, kLocalPersisted, false, true, "Testing!");
DEFINE_RUNTIME_AUTO_int32(test_auto_int32, kExternal, 1, 2, "Testing!");
DEFINE_RUNTIME_AUTO_int64(test_auto_int64, kExternal, 1, 2, "Testing!");
DEFINE_RUNTIME_AUTO_uint64(test_auto_uint64, kExternal, 1, 2, "Testing!");
DEFINE_RUNTIME_AUTO_double(test_auto_double, kExternal, 1, 2, "Testing!");
DEFINE_RUNTIME_AUTO_string(test_auto_string, kExternal, "false", "true", "Testing!");

// Static Assert test flags. These should fail to compile.
// DEFINE_RUNTIME_AUTO_int32(test_auto_flag, kExternal, 100, 1, "Testing"); // Duplicate flag
// DEFINE_RUNTIME_AUTO_bool(test_auto_bool, kExternal, 10, true, "Testing!"); // Initial value
// incompatible
// DEFINE_RUNTIME_AUTO_bool(test_auto_bool, kExternal, false, "true", "Testing!"); // Target value
// incompatible
// DEFINE_RUNTIME_AUTO_string(test_auto_string, kExternal, 1, "test", "Testing!"); // Initial value
// incompatible String
// DEFINE_RUNTIME_AUTO_bool(test_auto_string, kExternal, "test", true, "Testing!"); // Target value
// incompatible String
// DEFINE_RUNTIME_AUTO_bool(test_auto_bool, kExternal, true, true, "Testing!"); // Initial and
// Target are same
// DEFINE_RUNTIME_AUTO_string(test_auto_string, kExternal, "test", "test", "Testing!"); // Initial
// and Target are same String

DISABLE_PROMOTE_ALL_AUTO_FLAGS_FOR_TEST;

namespace yb {

const string kFlagName = "test_auto_flag";
const string kFlagNameArg = "--test_auto_flag";
const string kPromoteAllAutoFlagsArg = "--TEST_promote_all_auto_flags";

namespace {

void VerifyFlagDefault(const int expected_val) {
  gflags::CommandLineFlagInfo flags;
  ASSERT_TRUE(GetCommandLineFlagInfo(kFlagName.c_str(), &flags));
  ASSERT_EQ(flags.default_value, ToString(expected_val));
}

void ParseCommandLineFlags(vector<string> arguments) {
  char arg0[] = "";
  int argc = static_cast<int>(arguments.size()) + 1;
  char* argv[argc];
  argv[0] = arg0;
  for (int i = 0; i < argc - 1; i++) {
    argv[i + 1] = arguments[i].data();
  }

  char** argv_ptr = argv;
  yb::ParseCommandLineFlags(&argc, &argv_ptr, true /* remove_flags */);
}

}  // namespace

TEST(AutoFlagsTest, TestPromote) {
  ASSERT_EQ(FLAGS_test_auto_flag, 0);
  VerifyFlagDefault(0);

  ASSERT_EQ(FLAGS_test_auto_bool, false);
  ASSERT_EQ(FLAGS_test_auto_int32, 1);
  ASSERT_EQ(FLAGS_test_auto_int64, 1);
  ASSERT_EQ(FLAGS_test_auto_uint64, 1);
  ASSERT_EQ(FLAGS_test_auto_double, 1);
  ASSERT_EQ(FLAGS_test_auto_string, "false");

  const auto* flag_desc = GetAutoFlagDescription(kFlagName);
  ASSERT_NO_FATALS(PromoteAutoFlag(*flag_desc));
  ASSERT_EQ(FLAGS_test_auto_flag, 100);
  VerifyFlagDefault(100);

  // Setting an override should take precedence.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_test_auto_flag) = 10;
  ASSERT_EQ(FLAGS_test_auto_flag, 10);
  VerifyFlagDefault(100);

  // Promote should not modify overridden values
  ASSERT_NO_FATALS(PromoteAutoFlag(*flag_desc));
  ASSERT_EQ(FLAGS_test_auto_flag, 10);
}

TEST(AutoFlagsTest, TestAutoPromoted) {
  ParseCommandLineFlags({kPromoteAllAutoFlagsArg});

  ASSERT_EQ(FLAGS_test_auto_flag, 100);
  VerifyFlagDefault(100);

  ASSERT_EQ(FLAGS_test_auto_bool, true);
  ASSERT_EQ(FLAGS_test_auto_int32, 2);
  ASSERT_EQ(FLAGS_test_auto_int64, 2);
  ASSERT_EQ(FLAGS_test_auto_uint64, 2);
  ASSERT_EQ(FLAGS_test_auto_double, 2);
  ASSERT_EQ(FLAGS_test_auto_string, "true");

  // promote again should be no-op
  const auto* flag_desc = GetAutoFlagDescription(kFlagName);
  ASSERT_NO_FATALS(PromoteAutoFlag(*flag_desc));
  ASSERT_EQ(FLAGS_test_auto_flag, 100);
  VerifyFlagDefault(100);
}

TEST(AutoFlagsTest, TestOverride) {
  ParseCommandLineFlags({kFlagNameArg, "5"});

  ASSERT_EQ(FLAGS_test_auto_flag, 5);
  VerifyFlagDefault(0);

  // Override and PromoteAll
  ParseCommandLineFlags({kFlagNameArg, "5", kPromoteAllAutoFlagsArg});
  ASSERT_EQ(FLAGS_test_auto_flag, 5);
  VerifyFlagDefault(100);

  // PromoteAll and Override
  ParseCommandLineFlags({kPromoteAllAutoFlagsArg, kFlagNameArg, "5"});
  ASSERT_EQ(FLAGS_test_auto_flag, 5);
  VerifyFlagDefault(100);
}

TEST(AutoFlagsTest, TestGetFlagsEligibleForPromotion) {
  string max_flag_class;
  AutoFlagsInfoMap available_flags;
  available_flags["p1"].emplace_back("c1", AutoFlagClass::kLocalVolatile, RuntimeAutoFlag::kTrue);
  available_flags["p2"].emplace_back("c2", AutoFlagClass::kLocalPersisted, RuntimeAutoFlag::kTrue);
  available_flags["p3"].emplace_back("c3", AutoFlagClass::kExternal, RuntimeAutoFlag::kFalse);
  available_flags["p3"].emplace_back("c3r", AutoFlagClass::kExternal, RuntimeAutoFlag::kTrue);

  auto eligible_flags = AutoFlagsUtil::GetFlagsEligibleForPromotion(
      available_flags, AutoFlagClass::kLocalVolatile, PromoteNonRuntimeAutoFlags::kFalse);
  ASSERT_EQ(eligible_flags.size(), 1);
  ASSERT_TRUE(eligible_flags.contains("p1"));
  ASSERT_EQ(eligible_flags["p1"].size(), 1);
  ASSERT_EQ(eligible_flags["p1"][0].name, "c1");

  eligible_flags = AutoFlagsUtil::GetFlagsEligibleForPromotion(
      available_flags, AutoFlagClass::kLocalPersisted, PromoteNonRuntimeAutoFlags::kFalse);
  ASSERT_EQ(eligible_flags.size(), 2);
  ASSERT_TRUE(eligible_flags.contains("p1") && eligible_flags.contains("p2"));
  ASSERT_EQ(eligible_flags["p1"].size(), 1);
  ASSERT_EQ(eligible_flags["p1"][0].name, "c1");
  ASSERT_EQ(eligible_flags["p2"].size(), 1);
  ASSERT_EQ(eligible_flags["p2"][0].name, "c2");

  eligible_flags = AutoFlagsUtil::GetFlagsEligibleForPromotion(
      available_flags, AutoFlagClass::kExternal, PromoteNonRuntimeAutoFlags::kFalse);
  ASSERT_EQ(eligible_flags.size(), 3);
  ASSERT_TRUE(eligible_flags.contains("p3"));
  ASSERT_EQ(eligible_flags["p3"].size(), 1);
  ASSERT_EQ(eligible_flags["p3"][0].name, "c3r");

  eligible_flags = AutoFlagsUtil::GetFlagsEligibleForPromotion(
      available_flags, AutoFlagClass::kExternal, PromoteNonRuntimeAutoFlags::kTrue);
  ASSERT_EQ(eligible_flags.size(), 3);
  ASSERT_TRUE(eligible_flags.contains("p3"));
  ASSERT_EQ(eligible_flags["p3"].size(), 2);
}

TEST(AutoFlagsTest, TestDemote) {
  ASSERT_EQ(FLAGS_test_auto_flag, 0);
  VerifyFlagDefault(0);

  const auto* flag_desc = GetAutoFlagDescription(kFlagName);
  ASSERT_NO_FATALS(PromoteAutoFlag(*flag_desc));
  ASSERT_EQ(FLAGS_test_auto_flag, 100);
  VerifyFlagDefault(100);

  ASSERT_NO_FATALS(DemoteAutoFlag(*flag_desc));
  ASSERT_EQ(FLAGS_test_auto_flag, 0);
  VerifyFlagDefault(0);

  // Setting an override should take precedence.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_test_auto_flag) = 10;
  ASSERT_EQ(FLAGS_test_auto_flag, 10);
  VerifyFlagDefault(0);

  // Promote should not modify overridden values.
  ASSERT_NO_FATALS(PromoteAutoFlag(*flag_desc));
  ASSERT_EQ(FLAGS_test_auto_flag, 10);
  VerifyFlagDefault(100);

  // Demote should not modify overridden values.
  ASSERT_NO_FATALS(DemoteAutoFlag(*flag_desc));
  ASSERT_EQ(FLAGS_test_auto_flag, 10);
  VerifyFlagDefault(0);
}
}  // namespace yb
