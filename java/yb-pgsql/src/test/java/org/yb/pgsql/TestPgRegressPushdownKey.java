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
package org.yb.pgsql;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;

import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.time.Instant;

import static org.yb.AssertionWrappers.*;

// Runs the pg_regress test suite on YB code.
// This test is running on our large table, "airports".
// CREATE TABLE airports(ident TEXT,
//                       type TEXT,
//                       name TEXT,
//                       elevation_ft INT,
//                       continent TEXT,
//                       iso_country TEXT,
//                       iso_region TEXT,
//                       municipality TEXT,
//                       gps_code TEXT,
//                       iata_code TEXT,
//                       local_code TEXT,
//                       coordinates TEXT,
//                       PRIMARY KEY (iso_region, name, type, ident));
//
// The test also runs on the following secondary indexes.
// CREATE INDEX airports_scatter_idx ON airports((iso_region, elevation_ft) HASH,
//                                               coordinates, ident, name);

@RunWith(value=YBTestRunner.class)
public class TestPgRegressPushdownKey extends BasePgRegressTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgRegressPushdownKey.class);

  @Override
  public int getTestMethodTimeoutSec() {
    return 1800;
  }

  @Test
  public void testPgRegressPushdownKey() throws Exception {
    // Run schedule, check time for release build.
    runPgRegressTest("yb_pushdown_serial_schedule",
                     getPerfMaxRuntime(60000, 0, 0, 0, 0) /* maxRuntimeMillis */);

    // There should be a follow-up diff to add performance test cases in this file for key-pushdown.
  }
}
