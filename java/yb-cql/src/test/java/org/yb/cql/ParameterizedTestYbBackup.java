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

import java.text.SimpleDateFormat;
import java.time.Instant;
import java.time.temporal.ChronoUnit;
import java.util.*;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.Parameterized.Parameters;
import org.json.JSONObject;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import org.yb.util.TableProperties;
import org.yb.util.YBBackupException;
import org.yb.util.YBBackupUtil;
import org.yb.util.YBParameterizedTestRunnerNonTsanAsan;

import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.fail;

@RunWith(YBParameterizedTestRunnerNonTsanAsan.class)
public class ParameterizedTestYbBackup extends BaseYbBackupTest {
  private static final Logger LOG = LoggerFactory.getLogger(ParameterizedTestYbBackup.class);

  protected static enum IndexGroupType {
    BASIC, NUMERIC, NON_NUMERIC, COLLECTION_AND_JSON;

    @Override
    public String toString() {
      switch (this) {
        case BASIC: return "basic_indexes";
        case NUMERIC: return "numeric_indexes";
        case NON_NUMERIC: return "non_numeric_indexes";
        case COLLECTION_AND_JSON: return "collection_and_json_indexes";
        default: throw new IllegalArgumentException();
      }
    }
  }

  // Run each test with each IndexGroups.
  @Parameters(name = "{index}_{0}")
  public static List<IndexGroupType> parameterization() {
    return Arrays.asList(
        IndexGroupType.BASIC, IndexGroupType.NUMERIC,
        IndexGroupType.NON_NUMERIC, IndexGroupType.COLLECTION_AND_JSON);
  }

  public enum ValuesUpdateState {
    SOURCE, UPDATED;
  }

  private abstract class IndexGroup {
    public IndexGroup(IndexGroupType groupType) { type = groupType; }

    public abstract String[] tables(String keyspace);
    public abstract void setup() throws Exception;
    public abstract void update(String keyspace) throws Exception;
    public abstract void check(String keyspace, ValuesUpdateState state) throws Exception;

    protected String tableProp() throws Exception {
      return tp.isTransactional() ? "with transactions = { 'enabled' : true };" : ";";
    }
    protected String indexTrans() {
      return " transactions = {'enabled' : false, 'consistency_level' : 'user_enforced'};";
    }
    protected String withIndexProp() throws Exception {
      return tp.isTransactional() ? ";" : " with" + indexTrans();
    }
    protected String andIndexProp() throws Exception {
      return tp.isTransactional() ? ";" : " and" + indexTrans();
    }

    public String keyspacePrefix() { return type.toString() + "_"; }

    // Test can update table properties. By default - Transactional table;
    protected TableProperties tp = new TableProperties(TableProperties.TP_TRANSACTIONAL);

    private IndexGroupType type;
  }

  // =========================================================================
  // Test Basic Indexes.
  // =========================================================================
  private class BasicIndexes extends IndexGroup  {
    public BasicIndexes() { super(IndexGroupType.BASIC); }

    @Override
    public String[] tables(String keyspace) {
      return new String[] {"--keyspace", keyspace, "--table", "test_tbl"};
    }

    @Override
    public void setup() throws Exception {
      session.execute("create table test_tbl (h int, r1 int, r2 int, c int, " +
                      "primary key ((h), r1, r2)) " + tableProp());

      session.execute("create index i1 on test_tbl (h, r2, r1) include (c)" + withIndexProp());
      // Special case - reordering PK columns.
      session.execute("create index i2 on test_tbl (r2, r1, h)" + withIndexProp());
      // Unique index.
      session.execute("create unique index i3 on test_tbl (r1, r2, c) " +
                      "with clustering order by (r2 desc, c asc)" + andIndexProp());

      session.execute("insert into test_tbl (h, r1, r2, c) values (1, 2, 3, 4);");
    }

    @Override
    public void update(String keyspace) throws Exception {
      session.execute("insert into " + keyspace + ".test_tbl (h, r1, r2, c) " +
                      "values (1, 2, 3, 99);");
    }

