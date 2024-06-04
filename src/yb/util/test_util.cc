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

#include "yb/util/test_util.h"

#include <gtest/gtest-spi.h>

#include "yb/gutil/casts.h"
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/strcat.h"
#include "yb/gutil/strings/util.h"
#include "yb/gutil/walltime.h"

#include "yb/util/curl_util.h"
#include "yb/util/env.h"
#include "yb/util/env_util.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/path_util.h"
#include "yb/util/spinlock_profiling.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/thread.h"
#include "yb/util/debug/trace_event.h"

DEFINE_NON_RUNTIME_string(test_leave_files, "on_failure",
              "Whether to leave test files around after the test run. "
              " Valid values are 'always', 'on_failure', or 'never'");

DEFINE_NON_RUNTIME_int32(test_random_seed, 0, "Random seed to use for randomized tests");
DECLARE_int64(memory_limit_hard_bytes);
DECLARE_bool(enable_tracing);
DECLARE_bool(TEST_enable_sync_points);
DECLARE_bool(TEST_running_test);
DECLARE_bool(never_fsync);
DECLARE_string(vmodule);
DEFINE_test_flag(bool, use_yb_controller, false, "Use YBController in tests.");

using std::string;
using strings::Substitute;
using gflags::FlagSaver;

namespace yb {

namespace {

class YBTestEnvironment : public ::testing::Environment {
 public:
  void SetUp() override {
  }

  void TearDown() override {
  }
};

class YBTestEnvironmentRegisterer {
 public:
  YBTestEnvironmentRegisterer() {
    ::testing::AddGlobalTestEnvironment(new YBTestEnvironment());
  }

};

YBTestEnvironmentRegisterer yb_test_environment_registerer;

}  // namespace

static const char* const kSlowTestsEnvVariable = "YB_ALLOW_SLOW_TESTS";

static const uint64 kTestBeganAtMicros = Env::Default()->NowMicros();

///////////////////////////////////////////////////
// YBTest
///////////////////////////////////////////////////

YBTest::YBTest()
    : env_(new EnvWrapper(Env::Default())),
      test_dir_(GetTestDataDirectory()) {
  InitThreading();
  debug::EnableTraceEvents();
}

// env passed in from subclass, for tests that run in-memory
YBTest::YBTest(Env *env)
  : env_(env),
    test_dir_(GetTestDataDirectory()) {
}

YBTest::~YBTest() {
  // Clean up the test directory in the destructor instead of a TearDown
  // method. This is better because it ensures that the child-class
  // dtor runs first -- so, if the child class is using a minicluster, etc,
  // we will shut that down before we remove files underneath.
  if (FLAGS_test_leave_files == "always") {
    LOG(INFO) << "-----------------------------------------------";
    LOG(INFO) << "--test_leave_files specified, leaving files in " << test_dir_;
  } else if (FLAGS_test_leave_files == "on_failure" && HasFatalFailure()) {
    LOG(INFO) << "-----------------------------------------------";
    LOG(INFO) << "Had fatal failures, leaving test files at " << test_dir_;
  } else {
    VLOG(1) << "Cleaning up temporary test files...";
    WARN_NOT_OK(env_->DeleteRecursively(test_dir_),
                "Couldn't remove test files");
  }
}

void YBTest::SetUp() {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_running_test) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_enable_sync_points) = true;

  InitSpinLockContentionProfiling();
  InitGoogleLoggingSafeBasic("yb_test");
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_tracing) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_memory_limit_hard_bytes) = 8 * 1024 * 1024 * 1024L;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_never_fsync) = true;

  for (const char* env_var_name : {
      "ASAN_OPTIONS",
      "LSAN_OPTIONS",
      "UBSAN_OPTIONS",
      "TSAN_OPTIONS"
  }) {
    const char* value = getenv(env_var_name);
    if (value && value[0]) {
      LOG(INFO) << "Environment variable " << env_var_name << ": " << value;
    }
  }

  global_curl_ = std::make_unique<CurlGlobalInitializer>();
}

string YBTest::GetTestPath(const string& relative_path) {
  CHECK(!test_dir_.empty()) << "Call SetUp() first";
  return JoinPathSegments(test_dir_, relative_path);
}

///////////////////////////////////////////////////
// Test utility functions
///////////////////////////////////////////////////

