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
import org.yb.util.YBTestRunnerNonTsanOnly;

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
//                       PRIMARY KEY (iso_region HASH, ident ASC));
//
// The test also runs on the following indexes beside the PRIMARY index.
// CREATE INDEX airports_idx1 ON airports(iso_region hash, name DESC);
// CREATE INDEX airports_idx2 ON airports(iso_region ASC, gps_code ASC);
// CREATE INDEX airports_idx3 ON airports((iso_region, elevation_ft) HASH,
//                                        coordinates, ident, name);

@RunWith(value=YBTestRunnerNonTsanOnly.class)
public class TestPgRegressHashIndex extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgRegressHashIndex.class);

  @Override
  public int getTestMethodTimeoutSec() {
    return 1800;
  }

  @Test
  public void testPgRegressHashIndex() throws Exception {
    // Run schedule, check time for release build.
    runPgRegressTest("yb_hash_index_serial_schedule",
                     getPerfMaxRuntime(60000, 0, 0, 0, 0) /* maxRuntimeMillis */);

    // Number of executions for each statement and the total run time for all executions.
    final int execCount = 3;
    long scan_time;

    // Find the full-scan time.
    long fullscan_time = timeQueryWithRowCount("SELECT gps_code FROM airports",
                                               -1 /* expectedRowCount */,
                                               0 /* maxRuntimeMillis */,
                                               execCount);

    //----------------------------------------------------------------------------------------------
    // Check elapsed time for various statements that use PRIMARY INDEX.
    //   PRIMARY KEY (iso_region HASH, ident ASC)
    scan_time = timeQueryWithRowCount("SELECT count(*) FROM airports" +
                                      "  WHERE iso_region = 'US-CA'",
                                      -1 /* expectedRowCount */,
                                      0 /* maxRuntimeMillis */,
                                      execCount);
    assertLessThan(scan_time * 10, fullscan_time);

    scan_time = timeQueryWithRowCount("SELECT * FROM airports" +
                                      "  WHERE iso_region = 'US-CA' LIMIT 1",
                                      1 /* expectedRowCount */,
                                      0 /* maxRuntimeMillis */,
                                      execCount);
    assertLessThan(scan_time * 20, fullscan_time);

    scan_time = timeQueryWithRowCount("SELECT * FROM airports" +
                                      "  WHERE iso_region = 'US-CA'" +
                                      "  ORDER BY ident LIMIT 1",
                                      1 /* expectedRowCount */,
                                      0 /* maxRuntimeMillis */,
                                      execCount);
    assertLessThan(scan_time * 25, fullscan_time);

    scan_time = timeQueryWithRowCount("SELECT * FROM airports" +
                                      "  WHERE iso_region = 'US-CA'" +
                                      "  ORDER BY ident ASC LIMIT 1",
                                      1 /* expectedRowCount */,
                                      0 /* maxRuntimeMillis */,
                                      execCount);
    assertLessThan(scan_time * 25, fullscan_time);

    LOG.info("Reverse scan is expected to be slower");
    scan_time = timeQueryWithRowCount("SELECT * FROM airports" +
                                      "  WHERE iso_region = 'US-CA'" +
                                      "  ORDER BY ident DESC LIMIT 1",
                                      1 /* expectedRowCount */,
                                      0 /* maxRuntimeMillis */,
                                      execCount);
    assertLessThan(scan_time * 10, fullscan_time);

    scan_time = timeQueryWithRowCount("SELECT * FROM airports" +
                                      "  WHERE iso_region = 'US-CA'" +
                                      "  ORDER BY iso_region, ident LIMIT 1",
                                      1 /* expectedRowCount */,
                                      0 /* maxRuntimeMillis */,
                                      execCount);
    assertLessThan(scan_time * 25, fullscan_time);

    // For some reasons this query runs slower. This needs work.
    scan_time = timeQueryWithRowCount("SELECT gps_code FROM airports" +
                                      "  WHERE iso_region = 'US-CA' AND ident >= '4' LIMIT 2",
                                      2 /* expectedRowCount */,
                                      0 /* maxRuntimeMillis */,
                                      execCount);
    assertLessThan(scan_time, fullscan_time);

    long op_time = timeQueryWithRowCount("SELECT gps_code FROM airports" +
                                         "  WHERE iso_region = 'US-CA' AND ident < '4' LIMIT 2",
                                         2 /* expectedRowCount */,
                                         0 /* maxRuntimeMillis */,
                                         execCount);
    assertLessThan(op_time * 25, fullscan_time);

    //----------------------------------------------------------------------------------------------
    // Check elapsed time for various statements that use the following INDEX.
    //   CREATE INDEX airports_idx1 ON airports(iso_region hash, name DESC);
    scan_time = timeQueryWithRowCount("SELECT gps_code FROM airports" +
                                      "  WHERE iso_region = 'US-CA' ORDER BY name DESC LIMIT 1",
                                      1,
                                      0,
                                      execCount);
    assertLessThan(scan_time * 25, fullscan_time);

    scan_time = timeQueryWithRowCount("SELECT gps_code FROM airports" +
                                      "  WHERE iso_region = 'US-CA' ORDER BY name ASC LIMIT 1",
                                      1,
                                      0,
                                      execCount);
    assertLessThan(scan_time * 25, fullscan_time);

    //----------------------------------------------------------------------------------------------
    // Check elapsed time for various statements that use the following INDEX.
    //   CREATE INDEX airports_idx2 ON airports(iso_region ASC, gps_code ASC);
    scan_time = timeQueryWithRowCount("SELECT gps_code FROM airports" +
                                      "  WHERE iso_region = 'US-CA' ORDER BY gps_code ASC LIMIT 1",
                                      1,
                                      0,
                                      execCount);
    assertLessThan(scan_time * 15, fullscan_time);

    scan_time = timeQueryWithRowCount("SELECT gps_code FROM airports" +
                                      "  WHERE iso_region = 'US-CA' ORDER BY gps_code DESC LIMIT 1",
                                      1,
                                      0,
                                      execCount);
    assertLessThan(scan_time * 15, fullscan_time);

    // Forward scan is a lot slower than reverse scan.
    scan_time = timeQueryWithRowCount("SELECT gps_code FROM airports" +
                                      "  ORDER BY iso_region, gps_code LIMIT 1",
                                      1,
                                      0,
                                      execCount);
    assertLessThan(scan_time * 5, fullscan_time);

    scan_time = timeQueryWithRowCount("SELECT gps_code FROM airports" +
                                      "  ORDER BY iso_region ASC, gps_code ASC LIMIT 1",
                                      1,
                                      0,
                                      execCount);
    assertLessThan(scan_time * 5, fullscan_time);

    // Reverse scan is faster.
    long reverse_time = timeQueryWithRowCount("SELECT gps_code FROM airports" +
                                              "  ORDER BY iso_region DESC, gps_code DESC LIMIT 1",
                                              1,
                                              0,
                                              execCount);
    assertLessThan(reverse_time * 25, fullscan_time);

    // The following statement is doing full scan due to ordering in different direction.
    scan_time = timeQueryWithRowCount("SELECT gps_code FROM airports" +
                                      "  ORDER BY iso_region ASC, gps_code DESC LIMIT 1",
                                      1,
                                      0,
                                      execCount);

    //----------------------------------------------------------------------------------------------
    // Check elapsed time for various statements that use the following INDEX.
    //   CREATE INDEX airports_idx3 ON airports((iso_region, type) HASH,
    //                                          ident, name, coordinates) INCLUDE (gps_code);

    // This ORDER BY should be HASH + RANGE optimized.
    scan_time = timeQueryWithRowCount("SELECT gps_code FROM airports" +
                                      "  WHERE iso_region = 'US-CA' AND type = 'small_airport'" +
                                      "  ORDER BY coordinates, ident, name LIMIT 1",
                                      1,
                                      0,
                                      execCount);
    assertLessThan(scan_time * 15, fullscan_time);

    scan_time = timeQueryWithRowCount("SELECT gps_code FROM airports" +
                                      "  WHERE iso_region = 'US-CA' AND type = 'small_airport'" +
                                      "  ORDER BY coordinates ASC, ident, name LIMIT 1",
                                      1,
                                      0,
                                      execCount);
    assertLessThan(scan_time * 15, fullscan_time);

    scan_time = timeQueryWithRowCount("SELECT gps_code FROM airports" +
                                      "  WHERE iso_region = 'US-CA' AND type = 'small_airport'" +
                                      "  ORDER BY coordinates DESC, ident DESC, name DESC LIMIT 1",
                                      1,
                                      0,
                                      execCount);
    assertLessThan(scan_time * 15, fullscan_time);

    scan_time = timeQueryWithRowCount("SELECT gps_code FROM airports" +
                                      "  WHERE iso_region = 'US-CA' AND type = 'small_airport'" +
                                      "  ORDER BY coordinates, ident, name ASC LIMIT 1",
                                      1,
                                      0,
                                      execCount);
    assertLessThan(scan_time * 15, fullscan_time);

    // This ORDER BY should be HASH + RANGE optimized.
    scan_time = timeQueryWithRowCount("SELECT gps_code FROM airports" +
                                      "  WHERE iso_region = 'US-CA' AND type = 'small_airport'" +
                                      "  ORDER BY coordinates, ident LIMIT 1",
                                      1,
                                      0,
                                      execCount);
    assertLessThan(scan_time * 15, fullscan_time);

    // This ORDER BY should be HASH + RANGE optimized.
    scan_time = timeQueryWithRowCount("SELECT gps_code FROM airports" +
                                      "  WHERE iso_region = 'US-CA' AND type = 'small_airport'" +
                                      "  ORDER BY coordinates LIMIT 1",
                                      1,
                                      0,
                                      execCount);
    assertLessThan(scan_time * 15, fullscan_time);

    // Wrong order direction for name (Must be all ASC or all DESC).
    scan_time = timeQueryWithRowCount("SELECT gps_code FROM airports" +
                                      "  WHERE iso_region = 'US-CA' AND type = 'small_airport'" +
                                      "  ORDER BY coordinates, ident, name DESC LIMIT 1",
                                      1,
                                      0,
                                      execCount);
    assertLessThan(scan_time, fullscan_time);

    // Wrong order direction for ident and name (Must be all ASC or all DESC).
    scan_time = timeQueryWithRowCount("SELECT gps_code FROM airports" +
                                      "  WHERE iso_region = 'US-CA' AND type = 'small_airport'" +
                                      "  ORDER BY coordinates DESC, ident, name LIMIT 1",
                                      1,
                                      0,
                                      execCount);
    assertLessThan(scan_time, fullscan_time);

    // This ORDER BY should not be optimized. Missing hash column.
    scan_time = timeQueryWithRowCount("SELECT gps_code FROM airports" +
                                      "  WHERE iso_region = 'US-CA'" +
                                      "  ORDER BY type, coordinates LIMIT 1",
                                      1,
                                      0,
                                      execCount);

    // This ORDER BY statement behavior is dependent on the cost estimator. It might choose the
    // PRIMARY INDEX over this index.
    scan_time = timeQueryWithRowCount("SELECT gps_code FROM airports" +
                                      "  WHERE iso_region = 'US-CA' AND type = 'small_airport'" +
                                      "  ORDER BY ident LIMIT 1",
                                      1,
                                      0,
                                      execCount);
    assertLessThan(scan_time, fullscan_time);

    //----------------------------------------------------------------------------------------------
    // The following test cases are also for airports_idx3 but wihtout LIMIT clause.
    // Check elapsed time for various statements that use the following INDEX.
    //   CREATE INDEX airports_idx3 ON airports((iso_region, type) HASH,
    //                                          coordinates, ident, name) INCLUDE (gps_code);
    scan_time = timeQueryWithRowCount("SELECT gps_code FROM airports" +
                                      "  WHERE iso_region = 'US-CA' AND type = 'small_airport'" +
                                      "  ORDER BY coordinates, ident, name",
                                      -1,
                                      0,
                                      execCount);
    assertLessThan(scan_time * 10, fullscan_time);

    // This ORDER BY should be HASH + RANGE optimized.
    scan_time = timeQueryWithRowCount("SELECT gps_code FROM airports" +
                                      "  WHERE iso_region = 'US-CA' AND type = 'small_airport'" +
                                      "  ORDER BY coordinates, ident",
                                      -1,
                                      0,
                                      execCount);
    assertLessThan(scan_time * 10, fullscan_time);

    // This ORDER BY should be HASH + RANGE optimized.
    scan_time = timeQueryWithRowCount("SELECT gps_code FROM airports" +
                                      "  WHERE iso_region = 'US-CA' AND type = 'small_airport'" +
                                      "  ORDER BY coordinates",
                                      -1,
                                      0,
                                      execCount);
    assertLessThan(scan_time * 10, fullscan_time);

    // This ORDER BY should not be optimized. Missing hash column.
    scan_time = timeQueryWithRowCount("SELECT gps_code FROM airports" +
                                      "  WHERE iso_region = 'US-CA'" +
                                      "  ORDER BY type, coordinates",
                                      -1,
                                      0,
                                      execCount);

    // This ORDER BY statement behavior is dependent on the cost estimator. It might choose the
    // PRIMARY INDEX over this index.
    scan_time = timeQueryWithRowCount("SELECT gps_code FROM airports" +
                                      "  WHERE iso_region = 'US-CA' AND type = 'small_airport'" +
                                      "  ORDER BY ident",
                                      -1,
                                      0,
                                      execCount);
    assertLessThan(scan_time, fullscan_time);
  }
}
