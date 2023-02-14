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

import java.sql.Statement;

import org.junit.Test;
import org.junit.runner.RunWith;

import org.yb.minicluster.YsqlSnapshotVersion;
import org.yb.YBTestRunner;

@RunWith(value = YBTestRunner.class)
public class TestUsingOlderSnapshot extends BasePgSQLTest {

  /** Restart the cluster at earlier YSQL version and verify it worked. */
  @Test
  public void switchingToAnOlderSnaphsot() throws Exception {
    final String catalogVerTableName = "pg_yb_catalog_version";
    final String catalogVerClassEntrySql = "SELECT oid, relname FROM pg_class WHERE relname = '"
        + catalogVerTableName + "'";
    final Row catalogVerClassEntryRow = new Row(8010L, catalogVerTableName);

    try (Statement stmt = connection.createStatement()) {
      assertQuery(stmt, catalogVerClassEntrySql, catalogVerClassEntryRow);
    }

    recreateWithYsqlVersion(YsqlSnapshotVersion.EARLIEST);

    try (Statement stmt = connection.createStatement()) {
      assertNoRows(stmt, catalogVerClassEntrySql);
    }

    recreateWithYsqlVersion(YsqlSnapshotVersion.LATEST);

    try (Statement stmt = connection.createStatement()) {
      assertQuery(stmt, catalogVerClassEntrySql, catalogVerClassEntryRow);
    }
  }
}
