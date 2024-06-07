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
package org.yb.cql;

import java.util.*;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.json.JSONObject;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import org.yb.util.YBBackupException;
import org.yb.util.YBBackupUtil;
import org.yb.util.YBTestRunnerNonTsanAsan;

import static org.yb.AssertionWrappers.fail;

@RunWith(value=YBTestRunnerNonTsanAsan.class)
public class TestYbBackup extends BaseYbBackupTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestYbBackup.class);

  @Test
  public void testYCQLBackupWithUniqueIndex() throws Exception {
    session.execute("create table test_tbl (i int, j int, k int, l int, m int, n int, " +
                    "primary key (i, j, k, l)) with transactions = { 'enabled' : true };");

    session.execute("create UNIQUE INDEX i1 on test_tbl (i, j, k) COVERING (m, n);");
    session.execute("insert into test_tbl (i, j, k, l, m, n) values (1, 1, 1, 1, 1, 1);");
    session.execute("insert into test_tbl (i, j, k, l, m, n) values (2, 1, 1, 1, 1, 1);");
    session.execute("insert into test_tbl (i, j, k, l, m, n) values (1, 2, 1, 1, 1, 1);");
    session.execute("insert into test_tbl (i, j, k, l, m, n) values (1, 1, 2, 1, 1, 1);");

    // Violate the UNIQUE INDEX.
    runInvalidStmt("insert into test_tbl (i, j, k, l, m, n) values (1, 1, 1, 2, 2, 2);");
    runInvalidStmt("insert into test_tbl (i, j, k, l, m, n) values (2, 1, 1, 2, 2, 2);");
    runInvalidStmt("insert into test_tbl (i, j, k, l, m, n) values (1, 2, 1, 2, 2, 2);");
    runInvalidStmt("insert into test_tbl (i, j, k, l, m, n) values (1, 1, 2, 2, 2, 2);");

    String backupDir = YBBackupUtil.getTempBackupDir();
    String output = YBBackupUtil.runYbBackupCreate("--backup_location", backupDir,
        "--keyspace", DEFAULT_TEST_KEYSPACE);
    backupDir = new JSONObject(output).getString("snapshot_url");
    session.execute("insert into test_tbl (i, j, k, l, m, n) values (2, 2, 2, 1, 1, 1);");
    YBBackupUtil.runYbBackupRestore(backupDir, "--keyspace", "ks6");

    assertQuery("select * from " + DEFAULT_TEST_KEYSPACE + ".test_tbl;",
                "Row[1, 1, 1, 1, 1, 1]" +
                "Row[1, 1, 2, 1, 1, 1]" +
                "Row[1, 2, 1, 1, 1, 1]" +
                "Row[2, 1, 1, 1, 1, 1]" +
                "Row[2, 2, 2, 1, 1, 1]");
    runInvalidStmt("insert into " + DEFAULT_TEST_KEYSPACE + ".test_tbl (i, j, k, l, m, n) " +
                   "values (1, 1, 1, 2, 2, 2);");
    runInvalidStmt("insert into " + DEFAULT_TEST_KEYSPACE + ".test_tbl (i, j, k, l, m, n) " +
                   "values (2, 1, 1, 2, 2, 2);");
    runInvalidStmt("insert into " + DEFAULT_TEST_KEYSPACE + ".test_tbl (i, j, k, l, m, n) " +
                   "values (1, 2, 1, 2, 2, 2);");
    runInvalidStmt("insert into " + DEFAULT_TEST_KEYSPACE + ".test_tbl (i, j, k, l, m, n) " +
                   "values (1, 1, 2, 2, 2, 2);");

    assertQuery("select * from ks6.test_tbl;",
                "Row[1, 1, 1, 1, 1, 1]" +
                "Row[1, 1, 2, 1, 1, 1]" +
                "Row[1, 2, 1, 1, 1, 1]" +
                "Row[2, 1, 1, 1, 1, 1]");
    runInvalidStmt("insert into ks6.test_tbl (i, j, k, l, m, n) values (1, 1, 1, 2, 2, 2);");
    runInvalidStmt("insert into ks6.test_tbl (i, j, k, l, m, n) values (2, 1, 1, 2, 2, 2);");
    runInvalidStmt("insert into ks6.test_tbl (i, j, k, l, m, n) values (1, 2, 1, 2, 2, 2);");
    runInvalidStmt("insert into ks6.test_tbl (i, j, k, l, m, n) values (1, 1, 2, 2, 2, 2);");
  }

  @Test
  public void testYCQLBackupWithJsonIndex() throws Exception {
    session.execute("create table test_json_tbl (h int primary key, j jsonb) " +
                    "with transactions = { 'enabled' : true };");
    session.execute("create index json_idx on test_json_tbl (j->'a'->>'b');");

    waitForReadPermsOnAllIndexes("test_json_tbl");

    for (int i = 1; i <= 2000; ++i) {
      String s = String.valueOf(i);
      session.execute("insert into test_json_tbl (h, j) " +
                      "values (" + s + ", '{\"a\":{\"b\":\"b" + s + "\"},\"c\":" + s + "}');");
    }

    String backupDir = YBBackupUtil.getTempBackupDir();
    String output = YBBackupUtil.runYbBackupCreate("--backup_location", backupDir,
        "--keyspace", DEFAULT_TEST_KEYSPACE);
    backupDir = new JSONObject(output).getString("snapshot_url");

    assertQuery("select count(*) from " + DEFAULT_TEST_KEYSPACE + ".test_json_tbl;",
                "Row[2000]");
    session.execute("insert into test_json_tbl (h, j) " +
                    "values (9999, '{\"a\":{\"b\":\"b9999\"},\"c\":9999}');");

    YBBackupUtil.runYbBackupRestore(backupDir, "--keyspace", "ks7");

    assertQuery("select count(*) from " + DEFAULT_TEST_KEYSPACE + ".test_json_tbl;",
                "Row[2001]");
    assertQuery("select * from " + DEFAULT_TEST_KEYSPACE + ".test_json_tbl where h=1;",
                "Row[1, {\"a\":{\"b\":\"b1\"},\"c\":1}]");
    assertQuery("select * from " + DEFAULT_TEST_KEYSPACE + ".test_json_tbl where " +
                "j->'a'->>'b'='b1';", "Row[1, {\"a\":{\"b\":\"b1\"},\"c\":1}]");
    assertQuery("select * from " + DEFAULT_TEST_KEYSPACE + ".test_json_tbl where h=9999;",
                "Row[9999, {\"a\":{\"b\":\"b9999\"},\"c\":9999}]");

    assertQuery("select count(*) from ks7.test_json_tbl;",
                "Row[2000]");
    assertQuery("select * from ks7.test_json_tbl where h=1;",
                "Row[1, {\"a\":{\"b\":\"b1\"},\"c\":1}]");
    assertQuery("select * from ks7.test_json_tbl where j->'a'->>'b'='b1';",
                "Row[1, {\"a\":{\"b\":\"b1\"},\"c\":1}]");
    assertQuery("select * from ks7.test_json_tbl where h=9999;", "");
  }

  @Test
  public void testAlteredYCQLTableBackup() throws Exception {
    session.execute("create table test_tbl (h int primary key, a int, b float) " +
                    "with transactions = { 'enabled' : true }");

    for (int i = 1; i <= 2000; ++i) {
      session.execute("insert into test_tbl (h, a, b) values" +
                      " (" + String.valueOf(i) +                     // h
                      ", " + String.valueOf(100 + i) +               // a
                      ", " + String.valueOf(2.14 + (float)i) + ")"); // b
    }

    session.execute("alter table test_tbl drop a;");
    String backupDir = YBBackupUtil.getTempBackupDir();
    String output = YBBackupUtil.runYbBackupCreate("--backup_location", backupDir,
        "--keyspace", DEFAULT_TEST_KEYSPACE, "--table", "test_tbl");
    backupDir = new JSONObject(output).getString("snapshot_url");
    session.execute("insert into test_tbl (h, b) values (9999, 8.9)");

    YBBackupUtil.runYbBackupRestore(backupDir, "--keyspace", "ks1");
    assertQuery("select * from " + DEFAULT_TEST_KEYSPACE + ".test_tbl where h=1", "Row[1, 3.14]");
    assertQuery("select * from " + DEFAULT_TEST_KEYSPACE + ".test_tbl where h=2000",
                "Row[2000, 2002.14]");
    assertQuery("select * from " + DEFAULT_TEST_KEYSPACE + ".test_tbl where h=9999",
                "Row[9999, 8.9]");

    assertQuery("select * from ks1.test_tbl where h=1", "Row[1, 3.14]");
    assertQuery("select b from ks1.test_tbl where h=1", "Row[3.14]");
    assertQuery("select * from ks1.test_tbl where h=2000", "Row[2000, 2002.14]");
    assertQuery("select h from ks1.test_tbl where h=2000", "Row[2000]");
    assertQuery("select * from ks1.test_tbl where h=9999", "");
  }

  @Test
  public void testAlteredYCQLTableBackupInOriginalCluster() throws Exception {
    session.execute("create table test_tbl (h int primary key, a int, b float) " +
                    "with transactions = { 'enabled' : true }");

    for (int i = 1; i <= 2000; ++i) {
      session.execute("insert into test_tbl (h, a, b) values" +
                      " (" + String.valueOf(i) +                     // h
                      ", " + String.valueOf(100 + i) +               // a
                      ", " + String.valueOf(2.14 + (float)i) + ")"); // b
    }

    String backupDir = YBBackupUtil.getTempBackupDir();
    String output = YBBackupUtil.runYbBackupCreate("--backup_location", backupDir,
        "--keyspace", DEFAULT_TEST_KEYSPACE, "--table", "test_tbl");
    backupDir = new JSONObject(output).getString("snapshot_url");
    session.execute("alter table test_tbl drop a;");
    session.execute("insert into test_tbl (h, b) values (9999, 8.9)");

    try {
      YBBackupUtil.runYbBackupRestore(backupDir);
      fail("Backup restoring did not fail as expected");
    } catch (YBBackupException ex) {
      LOG.info("Expected exception", ex);
    }

    YBBackupUtil.runYbBackupRestore(backupDir, "--keyspace", "ks1");

    assertQuery("select * from " + DEFAULT_TEST_KEYSPACE + ".test_tbl where h=1", "Row[1, 3.14]");
    assertQuery("select * from " + DEFAULT_TEST_KEYSPACE + ".test_tbl where h=2000",
                "Row[2000, 2002.14]");
    assertQuery("select * from " + DEFAULT_TEST_KEYSPACE + ".test_tbl where h=9999",
                "Row[9999, 8.9]");

    assertQuery("select * from ks1.test_tbl where h=1", "Row[1, 101, 3.14]");
    assertQuery("select b from ks1.test_tbl where h=1", "Row[3.14]");
    assertQuery("select * from ks1.test_tbl where h=2000",
                "Row[2000, 2100, 2002.14]");
    assertQuery("select h from ks1.test_tbl where h=2000", "Row[2000]");
    assertQuery("select * from ks1.test_tbl where h=9999", "");
  }
}