    @Override
    public void check(String keyspace, ValuesUpdateState state) throws Exception {
      final String value_c = (state == ValuesUpdateState.UPDATED ? "99" : "4");
      assertQuery("select * from " + keyspace + ".test_tbl;",
                  "Row[1, 2, 3, " + value_c + "]");
      assertQuery("select * from " + keyspace + ".i1;",
                  "Row[1, 2, 3, " + value_c + "]");
      assertQuery("select * from " + keyspace + ".i2;", "Row[1, 2, 3]");
      assertQuery("select * from " + keyspace + ".i3;",
                  "Row[1, 2, 3, " + value_c + "]");

      // Index 'i2' only scan.
      String select_stmt = "select r1, r2, h from " + keyspace + ".test_tbl where r2=3;";
      String result = session.execute("EXPLAIN " + select_stmt).all().toString();
      assertTrue("Should use index only scan", result.contains("Index Only Scan using"));
      assertTrue("Should use index i2",
          result.contains("using " + keyspace + ".i2 on " + keyspace + ".test_tbl"));
      assertQuery(select_stmt, "Row[2, 3, 1]");

      // Index 'i2' scan.
      select_stmt = "select * from " + keyspace + ".test_tbl where r2=3;";
      result = session.execute("EXPLAIN " + select_stmt).all().toString();
      assertTrue("Should use index scan", result.contains("Index Scan using"));
      assertTrue("Should use index i2",
          result.contains("using " + keyspace + ".i2 on " + keyspace + ".test_tbl"));
      assertQuery(select_stmt, "Row[1, 2, 3, " + value_c + "]");

      // Check unique index.
      if (tp.isTransactional()) {
        runInvalidStmt("insert into " + keyspace + ".test_tbl (h, r1, r2, c) " +
                       "values (9, 2, 3, " + value_c + ");",
                       "Duplicate value disallowed by unique index");
      } else {
        runInvalidStmt("insert into " + keyspace + ".i3 (\"C$_r1\", \"C$_r2\", \"C$_c\", " +
                       "\"C$_h\") values (2, 3, " + value_c + ", 9);",
                       "Duplicate value disallowed by unique index");
      }
    }
  }

  // =========================================================================
  // Test Indexes on numeric types.
  // =========================================================================
  private class NumericIndexes extends IndexGroup  {
    public NumericIndexes() { super(IndexGroupType.NUMERIC); }

    @Override
    public String[] tables(String keyspace) {
      return new String[] {"--keyspace", keyspace, "--table", "test_types"};
    }

    @Override
    public void setup() throws Exception {
      // Different types testing.
      session.execute("create table test_types (" +
                      "c1 tinyint, c2 smallint, c3 integer, c4 bigint, " +
                      "c5 float, c6 double, c9 boolean, " +
                      "primary key (c1))" + tableProp());
      session.execute("create index c2i on test_types (c2)" + withIndexProp());
      session.execute("create index c3i on test_types (c3)" + withIndexProp());
      session.execute("create index c4i on test_types (c4)" + withIndexProp());
      session.execute("create index c5i on test_types (c5)" + withIndexProp());
      session.execute("create index c6i on test_types (c6)" + withIndexProp());
      session.execute("create index c9i on test_types (c9)" + withIndexProp());

      session.execute("insert into test_types (c1, c2, c3, c4, c5, c6, c9) values " +
                      "(1, 2, 3, 4, 5.5, 6.6, true);");
    }

    @Override
    public void update(String keyspace) throws Exception {
      session.execute("insert into " + keyspace + ".test_types (c1, c2, c3, c4, c5, c6, c9) " +
                      "values (1, 12, 13, 14, 15.5, 16.6, false);");
    }

