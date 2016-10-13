// Copyright (c) YugaByte, Inc.

#include <atomic>
#include <memory>
#include <vector>

#include "yb/client/client-test-util.h"
#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/test_workload.h"
#include "yb/util/metrics.h"
#include "yb/util/test_util.h"
#include "yb/integration-tests/load_generator.h"
#include "yb/integration-tests/yb_table_test_base.h"

DEFINE_int32(test_num_iter,
             1,
             "Number of iterations for key-value table tablet server failover test");

namespace yb {

using std::unique_ptr;

using client::YBClient;
using client::YBClientBuilder;
using client::YBScanner;
using client::YBTable;
using client::sp::shared_ptr;

using integration_tests::YBTableTestBase;

class KVTableTsFailoverTest : public YBTableTestBase {
 public:
  virtual bool use_external_mini_cluster() OVERRIDE { return true; }
};

TEST_F(KVTableTsFailoverTest, KillTabletServerUnderLoad) {
  for (int i = 1; i <= FLAGS_test_num_iter; ++i) {
    std::atomic_bool stop_requested_flag(false);
    int rows = 1000000;
    int start_key = 0;
    int writer_threads = 4;
    int reader_threads = 4;
    int value_size_bytes = 16;
    int max_write_errors = 0;
    int max_read_errors = 0;
    int retries_on_empty_read = 10;
    yb::load_generator::MultiThreadedWriter writer(
        rows, start_key, writer_threads, client_.get(), table_.get(), &stop_requested_flag,
        value_size_bytes, max_write_errors);
    yb::load_generator::MultiThreadedReader reader(
        rows, reader_threads, client_.get(), table_.get(), writer.InsertionPoint(),
        writer.InsertedKeys(), writer.FailedKeys(), &stop_requested_flag, value_size_bytes,
        max_read_errors, retries_on_empty_read, false /* noop_reads */);

    writer.Start();
    reader.Start();

    for (int i = 0; i < 3; ++i) {
      SleepFor(MonoDelta::FromSeconds(2));
      LOG(INFO) << "Killing tablet server #" << i;
      external_mini_cluster()->tablet_server(i)->Shutdown();
      LOG(INFO) << "Re-starting tablet server #" << i;
      ASSERT_OK(external_mini_cluster()->tablet_server(i)->Restart());
      external_mini_cluster()->WaitForTabletServerCount(3, MonoDelta::FromSeconds(20));
    }

    stop_requested_flag.store(true);  // stop both reader and writer
    writer.WaitForCompletion();
    LOG(INFO) << "Writing complete";

    reader.WaitForCompletion();
    LOG(INFO) << "Reading complete";

    if (FLAGS_test_num_iter > 1) {
      LOG(INFO) << "Completed iteration " << i << " of the test";
    }

    ASSERT_EQ(0, writer.num_write_errors());
    ASSERT_EQ(0, reader.num_read_errors());
    // Assuming every thread has time to do at least 50 writes. Had to lower this from 100 after
    // enabling TSAN.
    ASSERT_GE(writer.num_writes(), writer_threads * 50);
    // Assuming reads are at least as fast as writes.
    ASSERT_GE(reader.num_reads(), writer.num_writes());
  }
}

}  // namespace yb