bool AllowSlowTests() {
  char *e = getenv(kSlowTestsEnvVariable);
  if ((e == nullptr) ||
      (strlen(e) == 0) ||
      (strcasecmp(e, "false") == 0) ||
      (strcasecmp(e, "0") == 0) ||
      (strcasecmp(e, "no") == 0)) {
    return false;
  }
  if ((strcasecmp(e, "true") == 0) ||
      (strcasecmp(e, "1") == 0) ||
      (strcasecmp(e, "yes") == 0)) {
    return true;
  }
  LOG(FATAL) << "Unrecognized value for " << kSlowTestsEnvVariable << ": " << e;
  return false;
}

void OverrideFlagForSlowTests(const std::string& flag_name,
                              const std::string& new_value) {
  // Ensure that the flag is valid.
  google::GetCommandLineFlagInfoOrDie(flag_name.c_str());

  // If we're not running slow tests, don't override it.
  if (!AllowSlowTests()) {
    return;
  }
  google::SetCommandLineOptionWithMode(flag_name.c_str(), new_value.c_str(),
                                       google::SET_FLAG_IF_DEFAULT);
}

int SeedRandom() {
  int seed;
  // Initialize random seed
  if (FLAGS_test_random_seed == 0) {
    // Not specified by user
    seed = static_cast<int>(GetCurrentTimeMicros());
  } else {
    seed = FLAGS_test_random_seed;
  }
  LOG(INFO) << "Using random seed: " << seed;
  srand(seed);
  return seed;
}

string GetTestDataDirectory() {
  const ::testing::TestInfo* const test_info =
    ::testing::UnitTest::GetInstance()->current_test_info();
  CHECK(test_info) << "Must be running in a gtest unit test to call this function";
  string dir;
  CHECK_OK(Env::Default()->GetTestDirectory(&dir));

  // The directory name includes some strings for specific reasons:
  // - program name: identifies the directory to the test invoker
  // - timestamp and pid: disambiguates with prior runs of the same test
  //
  // e.g. "env-test.TestEnv.TestReadFully.1409169025392361-23600"
  dir += Substitute("/$0.$1.$2.$3-$4",
    StringReplace(google::ProgramInvocationShortName(), "/", "_", true),
    StringReplace(test_info->test_case_name(), "/", "_", true),
    StringReplace(test_info->name(), "/", "_", true),
    kTestBeganAtMicros,
    getpid());
  Status s = Env::Default()->CreateDir(dir);
  CHECK(s.IsAlreadyPresent() || s.ok())
    << "Could not create directory " << dir << ": " << s.ToString();
  if (s.ok()) {
    string metadata;

    StrAppend(&metadata, Substitute("PID=$0\n", getpid()));

    StrAppend(&metadata, Substitute("PPID=$0\n", getppid()));

    char* jenkins_build_id = getenv("BUILD_ID");
    if (jenkins_build_id) {
      StrAppend(&metadata, Substitute("BUILD_ID=$0\n", jenkins_build_id));
    }

    CHECK_OK(WriteStringToFile(Env::Default(), metadata,
                               Substitute("$0/test_metadata", dir)));
  }
  return dir;
}

void AssertEventually(const std::function<void(void)>& f,
                      const MonoDelta& timeout) {
  const MonoTime deadline = MonoTime::Now() + timeout;
  {
    FlagSaver flag_saver;
    // Disable --gtest_break_on_failure, or else the assertion failures
    // inside our attempts will cause the test to SEGV even though we
    // would like to retry.
    testing::FLAGS_gtest_break_on_failure = false;

    for (int attempts = 0; MonoTime::Now() < deadline; attempts++) {
      // Capture any assertion failures within this scope (i.e. from their function)
      // into 'results'
      testing::TestPartResultArray results;
      testing::ScopedFakeTestPartResultReporter reporter(
          testing::ScopedFakeTestPartResultReporter::INTERCEPT_ONLY_CURRENT_THREAD,
          &results);
      f();

      // Determine whether their function produced any new test failure results.
      bool has_failures = false;
      for (int i = 0; i < results.size(); i++) {
        has_failures |= results.GetTestPartResult(i).failed();
      }
      if (!has_failures) {
        return;
      }

      // If they had failures, sleep and try again.
      int sleep_ms = (attempts < 10) ? (1 << attempts) : 1000;
      SleepFor(MonoDelta::FromMilliseconds(sleep_ms));
    }
  }

  // If we ran out of time looping, run their function one more time
  // without capturing its assertions. This way the assertions will
  // propagate back out to the normal test reporter. Of course it's
  // possible that it will pass on this last attempt, but that's OK
  // too, since we aren't trying to be that strict about the deadline.
  f();
  if (testing::Test::HasFatalFailure()) {
    ADD_FAILURE() << "Timed out waiting for assertion to pass.";
  }
}