    @Override
    public void check(String keyspace, ValuesUpdateState state) throws Exception {
      if (state == ValuesUpdateState.UPDATED) {
        assertQuery("select * from " + keyspace + ".test_types;",
                    "Row[1, 12, 13, 14, 15.5, 16.6, false]");

        assertQuery("select * from " + keyspace + ".c2i;", "Row[1, 12]");
        assertQuery("select * from " + keyspace + ".c3i;", "Row[1, 13]");
        assertQuery("select * from " + keyspace + ".c4i;", "Row[1, 14]");
        assertQuery("select * from " + keyspace + ".c5i;", "Row[1, 15.5]");
        assertQuery("select * from " + keyspace + ".c6i;", "Row[1, 16.6]");
        assertQuery("select * from " + keyspace + ".c9i;", "Row[1, false]");
      } else {
        assertQuery("select * from " + keyspace + ".test_types;",
                    "Row[1, 2, 3, 4, 5.5, 6.6, true]");

        assertQuery("select * from " + keyspace + ".c2i;", "Row[1, 2]");
        assertQuery("select * from " + keyspace + ".c3i;", "Row[1, 3]");
        assertQuery("select * from " + keyspace + ".c4i;", "Row[1, 4]");
        assertQuery("select * from " + keyspace + ".c5i;", "Row[1, 5.5]");
        assertQuery("select * from " + keyspace + ".c6i;", "Row[1, 6.6]");
        assertQuery("select * from " + keyspace + ".c9i;", "Row[1, true]");
      }
    }
  }

  // =========================================================================
  // Test Indexes on non-numeric types.
  // =========================================================================
  private class NonNumericIndexes extends IndexGroup  {
    public NonNumericIndexes() { super(IndexGroupType.NON_NUMERIC); }

    @Override
    public String[] tables(String keyspace) {
      return new String[] {"--keyspace", keyspace, "--table", "test_types"};
    }

    @Override
    public void setup() throws Exception {
      // Different types testing.
      session.execute("create table test_types (" +
                      "c1 tinyint, " +
                      "c7 varchar, c8 text, " +
                      "c10 date, c11 time, c12 timestamp, " +
                      "c13 inet, " +
                      "c14 uuid, " +
                      "c15 timeuuid, " +
                      "c16 jsonb, " +
                      "primary key (c1))" + tableProp());
      session.execute("create index c7i on test_types (c7)" + withIndexProp());
      session.execute("create index c8i on test_types (c8)" + withIndexProp());
      session.execute("create index c10i on test_types (c10)" + withIndexProp());
      session.execute("create index c11i on test_types (c11)" + withIndexProp());
      session.execute("create index c12i on test_types (c12)" + withIndexProp());
      session.execute("create index c13i on test_types (c13)" + withIndexProp());
      session.execute("create index c14i on test_types (c14)" + withIndexProp());
      session.execute("create index c15i on test_types (c15)" + withIndexProp());
      runInvalidStmt("create index c16i on test_types (c16)" + withIndexProp(),
                     "Invalid Primary Key Column Datatype");

      session.execute("insert into test_types (c1, c7, c8, " +
                      "c10, c11, c12, c13, c14, c15, c16) values " +
                      "(1, '7', '8', '2020-7-29', '1:2:3.123456789', " +
                      "'2020-7-29 13:24:56.987+01:00', '127.0.0.1', " +
                      "11111111-2222-3333-4444-555555555555, " +
                      "f58ba3dc-3422-11e7-a919-92ebcb67fe33, " +
                      "'{\"a\":0}');");
    }

    @Override
    public void update(String keyspace) throws Exception {
      session.execute("insert into " + keyspace + ".test_types (c1, c7, c8, " +
                      "c10, c11, c12, c13, c14, c15, c16) values " +
                      "(1, '17', '18', '2021-7-29', " +
                      "'11:2:3.123456789', '2021-7-29 13:24:56.987+01:00', '127.1.0.1', " +
                      "11111111-2222-3333-4444-999999999999, " +
                      "f58ba3dc-3422-11e7-a919-92ebcb67fe99, " +
                      "'{\"a\":10}');");
    }

