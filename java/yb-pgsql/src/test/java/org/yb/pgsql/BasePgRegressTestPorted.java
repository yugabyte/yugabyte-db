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

/**
 * Regress test schedules should either contain only YB original tests or upstream PG ported tests.
 * Subclass this class in case of ported tests in order to provide a similar environment to upstream
 * PG and reduce differences with upstream's expectations.  At the time of writing (2024-07-02),
 * some schedules hav a mix of test types, and the subclassing has yet to be applied for certain
 * schedules.
 */
public class BasePgRegressTestPorted extends BasePgRegressTest {
  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("TEST_generate_ybrowid_sequentially", "true");
    appendToYsqlPgConf(flagMap, "yb_use_hash_splitting_by_default=false");
    return flagMap;
  }
}
