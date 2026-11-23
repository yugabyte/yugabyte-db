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
package org.yb.pgsql;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;

import java.util.Map;

import static org.yb.AssertionWrappers.*;

@RunWith(value=YBTestRunner.class)
public class TestPgRegressSecondaryIndexScan extends BasePgRegressTest {

  @Override
  public int getTestMethodTimeoutSec() {
    return 1800;
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    // The 55k row COPY is batched (yb_default_copy_from_rows_per_transaction=20000) into three
    // distributed transactions. Pipelined writes are acked before replication completes, so a Raft
    // leader election (routine under ASAN/TSAN CPU oversubscription) drops an unreplicated write
    // and fails the in-flight batch unretryably, leaving a partial load. Synchronous writes are
    // instead transparently retried against the new leader.
    // TODO(#33082): gating early acks on leader health narrows this window rather than closing it,
    // so this flag may be needed even after that fix.
    flagMap.put("ysql_enable_write_pipelining", "false");
    return flagMap;
  }

  @Test
  public void testPgRegressSecondaryIndexScan() throws Exception {
    // Run schedule, check time for release build.
    runPgRegressTest("yb_secondary_index_scan_serial_schedule",
                     getPerfMaxRuntime(0, 0, 0, 0, 0) /* maxRuntimeMillis */);
  }
}