    @Override
    public void check(String keyspace, ValuesUpdateState state) throws Exception {
      SimpleDateFormat isoFormat = new SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss");
      isoFormat.setTimeZone(TimeZone.getTimeZone("UTC"));

      if (state == ValuesUpdateState.UPDATED) {
        String dateStr = isoFormat.parse("2021-07-29T12:24:56").toString();
        assertQuery("select * from " + keyspace + ".test_types;",
                    "Row[1, 17, 18, 2021-07-29, 39723123456789, " +
                    dateStr + ", /127.1.0.1, " +
                    "11111111-2222-3333-4444-999999999999, " +
                    "f58ba3dc-3422-11e7-a919-92ebcb67fe99, " +
                    "{\"a\":10}]");

        assertQuery("select * from " + keyspace + ".c7i;", "Row[1, 17]");
        assertQuery("select * from " + keyspace + ".c8i;", "Row[1, 18]");
        assertQuery("select * from " + keyspace + ".c10i;", "Row[1, 2021-07-29]");
        assertQuery("select * from " + keyspace + ".c11i;", "Row[1, 39723123456789]");
        assertQuery("select * from " + keyspace + ".c12i;", "Row[1, " + dateStr + "]");
        assertQuery("select * from " + keyspace + ".c13i;", "Row[1, /127.1.0.1]");
        assertQuery("select * from " + keyspace + ".c14i;",
                    "Row[1, 11111111-2222-3333-4444-999999999999]");
        assertQuery("select * from " + keyspace + ".c15i;",
                    "Row[1, f58ba3dc-3422-11e7-a919-92ebcb67fe99]");
      } else {
        String dateStr = isoFormat.parse("2020-07-29T12:24:56").toString();
        assertQuery("select * from " + keyspace + ".test_types;",
                    "Row[1, 7, 8, 2020-07-29, 3723123456789, " +
                    dateStr + ", /127.0.0.1, " +
                    "11111111-2222-3333-4444-555555555555, " +
                    "f58ba3dc-3422-11e7-a919-92ebcb67fe33, " +
                    "{\"a\":0}]");

        assertQuery("select * from " + keyspace + ".c7i;", "Row[1, 7]");
        assertQuery("select * from " + keyspace + ".c8i;", "Row[1, 8]");
        assertQuery("select * from " + keyspace + ".c10i;", "Row[1, 2020-07-29]");
        assertQuery("select * from " + keyspace + ".c11i;", "Row[1, 3723123456789]");
        assertQuery("select * from " + keyspace + ".c12i;", "Row[1, " + dateStr + "]");
        assertQuery("select * from " + keyspace + ".c13i;", "Row[1, /127.0.0.1]");
        assertQuery("select * from " + keyspace + ".c14i;",
                    "Row[1, 11111111-2222-3333-4444-555555555555]");
        assertQuery("select * from " + keyspace + ".c15i;",
                    "Row[1, f58ba3dc-3422-11e7-a919-92ebcb67fe33]");
      }
    }
  }

  // =========================================================================
  // Test Indexes on collection types & JSON-related indexes.
  // =========================================================================
  private class CollectionAndJsonIndexes extends IndexGroup  {
    public CollectionAndJsonIndexes() { super(IndexGroupType.COLLECTION_AND_JSON); }

    @Override
    public String[] tables(String keyspace) {
      return new String[] {"--keyspace", keyspace, "--table", "test_types",
                           "--keyspace", keyspace, "--table", "test_json_tbl"};
    }

    @Override
    public void setup() throws Exception {
      // Different types testing.
      session.execute("create table test_types (" +
                      "c1 tinyint, " +
                      "fm frozen<map<int, text>>, " +
                      "fs frozen<set<inet>>, " +
                      "fl frozen<list<double>>," +
                      "primary key (c1))" + tableProp());
      session.execute("create index fmi on test_types (fm)" + withIndexProp());
      session.execute("create index fsi on test_types (fs)" + withIndexProp());
      session.execute("create index fli on test_types (fl)" + withIndexProp());

      session.execute("insert into test_types (c1, fm, fs, fl) values " +
                      "(1, {1:'a',2:'b'}, {'1.2.3.4','5.6.7.8'}, [1.1, 2.2]);");

      // Test index on JSON-column & JSON-attribute.
      session.execute("create table test_json_tbl (h int primary key, j jsonb) " + tableProp());

      runInvalidStmt("create index c16i on test_json_tbl (j)" + withIndexProp(),
                     "Invalid Primary Key Column Datatype");

      session.execute("create index json_idx on test_json_tbl (j->'a'->>'b')" + withIndexProp());
      session.execute("insert into test_json_tbl (h, j) " +
                      "values (1, '{\"a\":{\"b\":\"b4\"},\"c\":4}');");
    }