string GetToolPath(const string& rel_path, const string& tool_name) {
  string exe;
  CHECK_OK(Env::Default()->GetExecutablePath(&exe));
  const string binroot = JoinPathSegments(DirName(exe), rel_path);
  const string tool_path = JoinPathSegments(binroot, tool_name);
  CHECK(Env::Default()->FileExists(tool_path)) << tool_name << " tool not found at " << tool_path;
  return tool_path;
}

bool UseYbController() {
  if (FLAGS_TEST_use_yb_controller) {
    return true;
  }
  const char* env = getenv("YB_TEST_YB_CONTROLLER");
  if (env) {
    auto s = string(env);
    if (s == "1" || s == "true") {
      return true;
    }
  }
  return false;
}

bool DisableMiniClusterBackupTests() {
  const char* env = getenv("YB_DISABLE_MINICLUSTER_BACKUP_TESTS");
  if (env) {
    auto s = string(env);
    if (s == "1" || s == "true") {
      return true;
    }
  }
  return false;
}

void AddExtraFlagsFromEnvVar(const char* env_var_name, std::vector<std::string>* args_dest) {
  const char* extra_daemon_flags_env_var_value = getenv(env_var_name);
  if (extra_daemon_flags_env_var_value) {
    LOG(INFO) << "Setting extra daemon flags as specified by env var " << env_var_name << ": "
              << extra_daemon_flags_env_var_value;
    // TODO: this has an issue with handling quoted arguments with embedded spaces.
    std::istringstream iss(extra_daemon_flags_env_var_value);
    copy(std::istream_iterator<string>(iss),
         std::istream_iterator<string>(),
         std::back_inserter(*args_dest));
  } else {
    LOG(INFO) << "Env var " << env_var_name << " not specified, not setting extra flags from it";
  }
}

string GetCertsDir() {
  const auto sub_dir = "test_certs";
  return JoinPathSegments(env_util::GetRootDir(sub_dir), sub_dir);
}

int CalcNumTablets(size_t num_tablet_servers) {
#ifdef NDEBUG
  return 0;  // Will use the default.
#elif defined(THREAD_SANITIZER) || defined(ADDRESS_SANITIZER)
  return narrow_cast<int>(num_tablet_servers);
#else
  return narrow_cast<int>(num_tablet_servers * 3);
#endif
}

Status CorruptFile(
    const std::string& file_path, int64_t offset, size_t bytes_to_corrupt,
    CorruptionType corruption_type) {
  if (bytes_to_corrupt == 0) {
    LOG(INFO) << "Not corrupting file " << file_path << " since bytes_to_corrupt == 0";
    return Status::OK();
  }
  struct stat sbuf;
  if (stat(file_path.c_str(), &sbuf) != 0) {
    const char* msg = strerror(errno);
    return STATUS_FORMAT(IOError, "$0: $1", msg, file_path);
  }

  if (offset < 0) {
    offset = std::max<int64_t>(sbuf.st_size + offset, 0);
  }
  offset = std::min<int64_t>(offset, sbuf.st_size);
  if (yb::std_util::cmp_greater(offset + bytes_to_corrupt, sbuf.st_size)) {
    bytes_to_corrupt = sbuf.st_size - offset;
  }

  LOG(INFO) << "Corrupting file " << file_path << ", " << bytes_to_corrupt << " bytes at offset "
            << offset << ", file size: " << sbuf.st_size;

  RWFileOptions opts;
  opts.mode = Env::CreateMode::OPEN_EXISTING;
  opts.sync_on_close = true;
  std::unique_ptr<RWFile> file;
  RETURN_NOT_OK(Env::Default()->NewRWFile(opts, file_path, &file));
  std::unique_ptr<uint8_t[]> scratch(new uint8_t[bytes_to_corrupt]);
  Slice data_read;
  RETURN_NOT_OK(file->Read(offset, bytes_to_corrupt, &data_read, scratch.get()));
  SCHECK_EQ(data_read.size(), bytes_to_corrupt, IOError, "Unexpected number of bytes read");

  for (uint8_t* p = data_read.mutable_data(); p < data_read.end(); ++p) {
    switch (corruption_type) {
      case CorruptionType::kZero:
        *p = 0;
        continue;
      case CorruptionType::kXor55:
        *p ^= 0x55;
        continue;
    }
    FATAL_INVALID_ENUM_VALUE(CorruptionType, corruption_type);
  }

  RETURN_NOT_OK(file->Write(offset, data_read));
  RETURN_NOT_OK(file->Sync());
  return file->Close();
}

} // namespace yb
