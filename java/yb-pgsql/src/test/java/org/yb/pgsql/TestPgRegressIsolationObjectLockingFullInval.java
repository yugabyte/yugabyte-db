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

import org.junit.Test;
import org.junit.runner.RunWith;

import org.yb.YBTestRunner;

/**
 * Runs the same isolation tests as TestPgRegressIsolationObjectLocking but with
 * ysql_max_invalidation_message_queue_size=0 to force the full cache invalidation
 * fallback path in AcceptInvalidationMessages (instead of incremental inval msgs).
 */
@RunWith(value=YBTestRunner.class)
public class TestPgRegressIsolationObjectLockingFullInval
    extends TestPgRegressIsolationObjectLocking {
  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("ysql_max_invalidation_message_queue_size", "0");
    return flagMap;
  }
}