    @Override
    public void update(String keyspace) throws Exception {
      session.execute("insert into " + keyspace + ".test_types (c1, fm, fs, fl) values " +
                      "(1, {11:'a',12:'b'}, {'11.2.3.4','15.6.7.8'}, [11.1, 12.2]);");
      session.execute("insert into " + keyspace + ".test_json_tbl (h, j) " +
                      "values (1, '{\"a\":{\"b\":\"b99\"},\"c\":99}');");
    }

    @Override
    public void check(String keyspace, ValuesUpdateState state) throws Exception {
      if (state == ValuesUpdateState.UPDATED) {
        assertQuery("select * from " + keyspace + ".test_types;",
                    "Row[1, {11=a, 12=b}, [/11.2.3.4, /15.6.7.8], [11.1, 12.2]]");

        assertQuery("select * from " + keyspace + ".fmi;", "Row[1, {11=a, 12=b}]");
        assertQuery("select * from " + keyspace + ".fsi;", "Row[1, [/11.2.3.4, /15.6.7.8]]");
        assertQuery("select * from " + keyspace + ".fli;", "Row[1, [11.1, 12.2]]");
      } else {
        assertQuery("select * from " + keyspace + ".test_types;",
                    "Row[1, {1=a, 2=b}, [/1.2.3.4, /5.6.7.8], [1.1, 2.2]]");

        assertQuery("select * from " + keyspace + ".fmi;", "Row[1, {1=a, 2=b}]");
        assertQuery("select * from " + keyspace + ".fsi;", "Row[1, [/1.2.3.4, /5.6.7.8]]");
        assertQuery("select * from " + keyspace + ".fli;", "Row[1, [1.1, 2.2]]");
      }

      // Testing JSONB.
      final String value_c = (state == ValuesUpdateState.UPDATED ? "99" : "4");
      assertQuery("select * from " + keyspace + ".test_json_tbl;",
                  "Row[1, {\"a\":{\"b\":\"b" + value_c + "\"},\"c\":" + value_c + "}]");
      assertQuery("select * from " + keyspace + ".json_idx;",
                  "Row[1, b" + value_c + "]");
      assertQuery("select * from " + keyspace + ".test_json_tbl " +
                  "where j->'a'->>'b'='b" + value_c + "';",
                  "Row[1, {\"a\":{\"b\":\"b" + value_c + "\"},\"c\":" + value_c + "}]");
    }
  }

  // =========================================================================

  private IndexGroup ig;

  public ParameterizedTestYbBackup(IndexGroupType type) {
    switch (type){
      case BASIC: ig = new BasicIndexes(); break;
      case NUMERIC: ig = new NumericIndexes(); break;
      case NON_NUMERIC: ig = new NonNumericIndexes(); break;
      case COLLECTION_AND_JSON: ig = new CollectionAndJsonIndexes(); break;
      default: throw new IllegalArgumentException();
    }
  }

  public String testYCQLBackupIntoKeyspace(String... createBackupArgs) throws Exception {
    ig.setup();
    ig.check(DEFAULT_TEST_KEYSPACE, ValuesUpdateState.SOURCE);
    String output = YBBackupUtil.runYbBackupCreate(createBackupArgs);
    String backupDir = new JSONObject(output).getString("snapshot_url");
    ig.update(DEFAULT_TEST_KEYSPACE);
    ig.check(DEFAULT_TEST_KEYSPACE, ValuesUpdateState.UPDATED);
    return backupDir;
  }

  public void testYCQLRestoreIntoKeyspace(String backupDir, String keyspace,
                                          String... restoreBackupArgs) throws Exception {
    YBBackupUtil.runYbBackupRestore(backupDir, restoreBackupArgs);
    if (keyspace != DEFAULT_TEST_KEYSPACE) {
      ig.check(DEFAULT_TEST_KEYSPACE, ValuesUpdateState.UPDATED);
    }

    ig.check(keyspace, ValuesUpdateState.SOURCE);
    // Test writes into the restored tables.
    ig.update(keyspace);
    ig.check(keyspace, ValuesUpdateState.UPDATED);
  }

