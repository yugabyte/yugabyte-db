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

import static org.yb.AssertionWrappers.*;

import java.sql.Connection;
import java.sql.Statement;
import java.sql.Timestamp;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.ListIterator;
import java.util.Objects;
import java.util.stream.Collectors;

import org.apache.commons.lang3.StringUtils;
import org.apache.commons.lang3.tuple.Pair;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TestName;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import org.yb.util.SanitizerUtil;
import org.yb.util.YBTestRunnerNonTsanOnly;

/**
 * For now, this test covers creation of system and shared system relations that should be created
 * indistinguishable from how initdb does it.
 * <p>
 * System relations are created as shared by specifying {@code TABLESPACE pg_global}, the result
 * being that they are accessible from every database. Implementation-wise, they are created in
 * "template1" DB and the YSQL metadata (stored in pg_xxx tables) is inserted across all DBs.
 * <p>
 * NOTE: Each test in this suite leaves garbage tables in system catalogs!
 */
@RunWith(value = YBTestRunnerNonTsanOnly.class)
public class TestYsqlUpgrade extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestYsqlUpgrade.class);

  // Static in order to persist between tests.
  private static int LAST_USED_SYS_OID = 9000;

  /** Tests are performed on a fresh database. */
  private final String customDbName = "test_sys_tables_db";

  /** Since shared relations aren't cleared between tests, we can't reuse names. */
  private String sharedRelName;

  @Rule
  public TestName name = new TestName();

  @Before
  public void beforeTestYsqlUpgrade() throws Exception {
    sharedRelName = "pg_yb_shared_" + Math.abs(name.getMethodName().hashCode());

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE DATABASE " + customDbName);
    }
  }

  @Test
  public void creatingSystemRelsByNonSuperuser() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE ROLE non_su LOGIN PASSWORD 'pwd'");
    }

    try (Connection con = getConnectionBuilder().withUser("non_su").withPassword("pwd").connect();
         Statement stmt = con.createStatement()) {
      runInvalidQuery(stmt, "SET ysql_upgrade_mode TO true",
          "permission denied to set parameter");
      runInvalidQuery(stmt, "SET yb_test_system_catalogs_creation TO true",
          "permission denied to set parameter");

      runInvalidQuery(stmt,
          "CREATE TABLE pg_catalog.pg_yb_sys_fail(v int) WITH ("
              + "  table_oid = " + newSysOid()
              + ", row_type_oid = " + newSysOid()
              + ")",
          "permission denied for schema pg_catalog");

      runInvalidQuery(stmt,
          "CREATE TABLE pg_catalog.pg_yb_shared_fail(v int) WITH ("
              + "  table_oid = " + newSysOid()
              + ", row_type_oid = " + newSysOid()
              + ") TABLESPACE pg_global",
          "permission denied for schema pg_catalog");
    }
  }

  @Test
  public void creatingSharedRelsCreatesThemEverywhere() throws Exception {
    // Since shared relations actually reside in template1, let's create one right there
    // for good measure.
    // Note that we don't change a connection, expecting existing connections to pick up
    // sys catalog changes.
    try (Connection conTpl = getConnectionBuilder().withDatabase("template1").connect();
         Connection conUsr = getConnectionBuilder().withDatabase(customDbName).connect();
         Statement stmtTpl = conTpl.createStatement();
         Statement stmtUsr = conUsr.createStatement()) {
      enableSystemRelsModification(stmtTpl);

      String createSharedRelSql = "CREATE TABLE pg_catalog." + sharedRelName + " ("
          + "  c1 int"
          + ", c2 timestamp with time zone NOT NULL"
          + ", c3 date UNIQUE WITH (table_oid = " + newSysOid() + ")"
          + ", PRIMARY KEY(oid DESC) WITH (table_oid = " + newSysOid() + ")"
          + ") WITH ("
          + "  oids = true"
          + ", table_oid = " + newSysOid()
          + ", row_type_oid = " + newSysOid()
          + ") TABLESPACE pg_global";
      LOG.info("Executing '{}'", createSharedRelSql);
      stmtTpl.execute(createSharedRelSql);
      LOG.info("Created shared relation {}", sharedRelName);

      // We expect template1 and user databases to be ABSOLUTELY identical.
      //
      // We exclude pg_amproc though. The reason for this is that, for template1,
      // returned regproc objects are (for some reason) not prefixed with pg_catalog schema name,
      // so we'd see a mismatch like "expected 'xxx', found 'pg_catalog.xxx'".
      // Since it's not relevant to the changes, excluding it seems fine.
      List<String> pgTables = getRowList(stmtTpl,
          "SELECT tablename FROM pg_tables"
              + " WHERE schemaname = 'pg_catalog' AND tablename != 'pg_amproc'")
                  .stream().map(row -> row.getString(0)).collect(Collectors.toList());

      assertTrue("Shared relation isn't found in information_schema!",
          pgTables.contains(sharedRelName));

      LOG.info("Checking whether {} system tables match each other", pgTables.size());
      for (String pgTable : pgTables) {
        List<Row> rowsTpl = getRowList(stmtTpl, "SELECT * FROM " + pgTable);
        List<Row> rowsUsr = getRowList(stmtUsr, "SELECT * FROM " + pgTable);
        Collections.sort(rowsTpl);
        Collections.sort(rowsUsr);
        assertRows("Mismatch found in table " + pgTable, rowsTpl, rowsUsr);
      }

      LOG.info("Checking if INSERT is propagated correctly");
      String ts1 = "2001-02-03 01:02:03";
      String ts2 = "2002-03-04";
      // write -> template1
      // read  <- user DB
      stmtTpl.execute(String.format("INSERT INTO %s VALUES (1, '%s', '%s')",
          sharedRelName, ts1, ts2));
      assertQuery(stmtUsr, "SELECT * FROM " + sharedRelName,
          new Row(1, Timestamp.valueOf(ts1), new SimpleDateFormat("yyyy-MM-dd").parse(ts2)));
    }
  }

  /** Create a shared relation just like pg_database and verify they look the same. */
  @Test
  public void creatingSharedRelsIsLikeInitdb() throws Exception {
    TableInfo origTi = new TableInfo("pg_database", 1262L, 1248L,
        Arrays.asList(
            Pair.of("pg_database_datname_index", 2671L),
            Pair.of("pg_database_oid_index", 2672L)));

    TableInfo newTi = new TableInfo("pg_database_2", newSysOid(), newSysOid(),
        Arrays.asList(
            Pair.of("pg_database_2_datname_index", newSysOid()),
            Pair.of("pg_database_2_oid_index", newSysOid())));

    try (Connection connB = getConnectionBuilder().withDatabase(customDbName).connect();
         Statement stmtA = connection.createStatement();
         Statement stmtB = connB.createStatement()) {
      enableSystemRelsModification(stmtA);

      String createRelSql = "CREATE TABLE pg_catalog." + newTi.name + " ("
          + "  datname        name      NOT NULL"
          + ", datdba         oid       NOT NULL"
          + ", encoding       integer   NOT NULL"
          + ", datcollate     name      NOT NULL"
          + ", datctype       name      NOT NULL"
          + ", datistemplate  boolean   NOT NULL"
          + ", datallowconn   boolean   NOT NULL"
          + ", datconnlimit   integer   NOT NULL"
          + ", datlastsysoid  oid       NOT NULL"
          + ", datfrozenxid   xid       NOT NULL"
          + ", datminmxid     xid       NOT NULL"
          + ", dattablespace  oid       NOT NULL"
          + ", datacl         aclitem[]"
          + ", CONSTRAINT " + newTi.indexes.get(1).getLeft() + " PRIMARY KEY (oid ASC)"
          + "    WITH (table_oid = " + newTi.indexes.get(1).getRight() + ")"
          + ") WITH ("
          + "  oids = true"
          + ", table_oid = " + newTi.oid
          + ", row_type_oid = " + newTi.typeOid
          + ") TABLESPACE pg_global";
      LOG.info("Executing '{}'", createRelSql);
      stmtA.execute(createRelSql);

      // Create pg_database_2_datname_index (don't do this inline for diversity).
      String createIndexSql = "CREATE UNIQUE INDEX " + newTi.indexes.get(0).getLeft()
          + " "
          + "ON pg_catalog." + newTi.name + " (datname) WITH ("
          + "  table_oid = " + newTi.indexes.get(0).getRight()
          + ") TABLESPACE pg_global";
      LOG.info("Executing '{}'", createIndexSql);
      stmtA.execute(createIndexSql);
      LOG.info("Created unique index {}", newTi.indexes.get(0).getLeft());

      assertTablesAreSimilar(origTi, newTi, stmtA, stmtB);
    }
  }

  /** Create a system relation just like pg_class and verify they look the same. */
  @Test
  public void creatingSystemRelsIsLikeInitdb() throws Exception {
    TableInfo origTi = new TableInfo("pg_class", 1259L, 83L,
        Arrays.asList(
            Pair.of("pg_class_oid_index", 2662L),
            Pair.of("pg_class_relname_nsp_index", 2663L),
            Pair.of("pg_class_tblspc_relfilenode_index", 3455L)));

    TableInfo newTi = new TableInfo("pg_class_2", newSysOid(), newSysOid(),
        Arrays.asList(
            Pair.of("pg_class_2_oid_index", newSysOid()),
            Pair.of("pg_class_2_relname_nsp_index", newSysOid()),
            Pair.of("pg_class_2_tblspc_relfilenode_index", newSysOid())));

    try (Connection conn = getConnectionBuilder().withDatabase(customDbName).connect();
         Statement stmtA = conn.createStatement();
         Statement stmtB = conn.createStatement()) {
      enableSystemRelsModification(stmtA);

      String createRelSql = "CREATE TABLE pg_catalog." + newTi.name + " ("
          + "  relname             name      NOT NULL"
          + ", relnamespace        oid       NOT NULL"
          + ", reltype             oid       NOT NULL"
          + ", reloftype           oid       NOT NULL"
          + ", relowner            oid       NOT NULL"
          + ", relam               oid       NOT NULL"
          + ", relfilenode         oid       NOT NULL"
          + ", reltablespace       oid       NOT NULL"
          + ", relpages            integer   NOT NULL"
          + ", reltuples           real      NOT NULL"
          + ", relallvisible       integer   NOT NULL"
          + ", reltoastrelid       oid       NOT NULL"
          + ", relhasindex         boolean   NOT NULL"
          + ", relisshared         boolean   NOT NULL"
          + ", relpersistence      \"char\"  NOT NULL"
          + ", relkind             \"char\"  NOT NULL"
          + ", relnatts            smallint  NOT NULL"
          + ", relchecks           smallint  NOT NULL"
          + ", relhasoids          boolean   NOT NULL"
          + ", relhasrules         boolean   NOT NULL"
          + ", relhastriggers      boolean   NOT NULL"
          + ", relhassubclass      boolean   NOT NULL"
          + ", relrowsecurity      boolean   NOT NULL"
          + ", relforcerowsecurity boolean   NOT NULL"
          + ", relispopulated      boolean   NOT NULL"
          + ", relreplident        \"char\"  NOT NULL"
          + ", relispartition      boolean   NOT NULL"
          + ", relrewrite          oid       NOT NULL"
          + ", relfrozenxid        xid       NOT NULL"
          + ", relminmxid          xid       NOT NULL"
          + ", relacl              aclitem[]"
          + ", reloptions          text[]"
          + ", relpartbound        pg_node_tree"
          + ", CONSTRAINT " + newTi.indexes.get(0).getLeft() + " PRIMARY KEY (oid ASC)"
          + "    WITH (table_oid = " + newTi.indexes.get(0).getRight() + ")"
          + ") WITH ("
          + "  oids = true"
          + ", table_oid = " + newTi.oid
          + ", row_type_oid = " + newTi.typeOid
          + ")";
      LOG.info("Executing '{}'", createRelSql);
      stmtA.execute(createRelSql);

      {
        String createIndexSql = "CREATE UNIQUE INDEX " + newTi.indexes.get(1).getLeft() + " "
            + "ON pg_catalog." + newTi.name + " (relname ASC, relnamespace ASC) WITH ("
            + "  table_oid = " + newTi.indexes.get(1).getRight()
            + ")";
        LOG.info("Executing '{}'", createIndexSql);
        stmtA.execute(createIndexSql);
        LOG.info("Created unique index {}", newTi.indexes.get(1).getLeft());
      }

      {
        String createIndexSql = "CREATE INDEX " + newTi.indexes.get(2).getLeft() + " "
            + "ON pg_catalog." + newTi.name + " (reltablespace ASC, relfilenode ASC) WITH ("
            + "  table_oid = " + newTi.indexes.get(2).getRight()
            + ")";
        LOG.info("Executing '{}'", createIndexSql);
        stmtA.execute(createIndexSql);
        LOG.info("Created index {}", newTi.indexes.get(2).getLeft());
      }

      assertTablesAreSimilar(origTi, newTi, stmtA, stmtB);
    }
  }

  @Test
  public void creatingSystemRelsDontFireTriggers() throws Exception {
    try (Connection conn = getConnectionBuilder().withDatabase(customDbName).connect();
         Statement stmt = conn.createStatement()) {
      stmt.execute("CREATE TABLE evt_trig_table (id serial PRIMARY KEY, evt_trig_name text)");
      stmt.execute("CREATE OR REPLACE FUNCTION evt_trig_fn()"
          + "  RETURNS event_trigger AS $$"
          + "  BEGIN"
          + "    INSERT INTO evt_trig_table (evt_trig_name) VALUES (TG_EVENT);"
          + "  END;"
          + "  $$ LANGUAGE plpgsql");
      stmt.execute("CREATE EVENT TRIGGER evt_ddl_start"
          + " ON ddl_command_start EXECUTE PROCEDURE evt_trig_fn()");
      stmt.execute("CREATE EVENT TRIGGER evt_ddl_end"
          + " ON ddl_command_end EXECUTE PROCEDURE evt_trig_fn()");
      stmt.execute("CREATE EVENT TRIGGER evt_rewr"
          + " ON table_rewrite EXECUTE PROCEDURE evt_trig_fn()");
      stmt.execute("CREATE EVENT TRIGGER evt_drop"
          + " ON sql_drop EXECUTE PROCEDURE evt_trig_fn()");

      enableSystemRelsModification(stmt);

      // Create a simple system relation.
      stmt.execute("CREATE TABLE pg_catalog.simple_system_table ("
          + "  id int"
          + ", PRIMARY KEY (id ASC)"
          + "    WITH (table_oid = " + newSysOid() + ")"
          + ") WITH ("
          + "  table_oid = " + newSysOid()
          + ", row_type_oid = " + newSysOid()
          + ")");

      // Create a simple SHARED system relation.
      stmt.execute("CREATE TABLE pg_catalog." + sharedRelName + " ("
          + "  id int"
          + ", PRIMARY KEY (id ASC)"
          + "    WITH (table_oid = " + newSysOid() + ")"
          + ") WITH ("
          + "  table_oid = " + newSysOid()
          + ", row_type_oid = " + newSysOid()
          + ") TABLESPACE pg_global");

      assertNoRows(stmt, "SELECT * FROM evt_trig_table");
    }
  }

  @Test
  public void creatingSystemRelsAfterFailure() throws Exception {
    try (Connection conn = getConnectionBuilder().withDatabase(customDbName).connect();
         Statement stmt = conn.createStatement()) {
      enableSystemRelsModification(stmt);

      String ddlSql = "CREATE TABLE pg_catalog.simple_system_table ("
          + "  id int"
          + ", PRIMARY KEY (id ASC)"
          + "    WITH (table_oid = " + newSysOid() + ")"
          + ") WITH ("
          + "  table_oid = " + newSysOid()
          + ", row_type_oid = " + newSysOid()
          + ")";

      stmt.execute("SET yb_test_fail_next_ddl TO true");
      runInvalidQuery(stmt, ddlSql, "DDL failed as requested");

      // Letting CatalogManagerBgTasks do the cleanup.
      Thread.sleep(SanitizerUtil.adjustTimeout(5000));

      stmt.execute(ddlSql);

      String selectSql = "SELECT * FROM simple_system_table";
      assertNoRows(stmt, selectSql);
      stmt.execute("INSERT INTO simple_system_table VALUES (2), (1), (3)");
      assertQuery(stmt, selectSql, new Row(1), new Row(2), new Row(3));
    }
  }

  @Test
  public void sharedRelsIndexesWork() throws Exception {
    try (Connection conTpl = getConnectionBuilder().withDatabase("template1").connect();
         Connection conUsr = getConnectionBuilder().withDatabase(customDbName).connect();
         Statement stmtTpl = conTpl.createStatement();
         Statement stmtUsr = conUsr.createStatement()) {
      enableSystemRelsModification(stmtTpl);

      String createSharedRelSql = "CREATE TABLE pg_catalog." + sharedRelName + " ("
          + "  id int"
          + ", v int"
          + ", PRIMARY KEY (id ASC) WITH (table_oid = " + newSysOid() + ")"
          + ") WITH ("
          + "  table_oid = " + newSysOid()
          + ", row_type_oid = " + newSysOid()
          + ") TABLESPACE pg_global";
      LOG.info("Executing '{}'", createSharedRelSql);
      stmtTpl.execute(createSharedRelSql);
      LOG.info("Created shared relation {}", sharedRelName);

      // Inserting some data before creating index.
      stmtTpl.execute(String.format("INSERT INTO %s VALUES (1, 10), (2, 20), (3, 30)",
          sharedRelName));

      // Creating index.
      String sharedIndexName = sharedRelName + "_idx";
      String createSharedIndexSql = "CREATE INDEX " + sharedIndexName
          + " ON pg_catalog." + sharedRelName + " (v ASC)"
          + " WITH (table_oid = " + newSysOid() + ")";
      LOG.info("Executing '{}'", createSharedIndexSql);
      stmtTpl.execute(createSharedIndexSql);
      LOG.info("Created shared index {}", sharedIndexName);

      // write -> template1
      // read  <- user DB
      String query = "SELECT v FROM " + sharedRelName + " WHERE v > 15";
      assertTrue(isIndexOnlyScan(stmtUsr, query, sharedIndexName));
      assertQuery(stmtUsr, query,
          new Row(20),
          new Row(30));

      // Another insert after index is in place already.
      // This time we write into user DB and read from template1.
      enableSystemRelsModification(stmtUsr);
      stmtUsr.execute(String.format("INSERT INTO %s VALUES (4, 40)", sharedRelName));
      assertTrue(isIndexOnlyScan(stmtTpl, query, sharedIndexName));
      assertQuery(stmtTpl, query,
          new Row(20),
          new Row(30),
          new Row(40));
    }
  }

  //
  // Helpers
  //

  /**
   * Assert that tables (represented by two instancs of {@link TableInfo}) are the same, minus
   * differences in OIDs and names.
   * <p>
   * Both tables should have indexes listed in the same order.
   * <p>
   * JDBC statements for querying info about original and new table could be different.
   */
  private void assertTablesAreSimilar(
      TableInfo origTi,
      TableInfo newTi,
      Statement stmtForOrig,
      Statement stmtForNew) throws Exception {
    assertEquals("Invalid table definition", origTi.indexes.size(), newTi.indexes.size());

    // For exactly four non-shared tables (and their indexes), relfilenode is expected to be zero.
    // This is not typical and happens because:
    // - pg_class.dat contains initial data for each of them;
    // - Their BKI headers are marked with BKI_BOOTSTRAP.
    // if we want to test creating a similar table, we'd have to ignore this mismatch.
    boolean expectRelfilenodeMismatch = //
        Arrays.asList("pg_type", "pg_attribute", "pg_proc", "pg_class").contains(origTi.name);

    // Functions to fetch rows from system tables corresponding to the original and new table
    // respectively, that transforms them to "look" like they belong to the same table - replaces
    // names and OIDs, etc - thus making them comparable
    // Also changes/removes values expected to mismatch due to PG vs YB difference.

    ConvertedRowFetcher fetchExpected = (TableInfoSqlFormatter formatter) -> {
      String sql = formatter.format(origTi);
      LOG.info("Executing '{}'", sql);
      return getRowList(stmtForOrig, sql)
          .stream()
          .map(r -> excluded(r, "reltuples"))
          .map(r -> expectRelfilenodeMismatch ? excluded(r, "relfilenode") : r)
          .collect(Collectors.toList());
    };

    ConvertedRowFetcher fetchActual = (TableInfoSqlFormatter formatter) -> {
      List<Row> rows = getRowList(stmtForNew, formatter.format(newTi));
      return rows.stream()
          .map(r -> excluded(r, "reltuples"))
          .map(r -> expectRelfilenodeMismatch ? excluded(r, "relfilenode") : r)
          .map(r -> replaced(r, newTi.oid, origTi.oid))
          .map(r -> replaced(r, newTi.typeOid, origTi.typeOid))
          .map(r -> replacedString(r, newTi.name, origTi.name))
          .map(r -> {
            for (int i = 0; i < newTi.indexes.size(); ++i) {
              r = replaced(r, newTi.indexes.get(i).getRight(), origTi.indexes.get(i).getRight());
            }
            return r;
          })
          .collect(Collectors.toList());
    };

    {
      // pg_class and pg_type (for rel and indexes)
      String sql = "SELECT cl.oid, cl.*, tp.oid, tp.* FROM pg_class cl"
          + " LEFT JOIN pg_type tp ON cl.oid = tp.typrelid"
          + " WHERE cl.oid = %d ORDER By cl.oid, tp.oid";
      {
        TableInfoSqlFormatter fmt = (ti) -> String.format(sql, ti.oid);
        assertRows(origTi.name,
            fetchExpected.fetch(fmt), fetchActual.fetch(fmt));
      }
      for (int i = 0; i < origTi.indexes.size(); ++i) {
        final int fi = i;
        TableInfoSqlFormatter fmt = (ti) -> String.format(sql, ti.indexes.get(fi).getRight());
        assertRows(newTi.indexes.get(i).getLeft(),
            fetchExpected.fetch(fmt), fetchActual.fetch(fmt));
      }
    }

    {
      // pg_attributes and pg_attrdef
      String sql = "SELECT * FROM pg_attribute attr"
          + " LEFT JOIN pg_attrdef def ON def.adrelid = attr.attrelid AND def.adnum = attr.attnum"
          + " WHERE attr.attrelid = %d ORDER BY attr.attnum";
      TableInfoSqlFormatter fmt = (ti) -> String.format(sql, ti.oid);
      assertRows("attributes",
          fetchExpected.fetch(fmt), fetchActual.fetch(fmt));
    }

    {
      // pg_index
      String sql = "SELECT * FROM pg_index idx"
          + " WHERE idx.indrelid = %d ORDER BY idx.indexrelid";
      TableInfoSqlFormatter fmt = (ti) -> String.format(sql, ti.oid);
      assertRows("pg_index",
          fetchExpected.fetch(fmt), fetchActual.fetch(fmt));
    }

    {
      // pg_constraints
      String sql = "SELECT * FROM pg_constraint"
          + " WHERE conrelid = %d ORDER BY conname";
      TableInfoSqlFormatter fmt = (ti) -> String.format(sql, ti.oid);
      assertRows("pg_constraint",
          fetchExpected.fetch(fmt), fetchActual.fetch(fmt));
    }

    {
      // pg_depend (for rel, type and indexes)
      String sql = "SELECT * FROM pg_depend"
          + " WHERE objid = %d OR refobjid = %d"
          + " ORDER BY classid, objid, objsubid, refclassid, refobjid, refobjsubid, deptype";
      {
        TableInfoSqlFormatter fmt = (ti) -> String.format(sql, ti.oid, ti.oid);
        assertRows("pg_depend for " + newTi.name,
            fetchExpected.fetch(fmt), fetchActual.fetch(fmt));
      }
      {
        TableInfoSqlFormatter fmt = (ti) -> String.format(sql, ti.typeOid, ti.typeOid);
        assertRows("pg_depend for type",
            fetchExpected.fetch(fmt), fetchActual.fetch(fmt));
      }
      for (int i = 0; i < origTi.indexes.size(); ++i) {
        final int fi = i;
        TableInfoSqlFormatter fmt = //
            (ti) -> String.format(sql,
                ti.indexes.get(fi).getRight(),
                ti.indexes.get(fi).getRight());
        assertRows("pg_depend for " + newTi.indexes.get(i).getLeft(),
            fetchExpected.fetch(fmt), fetchActual.fetch(fmt));
      }
    }

    {
      // pg_shdepend (for rel, type and indexes)
      String sql = "SELECT * FROM pg_shdepend"
          + " WHERE objid = %d OR refobjid = %d"
          + " ORDER BY dbid, classid, objid, objsubid, refclassid, refobjid, deptype";
      {
        TableInfoSqlFormatter fmt = (ti) -> String.format(sql, ti.oid, ti.oid);
        assertRows("pg_shdepend for " + newTi.name,
            fetchExpected.fetch(fmt), fetchActual.fetch(fmt));
      }
      {
        TableInfoSqlFormatter fmt = (ti) -> String.format(sql, ti.typeOid, ti.typeOid);
        assertRows("pg_shdepend for type",
            fetchExpected.fetch(fmt), fetchActual.fetch(fmt));
      }
      for (int i = 0; i < origTi.indexes.size(); ++i) {
        final int fi = i;
        TableInfoSqlFormatter fmt = //
            (ti) -> String.format(sql,
                ti.indexes.get(fi).getRight(),
                ti.indexes.get(fi).getRight());
        assertRows("pg_shdepend for " + newTi.indexes.get(i).getLeft(),
            fetchExpected.fetch(fmt), fetchActual.fetch(fmt));
      }
    }

    {
      // pg_init_privs (for rel, type and indexes)
      String sql = "SELECT * FROM pg_init_privs"
          + " WHERE objoid IN (%s)"
          + " ORDER BY objoid";
      TableInfoSqlFormatter fmt = (ti) -> {
        List<Long> list = new ArrayList<>(Arrays.asList(ti.oid, ti.typeOid));
        list.addAll(ti.indexes.stream().map(p -> p.getRight()).collect(Collectors.toList()));
        return String.format(sql, StringUtils.join(list, ","));
      };
      assertRows("pg_init_privs",
          fetchExpected.fetch(fmt), fetchActual.fetch(fmt));
    }
  }

  /** Returns a new row with certain column removed. */
  private Row excluded(Row row, String colNameToRemove) {
    try {
      Row row2 = row.clone();
      ListIterator<String> colNamesIter = row2.columnNames.listIterator();
      ListIterator<Object> elemsIter = row2.elems.listIterator();
      while (colNamesIter.hasNext()) {
        String currColName = colNamesIter.next();
        elemsIter.next();
        if (currColName.equals(colNameToRemove)) {
          colNamesIter.remove();
          elemsIter.remove();
        }
      }
      return row2;
    } catch (CloneNotSupportedException ex) {
      throw new RuntimeException(ex);
    }
  }

  /** Returns a new row with all occurrences of one value replaced with another. */
  private <T> Row replaced(Row row, T from, T to) {
    try {
      Row row2 = row.clone();
      ListIterator<Object> iter = row2.elems.listIterator();
      while (iter.hasNext()) {
        Object curr = iter.next();
        if (Objects.equals(curr, from)) {
          iter.set(to);
        }
      }
      return row2;
    } catch (CloneNotSupportedException ex) {
      throw new RuntimeException(ex);
    }
  }

  /** Returns a new row with substring replaced in all string columns. */
  private Row replacedString(Row row, String what, String replacement) {
    try {
      Row row2 = row.clone();
      ListIterator<Object> iter = row2.elems.listIterator();
      while (iter.hasNext()) {
        Object curr = iter.next();
        if (curr instanceof String && ((String) curr).contains(what)) {
          iter.set(((String) curr).replace(what, replacement));
        }
      }
      return row2;
    } catch (CloneNotSupportedException ex) {
      throw new RuntimeException(ex);
    }
  }

  private void enableSystemRelsModification(Statement stmt) throws Exception {
    stmt.execute("SET ysql_upgrade_mode TO true");
    stmt.execute("SET yb_test_system_catalogs_creation TO true");
  }

  /** Since YB expects OIDs to never get reused, we need to always pick a previously unused OID. */
  private long newSysOid() {
    return ++LAST_USED_SYS_OID;
  }

  @FunctionalInterface
  private interface TableInfoSqlFormatter {
    public String format(TableInfo ti);
  }

  @FunctionalInterface
  private interface ConvertedRowFetcher {
    // Ideally, args should be (String, TableInfo -> Object[]) but in Java that's too much
    // boilerplate
    public List<Row> fetch(TableInfoSqlFormatter formatter) throws Exception;
  }

  private class TableInfo {
    public final String name;
    public final long oid;
    public final long typeOid;
    public final List<Pair<String, Long>> indexes;

    public TableInfo(String name, long oid, long typeOid, List<Pair<String, Long>> indexes) {
      this.name = name;
      this.oid = oid;
      this.typeOid = typeOid;
      this.indexes = Collections.unmodifiableList(new ArrayList<>(indexes));
    }
  }
}
