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

import java.util.Map;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;

/**
 * Runs the whole INSERT ... ON CONFLICT schedule with conflict-resolution pushdown enabled for
 * every session, so that the correctness files exercise the pushdown path for any statement whose
 * shape is eligible for it, alongside the batched and non-batched paths covered by
 * {@link TestPgRegressInsertOnConflict} and its Batch* subclasses.
 *
 * Pushdown only changes the request pattern, never the result, so the same expected files apply.
 * As the eligibility gate widens (multi-row, DO UPDATE, partitioned targets), those files start
 * validating pushdown results without any further test changes.
 */
@RunWith(value=YBTestRunner.class)
public class TestPgRegressInsertOnConflictPushdown extends TestPgRegressInsertOnConflict {
  public static final String YB_INSERT_ON_CONFLICT_PUSHDOWN_FLAG =
      "ysql_yb_enable_ioc_do_nothing_pushdown";

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flags = super.getTServerFlags();
    flags.merge("allowed_preview_flags_csv", YB_INSERT_ON_CONFLICT_PUSHDOWN_FLAG,
                (e, a) -> e + "," + a);
    flags.put(YB_INSERT_ON_CONFLICT_PUSHDOWN_FLAG, "true");
    return flags;
  }
}