  public void testYCQLBackupAndRestoreIntoKeyspace(String keyspace,
                                                   String... createBackupArgs) throws Exception {
    String backupDir = testYCQLBackupIntoKeyspace(createBackupArgs);
    if (keyspace == DEFAULT_TEST_KEYSPACE) {
      testYCQLRestoreIntoKeyspace(backupDir, keyspace);
    } else {
      keyspace = ig.keyspacePrefix() + keyspace;
      testYCQLRestoreIntoKeyspace(backupDir, keyspace,
                                  "--keyspace", keyspace);
    }
  }

  @Test
  public void testYCQLKeyspaceBackup() throws Exception {
    ig.tp = new TableProperties(TableProperties.TP_NON_TRANSACTIONAL);
    // Using keyspace name only to test full-keyspace backup.
    String backupDir = YBBackupUtil.getTempBackupDir();
    testYCQLBackupAndRestoreIntoKeyspace(
        "ks2",
        "--keyspace", DEFAULT_TEST_KEYSPACE,
        "--backup_location", backupDir);
  }

  @Test
  public void testYCQLKeyspaceBackup_Transactional() throws Exception {
    // Using keyspace name only to test full-keyspace backup.
    String backupDir = YBBackupUtil.getTempBackupDir();
    testYCQLBackupAndRestoreIntoKeyspace(
        "ks3",
        "--keyspace", DEFAULT_TEST_KEYSPACE,
        "--backup_location", backupDir);
  }

  @Test
  public void testYCQLTablesWithIndexesBackup() throws Exception {
    ig.tp = new TableProperties(TableProperties.TP_NON_TRANSACTIONAL);
    // Using explicit keyspace/table pairs to test multi-table backup.
    String backupDir = YBBackupUtil.getTempBackupDir();
    String[] tables = ig.tables(DEFAULT_TEST_KEYSPACE);
    String[] createBackupArgs = Arrays.copyOf(tables, tables.length+2);
    createBackupArgs[tables.length] = "--backup_location";
    createBackupArgs[tables.length + 1] = backupDir;
    testYCQLBackupAndRestoreIntoKeyspace("ks4", createBackupArgs);
  }

  @Test
  public void testYCQLTablesWithIndexesBackup_Transactional() throws Exception {
    // Using explicit keyspace/table pairs to test multi-table backup.
    String backupDir = YBBackupUtil.getTempBackupDir();
    String[] tables = ig.tables(DEFAULT_TEST_KEYSPACE);
    String[] createBackupArgs = Arrays.copyOf(tables, tables.length+2);
    createBackupArgs[tables.length] = "--backup_location";
    createBackupArgs[tables.length + 1] = backupDir;
    testYCQLBackupAndRestoreIntoKeyspace("ks5", createBackupArgs);
  }

  @Test
  public void testYCQLBackupRestoringIntoOriginalKeyspace() throws Exception {
    ig.tp = new TableProperties(TableProperties.TP_NON_TRANSACTIONAL);
    // Using keyspace name only to test full-keyspace backup.
    String backupDir = YBBackupUtil.getTempBackupDir();
    testYCQLBackupAndRestoreIntoKeyspace(
        DEFAULT_TEST_KEYSPACE,
        "--keyspace", DEFAULT_TEST_KEYSPACE,
        "--backup_location", backupDir);
  }

  @Test
  public void testYCQLBackupRestoringIntoOriginalKeyspace_Transactional()
      throws Exception {
    // Using keyspace name only to test full-keyspace backup.
    String backupDir = YBBackupUtil.getTempBackupDir();
    testYCQLBackupAndRestoreIntoKeyspace(
        DEFAULT_TEST_KEYSPACE,
        "--keyspace", DEFAULT_TEST_KEYSPACE,
        "--backup_location", backupDir);
  }

  @Test
  public void testYCQLBackupRestoringIntoOriginalTables() throws Exception {
    ig.tp = new TableProperties(TableProperties.TP_NON_TRANSACTIONAL);
    // Using explicit keyspace/table pairs to test multi-table backup.
    String backupDir = YBBackupUtil.getTempBackupDir();
    String[] tables = ig.tables(DEFAULT_TEST_KEYSPACE);
    String[] createBackupArgs = Arrays.copyOf(tables, tables.length+2);
    createBackupArgs[tables.length] = "--backup_location";
    createBackupArgs[tables.length + 1] = backupDir;
    testYCQLBackupAndRestoreIntoKeyspace(DEFAULT_TEST_KEYSPACE, createBackupArgs);
  }

