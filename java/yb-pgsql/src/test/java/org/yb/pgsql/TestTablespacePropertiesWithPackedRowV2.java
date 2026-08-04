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

import org.junit.runner.RunWith;

import org.yb.YBTestRunner;
import org.yb.minicluster.MiniYBClusterBuilder;

@RunWith(value = YBTestRunner.class)
public class TestTablespacePropertiesWithPackedRowV2 extends TestTablespaceProperties {
  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    builder.addMasterFlag("ysql_enable_packed_row", "true");
    builder.addMasterFlag("allowed_preview_flags_csv", "ysql_use_packed_row_v2");
    builder.addMasterFlag("ysql_use_packed_row_v2", "true");
    builder.addCommonTServerFlag("ysql_enable_packed_row", "true");
    builder.addCommonTServerFlag("allowed_preview_flags_csv", "ysql_use_packed_row_v2");
    builder.addCommonTServerFlag("ysql_use_packed_row_v2", "true");
  }
}