  @Test
  public void testYCQLBackupRestoringIntoOriginalTables_Transactional()
      throws Exception {
    // Using explicit keyspace/table pairs to test multi-table backup.
    String backupDir = YBBackupUtil.getTempBackupDir();
    String[] tables = ig.tables(DEFAULT_TEST_KEYSPACE);
    String[] createBackupArgs = Arrays.copyOf(tables, tables.length+2);
    createBackupArgs[tables.length] = "--backup_location";
    createBackupArgs[tables.length + 1] = backupDir;
    testYCQLBackupAndRestoreIntoKeyspace(DEFAULT_TEST_KEYSPACE, createBackupArgs);
  }

  @Test
  public void testBackupWithoutChecksumsRestoreWithoutChecksums() throws Exception {
    // Create backup without any checksums.

    String backupDir = YBBackupUtil.getTempBackupDir();
    backupDir = testYCQLBackupIntoKeyspace("--backup_location", backupDir,
        "--keyspace", DEFAULT_TEST_KEYSPACE, "--disable_checksums");
    // Restore backup without any checksums, should succeed.
    testYCQLRestoreIntoKeyspace(
        backupDir, DEFAULT_TEST_KEYSPACE,
        "--keyspace", DEFAULT_TEST_KEYSPACE, "--disable_checksums");
  }

  @Test
  public void testBackupWithChecksumsRestoreWithoutChecksums() throws Exception {
    // Create backup with checksums.
    String backupDir = YBBackupUtil.getTempBackupDir();
    backupDir = testYCQLBackupIntoKeyspace("--backup_location", backupDir,
        "--keyspace", DEFAULT_TEST_KEYSPACE);
    // Restore backup without any checksum validation, should succeed.
    testYCQLRestoreIntoKeyspace(
        backupDir, DEFAULT_TEST_KEYSPACE,
        "--keyspace", DEFAULT_TEST_KEYSPACE, "--disable_checksums");
  }

  @Test
  public void testBackupWithoutChecksumsRestoreWithChecksums() throws Exception {
    // Create backup without any checksums.
    String backupDir = YBBackupUtil.getTempBackupDir();
    backupDir = testYCQLBackupIntoKeyspace("--backup_location", backupDir,
        "--keyspace", DEFAULT_TEST_KEYSPACE, "--disable_checksums");
    // Try to restore backup with checksum validation, should fail since there are no checksums.
    try {
      testYCQLRestoreIntoKeyspace(
          backupDir, DEFAULT_TEST_KEYSPACE,
          "--keyspace", DEFAULT_TEST_KEYSPACE);
      fail("Backup restoring did not fail as expected");
    } catch (YBBackupException ex) {
      LOG.info("Expected exception", ex);
    }
  }

  @Test
  public void testRestoreWithRestoreTime() throws Exception {
    // Create tables and verify.
    ig.setup();
    ig.check(DEFAULT_TEST_KEYSPACE, ValuesUpdateState.SOURCE);
    // Get the current timestamp in microseconds.
    String ts = Long.toString(ChronoUnit.MICROS.between(Instant.EPOCH, Instant.now()));
    // Make some changes to the tables.
    ig.update(DEFAULT_TEST_KEYSPACE);
    ig.check(DEFAULT_TEST_KEYSPACE, ValuesUpdateState.UPDATED);
    // Perform the backup.
    String backupDir = YBBackupUtil.getTempBackupDir();
    String output = YBBackupUtil.runYbBackupCreate("--backup_location", backupDir,
        "--keyspace", DEFAULT_TEST_KEYSPACE);
    backupDir = new JSONObject(output).getString("snapshot_url");
    // Restore with the timestamp provided.
    // Check that we only restored the original tables and not the updated values.
    testYCQLRestoreIntoKeyspace(
        backupDir, DEFAULT_TEST_KEYSPACE,
        "--keyspace", DEFAULT_TEST_KEYSPACE, "--restore_time", ts);
  }
}
