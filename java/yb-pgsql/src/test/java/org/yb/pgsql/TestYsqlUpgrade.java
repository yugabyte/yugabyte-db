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

import java.io.File;
import java.sql.Connection;
import java.sql.Statement;
import java.sql.Timestamp;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.ListIterator;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.TreeMap;
import java.util.TreeSet;
import java.util.function.BiConsumer;
import java.util.function.Consumer;
import java.util.function.Function;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Collectors;

import org.apache.commons.io.FileUtils;
import org.apache.commons.io.LineIterator;
import org.apache.commons.lang3.StringUtils;
import org.apache.commons.lang3.tuple.Pair;
import org.junit.After;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TestName;
import org.junit.runner.RunWith;
import com.yugabyte.util.PGobject;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import org.yb.client.TestUtils;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.minicluster.YsqlSnapshotVersion;
import org.yb.util.BuildTypeUtil;
import org.yb.util.CatchingThread;
import org.yb.util.YBTestRunnerNonTsanOnly;

import com.google.common.collect.ImmutableMap;

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

  /**
   * Prefix which should be used for all shared tables created in this test. Used to filter out test
   * rels when computing system catalog checksum.
   */
  private static final String SHARED_ENTITY_PREFIX = "pg_yb_test_";

  private static final String CATALOG_VERSION_TABLE = "pg_yb_catalog_version";
  private static final String MIGRATIONS_TABLE      = "pg_yb_migration";

  /** Guaranteed to be greated than any real OID, needed for sorted entities to appear at the end */
  private static final long PLACEHOLDER_OID = 1234567890L;

  /** Static in order to persist between tests. */
  private static int LAST_USED_SYS_OID = 9000;

  /** Tests are performed on a fresh database. */
  private final String      customDbName = SHARED_ENTITY_PREFIX + "sys_tables_db";
  private ConnectionBuilder customDbCb;
  private ConnectionBuilder template1Cb;

  private static final int MASTER_REFRESH_TABLESPACE_INFO_SECS = 2;
  private static final int MASTER_LOAD_BALANCER_WAIT_TIME_MS   = 60 * 1000;

  private static final List<Map<String, String>> perTserverZonePlacementFlags = Arrays.asList(
      ImmutableMap.of(
          "placement_cloud", "cloud1",
          "placement_region", "region1",
          "placement_zone", "zone1"),
      ImmutableMap.of(
          "placement_cloud", "cloud2",
          "placement_region", "region2",
          "placement_zone", "zone2"),
      ImmutableMap.of(
          "placement_cloud", "cloud3",
          "placement_region", "region3",
          "placement_zone", "zone3"));

  /** Since shared relations aren't cleared between tests, we can't reuse names. */
  private String sharedRelName;

  @Rule
  public TestName name = new TestName();

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    builder.addMasterFlag("ysql_tablespace_info_refresh_secs",
                          Integer.toString(MASTER_REFRESH_TABLESPACE_INFO_SECS));
    builder.addCommonFlag("log_ysql_catalog_versions", "true");

    builder.perTServerFlags(perTserverZonePlacementFlags);
  }

  @Before
  public void beforeTestYsqlUpgrade() throws Exception {
    sharedRelName = SHARED_ENTITY_PREFIX + "shared_" + Math.abs(name.getMethodName().hashCode());

    createDbConnections();

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE DATABASE " + customDbName);
    }
  }

  @After
  public void afterTestYsqlUpgrade() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      setSystemRelsModificationGuc(stmt, false);
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
          "CREATE TABLE pg_catalog." + SHARED_ENTITY_PREFIX + "shared_fail(v int) WITH ("
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
    try (Connection conTpl = template1Cb.connect();
         Connection conUsr = customDbCb.connect();
         Statement stmtTpl = conTpl.createStatement();
         Statement stmtUsr = conUsr.createStatement()) {
      setSystemRelsModificationGuc(stmtTpl, true);

      String createSharedRelSql = "CREATE TABLE pg_catalog." + sharedRelName + " ("
          + "  c1 int"
          + ", c2 timestamp with time zone NOT NULL"
          + ", c3 date CONSTRAINT " + sharedRelName + "_c3_idx UNIQUE"
          + "    WITH (table_oid = " + newSysOid() + ")"
          + ", CONSTRAINT " + sharedRelName + "_pk PRIMARY KEY (oid DESC)"
          + "    WITH (table_oid = " + newSysOid() + ")"
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
      executeSystemTableDml(stmtTpl, String.format("INSERT INTO %s VALUES (1, '%s', '%s')",
          sharedRelName, ts1, ts2));
      assertQuery(stmtUsr, "SELECT * FROM " + sharedRelName,
          new Row(1, Timestamp.valueOf(ts1), new SimpleDateFormat("yyyy-MM-dd").parse(ts2)));

      assertAllOidsAreSysGenerated(stmtUsr, sharedRelName);
    }
  }

  /** Create a shared relation just like pg_database and verify they look the same. */
  @Test
  public void creatingSharedRelsIsLikeInitdb() throws Exception {
    TableInfo origTi = new TableInfo("pg_database", 1262L, 1248L,
        Arrays.asList(
            Pair.of("pg_database_datname_index", 2671L),
            Pair.of("pg_database_oid_index", 2672L)));

    TableInfo newTi = new TableInfo(SHARED_ENTITY_PREFIX + "database_2", newSysOid(), newSysOid(),
        Arrays.asList(
            Pair.of(SHARED_ENTITY_PREFIX + "database_2_datname_index", newSysOid()),
            Pair.of(SHARED_ENTITY_PREFIX + "database_2_oid_index", newSysOid())));

    try (Connection connB = customDbCb.connect();
         Statement stmtA = connection.createStatement();
         Statement stmtB = connB.createStatement()) {
      setSystemRelsModificationGuc(stmtA, true);

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
          + ", table_oid = " + newTi.getOid()
          + ", row_type_oid = " + newTi.getTypeOid()
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

      assertTablesAreSimilar(origTi, newTi, stmtA, stmtB, true /* checkViewDefinition */);
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

    try (Connection conn = customDbCb.connect();
         Statement stmtA = conn.createStatement();
         Statement stmtB = conn.createStatement()) {
      setSystemRelsModificationGuc(stmtA, true);

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
          + ", table_oid = " + newTi.getOid()
          + ", row_type_oid = " + newTi.getTypeOid()
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

      assertTablesAreSimilar(origTi, newTi, stmtA, stmtB, true /* checkViewDefinition */);
    }
  }

  @Test
  public void creatingSystemRelsDontFireTriggers() throws Exception {
    try (Connection conn = customDbCb.connect();
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

      setSystemRelsModificationGuc(stmt, true);

      // Create a simple system relation.
      stmt.execute("CREATE TABLE pg_catalog.simple_system_table ("
          + "  id int"
          + ", CONSTRAINT simple_system_table_pk PRIMARY KEY (id ASC)"
          + "    WITH (table_oid = " + newSysOid() + ")"
          + ") WITH ("
          + "  table_oid = " + newSysOid()
          + ", row_type_oid = " + newSysOid()
          + ")");

      // Create a simple SHARED system relation.
      stmt.execute("CREATE TABLE pg_catalog." + sharedRelName + " ("
          + "  id int"
          + ", CONSTRAINT " + sharedRelName + "_pk PRIMARY KEY (id ASC)"
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
    try (Connection conn = customDbCb.connect();
         Statement stmt = conn.createStatement()) {
      setSystemRelsModificationGuc(stmt, true);

      String ddlSql = "CREATE TABLE pg_catalog.simple_system_table ("
          + "  id int"
          + ", CONSTRAINT simple_system_table_pk PRIMARY KEY (id ASC)"
          + "    WITH (table_oid = " + newSysOid() + ")"
          + ") WITH ("
          + "  table_oid = " + newSysOid()
          + ", row_type_oid = " + newSysOid()
          + ")";

      stmt.execute("SET yb_test_fail_next_ddl TO true");
      runInvalidQuery(stmt, ddlSql, "Failed DDL operation as requested");

      // Letting CatalogManagerBgTasks do the cleanup.
      Thread.sleep(BuildTypeUtil.adjustTimeout(5000));

      stmt.execute(ddlSql);

      String selectSql = "SELECT * FROM simple_system_table";
      assertNoRows(stmt, selectSql);
      executeSystemTableDml(stmt, "INSERT INTO simple_system_table VALUES (2), (1), (3)");
      assertQuery(stmt, selectSql, new Row(1), new Row(2), new Row(3));
    }
  }

  @Test
  public void sharedRelsIndexesWork() throws Exception {
    try (Connection conTpl = template1Cb.connect();
         Connection conUsr = customDbCb.connect();
         Statement stmtTpl = conTpl.createStatement();
         Statement stmtUsr = conUsr.createStatement()) {
      setSystemRelsModificationGuc(stmtTpl, true);

      String createSharedRelSql = "CREATE TABLE pg_catalog." + sharedRelName + " ("
          + "  id int"
          + ", v int"
          + ", CONSTRAINT " + sharedRelName + "_pk PRIMARY KEY (id ASC)"
          + "    WITH (table_oid = " + newSysOid() + ")"
          + ") WITH ("
          + "  table_oid = " + newSysOid()
          + ", row_type_oid = " + newSysOid()
          + ") TABLESPACE pg_global";
      LOG.info("Executing '{}'", createSharedRelSql);
      stmtTpl.execute(createSharedRelSql);
      LOG.info("Created shared relation {}", sharedRelName);

      // Inserting some data before creating index.
      executeSystemTableDml(stmtTpl,
          String.format("INSERT INTO %s VALUES (1, 10), (2, 20), (3, 30)", sharedRelName));

      // Creating index.
      String sharedIndexName = sharedRelName + "_idx";
      String createSharedIndexSql = "CREATE INDEX " + sharedIndexName
          + " ON pg_catalog." + sharedRelName + " (v ASC)"
          + " WITH (table_oid = " + newSysOid() + ")";
      LOG.info("Executing '{}'", createSharedIndexSql);
      stmtTpl.execute(createSharedIndexSql);
      LOG.info("Created shared index {}", sharedIndexName);

      // Create index concurrently is not supported for system catalog.
      String sharedIndexNameConcurrently = sharedIndexName + "_concurrently";
      String createConcurrentIndexSql = "CREATE INDEX CONCURRENTLY " + sharedIndexNameConcurrently
          + " ON pg_catalog." + sharedRelName + " (v ASC)"
          + " WITH (table_oid = " + newSysOid() + ")";
      runInvalidQuery(stmtTpl, createConcurrentIndexSql,
          "CREATE INDEX CONCURRENTLY is currently not supported for system catalog");

      // Checking index flags.
      String indexFlagsSql = "SELECT indislive, indisready, indisvalid FROM pg_index"
          + " WHERE indexrelid = '" + sharedIndexName + "'::regclass";
      assertQuery(stmtTpl, indexFlagsSql, new Row(true, true, true));
      assertQuery(stmtUsr, indexFlagsSql, new Row(true, true, true));

      // Checking if the index is shared.
      String isSharedSql = "SELECT relisshared FROM pg_class"
          + " WHERE relname = '" + sharedIndexName + "'";
      assertQuery(stmtUsr, isSharedSql, new Row(true));
      assertQuery(stmtTpl, isSharedSql, new Row(true));

      String query = "SELECT v FROM " + sharedRelName + " WHERE v > 15";
      assertTrue(isIndexOnlyScan(stmtUsr, query, sharedIndexName));
      assertQuery(stmtUsr, query,
          new Row(20),
          new Row(30));
      assertTrue(isIndexOnlyScan(stmtTpl, query, sharedIndexName));
      assertQuery(stmtTpl, query,
          new Row(20),
          new Row(30));

      // Another insert after index is in place already.
      // This time we write into user DB and read from template1.
      setSystemRelsModificationGuc(stmtUsr, true);
      executeSystemTableDml(stmtUsr, String.format("INSERT INTO %s VALUES (4, 40)", sharedRelName));
      assertTrue(isIndexOnlyScan(stmtUsr, query, sharedIndexName));
      assertQuery(stmtUsr, query,
          new Row(20),
          new Row(30),
          new Row(40));
      assertTrue(isIndexOnlyScan(stmtTpl, query, sharedIndexName));
      assertQuery(stmtTpl, query,
          new Row(20),
          new Row(30),
          new Row(40));
    }
  }

  /** Create a system view just like pg_stats and verify they look the same. */
  @Test
  public void creatingSystemViewsIsLikeInitdb() throws Exception {
    ViewInfo origTi = new ViewInfo("pg_stats");

    ViewInfo newTi = new ViewInfo("pg_stats_2");

    try (Connection conn = customDbCb.connect();
         Statement stmtA = conn.createStatement();
         Statement stmtB = conn.createStatement()) {
      setSystemRelsModificationGuc(stmtA, true);

      String createViewSql = "CREATE VIEW pg_catalog." + newTi.name + " WITH ("
          + "  security_barrier = true"
          + ", use_initdb_acl = true"
          + ") AS"
          + " SELECT"
          + "     nspname AS schemaname,"
          + "     relname AS tablename,"
          + "     attname AS attname,"
          + "     stainherit AS inherited,"
          + "     stanullfrac AS null_frac,"
          + "     stawidth AS avg_width,"
          + "     stadistinct AS n_distinct,"
          + "     CASE"
          + "         WHEN stakind1 = 1 THEN stavalues1"
          + "         WHEN stakind2 = 1 THEN stavalues2"
          + "         WHEN stakind3 = 1 THEN stavalues3"
          + "         WHEN stakind4 = 1 THEN stavalues4"
          + "         WHEN stakind5 = 1 THEN stavalues5"
          + "     END AS most_common_vals,"
          + "     CASE"
          + "         WHEN stakind1 = 1 THEN stanumbers1"
          + "         WHEN stakind2 = 1 THEN stanumbers2"
          + "         WHEN stakind3 = 1 THEN stanumbers3"
          + "         WHEN stakind4 = 1 THEN stanumbers4"
          + "         WHEN stakind5 = 1 THEN stanumbers5"
          + "     END AS most_common_freqs,"
          + "     CASE"
          + "         WHEN stakind1 = 2 THEN stavalues1"
          + "         WHEN stakind2 = 2 THEN stavalues2"
          + "         WHEN stakind3 = 2 THEN stavalues3"
          + "         WHEN stakind4 = 2 THEN stavalues4"
          + "         WHEN stakind5 = 2 THEN stavalues5"
          + "     END AS histogram_bounds,"
          + "     CASE"
          + "         WHEN stakind1 = 3 THEN stanumbers1[1]"
          + "         WHEN stakind2 = 3 THEN stanumbers2[1]"
          + "         WHEN stakind3 = 3 THEN stanumbers3[1]"
          + "         WHEN stakind4 = 3 THEN stanumbers4[1]"
          + "         WHEN stakind5 = 3 THEN stanumbers5[1]"
          + "     END AS correlation,"
          + "     CASE"
          + "         WHEN stakind1 = 4 THEN stavalues1"
          + "         WHEN stakind2 = 4 THEN stavalues2"
          + "         WHEN stakind3 = 4 THEN stavalues3"
          + "         WHEN stakind4 = 4 THEN stavalues4"
          + "         WHEN stakind5 = 4 THEN stavalues5"
          + "     END AS most_common_elems,"
          + "     CASE"
          + "         WHEN stakind1 = 4 THEN stanumbers1"
          + "         WHEN stakind2 = 4 THEN stanumbers2"
          + "         WHEN stakind3 = 4 THEN stanumbers3"
          + "         WHEN stakind4 = 4 THEN stanumbers4"
          + "         WHEN stakind5 = 4 THEN stanumbers5"
          + "     END AS most_common_elem_freqs,"
          + "     CASE"
          + "         WHEN stakind1 = 5 THEN stanumbers1"
          + "         WHEN stakind2 = 5 THEN stanumbers2"
          + "         WHEN stakind3 = 5 THEN stanumbers3"
          + "         WHEN stakind4 = 5 THEN stanumbers4"
          + "         WHEN stakind5 = 5 THEN stanumbers5"
          + "     END AS elem_count_histogram"
          + " FROM pg_statistic s JOIN pg_class c ON (c.oid = s.starelid)"
          + "      JOIN pg_attribute a ON (c.oid = attrelid AND attnum = s.staattnum)"
          + "      LEFT JOIN pg_namespace n ON (n.oid = c.relnamespace)"
          + " WHERE NOT attisdropped"
          + " AND has_column_privilege(c.oid, a.attnum, 'select')"
          + " AND (c.relrowsecurity = false OR NOT row_security_active(c.oid));";

      LOG.info("Executing '{}'", createViewSql);
      stmtA.execute(createViewSql);

      assertTablesAreSimilar(origTi, newTi, stmtA, stmtB, true /* checkViewDefinition */);
    }
  }

  /**
   * CREATE OR REPLACE VIEW should filter out upgrade-specific reloptions on both CREATE and REPLACE
   * paths.
   */
  @Test
  public void viewReloptionsAreFilteredOnReplace() throws Exception {
    String viewName = "replaceable_system_view";

    try (Connection conn = customDbCb.connect();
         Statement stmt = conn.createStatement()) {
      setSystemRelsModificationGuc(stmt, true);

      String createViewSql = "CREATE OR REPLACE VIEW pg_catalog." + viewName + " WITH ("
          + "  security_barrier = true"
          + ", use_initdb_acl = true"
          + ") AS SELECT 1";

      String getReloptionsSql = "SELECT reloptions FROM pg_class"
          + " WHERE oid = 'pg_catalog." + viewName + "'::regclass";

      // CREATE part.
      LOG.info("Executing '{}'", createViewSql);
      stmt.execute(createViewSql);

      assertQuery(stmt, getReloptionsSql,
          new Row(Arrays.asList("security_barrier=true")));

      // REPLACE part.
      LOG.info("Executing '{}' again", createViewSql);
      stmt.execute(createViewSql);

      assertQuery(stmt, getReloptionsSql,
          new Row(Arrays.asList("security_barrier=true")));
    }
  }

  /**
   * When view is queried for the first time, it's definition is cached in various caches.
   * <p>
   * When this view is replaced, those caches should be properly updated across every connection.
   */
  @Test
  public void replacingViewKeepsCacheConsistent() throws Exception {
    try (Connection conn1 = customDbCb.connect();
         Connection conn2 = customDbCb.connect();
         Connection conn3 = customDbCb.withTServer(1).connect();
         Statement stmt1 = conn1.createStatement();
         Statement stmt2 = conn2.createStatement();
         Statement stmt3 = conn2.createStatement()) {
      setSystemRelsModificationGuc(stmt1, true);

      String createViewSqlPat =
          "CREATE OR REPLACE VIEW pg_catalog.test_view_to_change"
              + " WITH (use_initdb_acl = true)"
              + " AS %s";
      String selectFromViewSql =
          "SELECT * FROM test_view_to_change";

      stmt1.execute(String.format(createViewSqlPat, "SELECT 10 AS a"));

      // Wait for the new catalog version to propagate. Otherwise the
      // next SELECT queries will not see the newly created system view
      // 'test_view_to_change' because YSQL allows negative caching
      // for system tables or system views.
      waitForTServerHeartbeat();

      assertQuery(stmt1, selectFromViewSql, new Row(10));
      assertQuery(stmt2, selectFromViewSql, new Row(10));
      assertQuery(stmt3, selectFromViewSql, new Row(10));

      stmt1.execute(String.format(createViewSqlPat, "SELECT 20 AS a, 30 as b"));

      // Wait for the new catalog version to propagate.
      waitForTServerHeartbeat();

      assertQuery(stmt1, selectFromViewSql, new Row(20, 30));
      assertQuery(stmt2, selectFromViewSql, new Row(20, 30));
      assertQuery(stmt3, selectFromViewSql, new Row(20, 30));
    }
  }

  @Test
  public void insertOnConflictWithOidsWorks() throws Exception {
    try (Connection conn = customDbCb.connect();
         Statement stmt = conn.createStatement()) {
      setSystemRelsModificationGuc(stmt, true);

      String commonCreateSqlPattern = "CREATE TABLE pg_catalog.%s ("
          + "  v1 int  NOT NULL"
          + ", v2 text NOT NULL"
          + ", CONSTRAINT %s_pk PRIMARY KEY (oid ASC)"
          + "    WITH (table_oid = %d)"
          + ") WITH ("
          + "  oids = true"
          + ", table_oid = %d"
          + ", row_type_oid = %d"
          + ")";

      String nonSharedRelName = "pg_yb_nonshared_insert";

      {
        String createSharedRelSql = String.format(commonCreateSqlPattern,
            sharedRelName, sharedRelName, newSysOid(), newSysOid(), newSysOid()) +
            " TABLESPACE pg_global";
        LOG.info("Executing '{}'", createSharedRelSql);
        stmt.execute(createSharedRelSql);
      }

      {
        String createNonSharedRelSql = String.format(commonCreateSqlPattern,
            nonSharedRelName, nonSharedRelName, newSysOid(), newSysOid(), newSysOid());
        LOG.info("Executing '{}'", createNonSharedRelSql);
        stmt.execute(createNonSharedRelSql);
      }

      for (String tableName : Arrays.asList(nonSharedRelName, sharedRelName)) {
        String selectSql = "SELECT oid, * FROM " + tableName;

        assertNoRows(stmt, selectSql);

        // Insert something.
        executeSystemTableDml(stmt,
            "INSERT INTO " + tableName + " (oid, v1, v2) VALUES (123, 111, 't1')");
        assertQuery(stmt, selectSql,
            new Row(123, 111, "t1"));

        // Insert something conflicting, with and without ON CONFLICT DO NOTHING.
        String conflictingInsertSql = "INSERT INTO " + tableName + " (oid, v1, v2)"
            + " VALUES (123, -1, 'Nope')";
        runInvalidSystemQuery(stmt, conflictingInsertSql,
            "duplicate key value violates unique constraint \"" + tableName + "_pk\"");
        runInvalidSystemQuery(stmt, conflictingInsertSql
            + " ON CONFLICT (oid) DO UPDATE SET v2 = 'No!'",
            "only ON CONFLICT DO NOTHING can be used in YSQL upgrade");
        stmt.execute(conflictingInsertSql + " ON CONFLICT DO NOTHING");
        assertQuery(stmt, selectSql,
            new Row(123, 111, "t1"));

        // Insert non-conflicting row with ON CONFLICT DO NOTHING.
        executeSystemTableDml(stmt, "INSERT INTO " + tableName + " (oid, v1, v2)"
            + " VALUES (234, 222, 't2') ON CONFLICT DO NOTHING");
        assertQuery(stmt, selectSql,
            new Row(123, 111, "t1"),
            new Row(234, 222, "t2"));

        executeSystemTableDml(stmt, "DELETE FROM " + tableName);
        assertNoRows(stmt, selectSql);

        // Insert without oid column.
        executeSystemTableDml(stmt, "INSERT INTO " + tableName + " (v1, v2) VALUES (333, 't3')");
        assertQuery(stmt, "SELECT * FROM " + tableName + " ORDER BY oid",
            new Row(333, "t3"));
        assertAllOidsAreSysGenerated(stmt, tableName);
      }
    }
  }

  /**
   * Tests that basic DML operations on system tables also update Postgres system caches.
   * <p>
   * This test is not limited to YSQL upgrade.
   */
  @Test
  public void dmlsUpdatePgCache() throws Exception {
    // Querying pg_sequence_parameters involves pg_sequence cache lookup, not an actual table scan.
    // Let's use this fact to make sure INSERT, UPDATE and DELETE properly update this cache.
    try (Connection conn = customDbCb.connect();
         Statement stmt = conn.createStatement()) {
      setAllowNonDdlTxnsGuc(stmt, true);

      String getCachedIncrementSql =
          "SELECT increment FROM pg_sequence_parameters('my_seq'::regclass)";

      stmt.execute("CREATE SEQUENCE my_seq INCREMENT BY 1");
      assertOneRow(stmt, getCachedIncrementSql, 1);

      // Single-row UPDATE
      stmt.execute("UPDATE pg_sequence SET seqincrement = 20 WHERE seqrelid = 'my_seq'::regclass");
      assertOneRow(stmt, getCachedIncrementSql, 20);

      // Mutli-row UPDATE
      stmt.execute("UPDATE pg_sequence SET seqincrement = 21");
      assertOneRow(stmt, getCachedIncrementSql, 21);

      // Single-row DELETE
      stmt.execute("DELETE FROM pg_sequence WHERE seqrelid = 'my_seq'::regclass");
      runInvalidQuery(stmt, getCachedIncrementSql, "cache lookup failed for sequence");

      // INSERT
      stmt.execute("INSERT INTO pg_sequence VALUES "
          + "('my_seq'::regclass, 'bigint'::regtype, 1, 30, 9223372036854775807, 1, 1, false)");
      assertOneRow(stmt, getCachedIncrementSql, 30);

      // Multi-row DELETE
      stmt.execute("DELETE FROM pg_sequence");
      runInvalidQuery(stmt, getCachedIncrementSql, "cache lookup failed for sequence");
    }
  }

  /**
   * YB has an inner cache of pinned dependent objects as a perf optimization. We make sure it's
   * properly updated when we do a pg_depend insert.
   * <p>
   * In this case test, we're also verifying that a view referencing a newly-pinned function is the
   * same as the view referencing an existing pinned function.
   */
  @Test
  public void pinnedObjectsCacheIsUpdated() throws Exception {
    try (Connection conn = customDbCb.connect();
         Statement stmt = conn.createStatement()) {
      setSystemRelsModificationGuc(stmt, true);

      long newProcOid = newSysOid();

      // First, pinned object cache is lazy, so let's trigger its initialization (otherwise,
      // cache would be initialized too late and everything would work even without a fix).
      //
      // To do so, we need to hit isObjectPinned check. One way to do so is to create a view
      // depending on a function.
      {
        String sql = "CREATE VIEW view_to_trigger_pinned_cache_init AS"
            + " SELECT * FROM yb_is_database_colocated()";
        LOG.info("Executing '{}'", sql);
        stmt.execute(sql);
      }

      setAllowNonDdlTxnsGuc(stmt, true);

      // Create a function yb_is_local_table_2 backed up by yb_is_local_table.
      // This is based on our definition of yb_is_local_table, as defined in related migrations.
      {
        String sql = "INSERT INTO pg_catalog.pg_proc ("
            + "  oid, proname, pronamespace, proowner, prolang,"
            + "  procost, prorows, provariadic, protransform,"
            + "  prokind, prosecdef, proleakproof, proisstrict,"
            + "  proretset, provolatile, proparallel, pronargs,"
            + "  pronargdefaults, prorettype, proargtypes,"
            + "  proallargtypes, proargmodes, proargnames,"
            + "  proargdefaults, protrftypes, prosrc, probin, proconfig, proacl"
            + ") VALUES"
            + "  (" + newProcOid + ", 'yb_is_local_table_2', 11, 10, 12, 1, 0, 0, '-', 'f',"
            + "   false, false, true, false, 's', 's', 1, 0, 16, '26', NULL, NULL, NULL,"
            + "   NULL, NULL, 'yb_is_local_table', NULL, NULL, NULL)";
        LOG.info("Executing '{}'", sql);
        stmt.execute(sql);

        sql = "INSERT INTO pg_catalog.pg_depend ("
            + "  classid, objid, objsubid, refclassid, refobjid, refobjsubid, deptype"
            + ") VALUES (0, 0, 0, 'pg_proc'::regclass, " + newProcOid + ", 0, 'p')";
        LOG.info("Executing '{}'", sql);
        stmt.execute(sql);
      }

      // Confirm that functions definition match.
      {
        String sqlPat = "SELECT * FROM pg_proc WHERE proname = '%s'";
        Row expectedPgProcRow = getSingleRow(stmt, String.format(sqlPat, "yb_is_local_table"));
        expectedPgProcRow.elems.set(expectedPgProcRow.columnNames.indexOf("proname"),
            "yb_is_local_table_2");
        Row actualPgProcRow = getSingleRow(stmt, String.format(sqlPat, "yb_is_local_table_2"));
        assertRow(expectedPgProcRow, actualPgProcRow);
      }

      // Since we manually inserted pg_depend row for yb_is_local_table_2, no surprises here.
      assertProcPinDependences(stmt, "yb_is_local_table", "yb_is_local_table_2");

      // Now that we established all visible invariants hold, let's actually test
      // YB pinned object cache.
      //
      // When creating a view based on a function, a dependency will be recorded unless that
      // function is recorded as pinned. This check uses YB dependency cache - so let's test
      // that no dependencies are recorded for view's rule.
      {
        String createViewSqlPat = "CREATE OR REPLACE VIEW pg_catalog.%s"
            + " WITH (use_initdb_acl = true)"
            + " AS SELECT %s('pg_class'::regclass) AS v";
        String createViewOldSql = String.format(createViewSqlPat,
            "yb_is_local_table_view", "yb_is_local_table");
        String createViewNewSql = String.format(createViewSqlPat,
            "yb_is_local_table_2_view", "yb_is_local_table_2");

        LOG.info("Executing '{}'", createViewOldSql);
        stmt.execute(createViewOldSql);
        LOG.info("Executing '{}'", createViewNewSql);
        stmt.execute(createViewNewSql);
      }

      {
        ViewInfo oldView = new ViewInfo("yb_is_local_table_view");
        ViewInfo newView = new ViewInfo("yb_is_local_table_2_view");
        assertTablesAreSimilar(oldView, newView, stmt, stmt, false /* checkViewDefinition */);
      }

      // yb_is_local_table_2 should still have just one kind of dependency - pin dependency.
      assertProcPinDependences(stmt, "yb_is_local_table", "yb_is_local_table_2");
    }
  }

  /** Ensure that both functions have a singular dependency - a pin dependency of a same kind. */
  private void assertProcPinDependences(
      Statement stmt,
      String expectedFunctionName,
      String actualFunctionName) throws Exception {
    String sqlPat = "SELECT * FROM pg_depend"
        + " WHERE refclassid = 'pg_proc'::regclass"
        + " AND refobjid = '%s'::regproc";
    Row expectedPgProcRow = getSingleRow(stmt, String.format(sqlPat, expectedFunctionName));
    Row actualPgProcRow = getSingleRow(stmt, String.format(sqlPat, actualFunctionName));
    // We expect function OIDs to mismatch, so let's counter that.
    expectedPgProcRow.elems.set(expectedPgProcRow.columnNames.indexOf("refobjid"), 0L);
    actualPgProcRow.elems.set(actualPgProcRow.columnNames.indexOf("refobjid"), 0L);
    assertRow(expectedPgProcRow, actualPgProcRow);
    // Is this actually a pin dependency?
    assertEquals("p", actualPgProcRow.getString(6));
  }

  /**
   * Clear applied migrations table, re-run migrations and expect nothing to change from reapplying
   * migrations.
   */
  @Test
  public void upgradeIsIdempotent() throws Exception {
    recreateWithYsqlVersion(YsqlSnapshotVersion.EARLIEST);
    createDbConnections();

    upgradeCheckingIdempotency(false /* useSingleConnection */);
  }

  /**
   * Clear applied migrations table, re-run migrations and expect nothing to change from reapplying
   * migrations.
   * <p>
   * Single-connection variant of {@code upgradeIsIdempotent} test, also ensures there's never too
   * many connections opened.
   */
  @Test
  public void upgradeIsIdempotentSingleConn() throws Exception {
    recreateWithYsqlVersion(YsqlSnapshotVersion.EARLIEST);
    createDbConnections();

    // Ensures there's never more that one connection opened by an upgrade.
    CatchingThread connCounter = new CatchingThread("Connection counter", () -> {
      assertEquals(3, miniCluster.getNumTServers());

      String countOtherConnsSql = "SELECT COUNT(*) from pg_stat_activity"
          + " WHERE backend_type = 'client backend'"
          + " AND pid <> pg_backend_pid()";

      try (Connection conn1 = getConnectionBuilder().withTServer(0).connect();
           Statement stmt1 = conn1.createStatement();
           Connection conn2 = getConnectionBuilder().withTServer(1).connect();
           Statement stmt2 = conn2.createStatement();
           Connection conn3 = getConnectionBuilder().withTServer(2).connect();
           Statement stmt3 = conn3.createStatement()) {
        List<Statement> stmts = Arrays.asList(stmt1, stmt2, stmt3);

        while (!Thread.interrupted()) {
          long totalNumConns = 0;
          for (int tserverIdx = 0; tserverIdx < 3; ++tserverIdx) {
            long numConns = getSingleRow(stmts.get(tserverIdx), countOtherConnsSql).getLong(0);
            LOG.info("Tserver #{} has {} connections", tserverIdx, numConns);
            totalNumConns += numConns;
          }

          // We expect to have up to 3 other connections:
          // * BasePgSQLTest#connection (always active).
          // * An upgrade connection (picks an arbitrary tserver once, and connects/disconnects
          //   to it during work).
          // * Idempotency checking worker connection.
          assertLessThanOrEqualTo(totalNumConns, 3L);
          Thread.sleep(500);
        }
      }
    });

    connCounter.start();
    upgradeCheckingIdempotency(true /* useSingleConnection */);
    connCounter.finish();
  }

  /**
   * Verify that applying migrations to and old initdb snapshot transforms pg_catalog to a state
   * equivalent to a fresh initdb.
   * <p>
   * If you see this test failing, please make sure you've added a new YSQL migration as described
   * in {@code src/yb/yql/pgwrapper/ysql_migrations/README.md}.
   */
  @Test
  public void migratingIsEquivalentToReinitdb() throws Exception {
    final String createPgTablegroupTable =
        "CREATE TABLE IF NOT EXISTS pg_catalog.pg_tablegroup (\n" +
            "  grpname    name        NOT NULL,\n" +
            "  grpowner   oid         NOT NULL,\n" +
            "  grpacl     aclitem[],\n" +
            "  grpoptions text[],\n" +
            "  CONSTRAINT pg_tablegroup_oid_index PRIMARY KEY (oid ASC)\n" +
            "    WITH (table_oid = 8001)\n" +
            ") WITH (\n" +
            "  oids = true,\n" +
            "  table_oid = 8000,\n" +
            "  row_type_oid = 8002\n" +
            ")";

    final SysCatalogSnapshot preSnapshotCustom, preSnapshotTemplate1;
    try (Connection conn = customDbCb.connect();
         Statement stmt = conn.createStatement()) {
      setSystemRelsModificationGuc(stmt, true);
      // We need this until we can drop tables in migrations.
      // When we create the DB by reinitdb, the pg_tablegroup table does not exist.
      // When we upgrade the DB from an old snapshot, the pg_tablegroup table does exist.
      // To reconcile this, we can either create it in the reinitdb DB before taking the snapshot,
      // or delete the table from the upgraded snapshot
      // However, other system tables like pg_attribute are modified by the creation of this table,
      // and so we can't simply remove it from the snapshot. So it is much simpler to create this
      // table again than to try to remove all traces of it ever existing.
      executeSystemTableDml(stmt, createPgTablegroupTable);
      preSnapshotCustom = takeSysCatalogSnapshot(stmt);
    }
    try (Connection conn = template1Cb.connect();
         Statement stmt = conn.createStatement()) {
      setSystemRelsModificationGuc(stmt, true);
      executeSystemTableDml(stmt, createPgTablegroupTable);
      preSnapshotTemplate1 = takeSysCatalogSnapshot(stmt);
    }

    recreateWithYsqlVersion(YsqlSnapshotVersion.EARLIEST);
    createDbConnections();

    try (Connection conn = template1Cb.connect();
         Statement stmt = conn.createStatement()) {
      // Sanity check - no migrations table
      assertNoRows(stmt,
          "SELECT oid, relname FROM pg_class WHERE relname = '" + MIGRATIONS_TABLE + "'");

      stmt.execute("CREATE DATABASE " + customDbName);
    }

    runMigrations(false /* useSingleConnection */);

    final SysCatalogSnapshot postSnapshotCustom, postSnapshotTemplate1;
    try (Connection conn = customDbCb.connect();
         Statement stmt = conn.createStatement()) {
      postSnapshotCustom = takeSysCatalogSnapshot(stmt);
    }
    try (Connection conn = template1Cb.connect();
         Statement stmt = conn.createStatement()) {
      postSnapshotTemplate1 = takeSysCatalogSnapshot(stmt);
    }

    assertYbbasectidIsConsistent("template1");
    assertYbbasectidIsConsistent(customDbName);

    assertMigrationsWorked(preSnapshotCustom, postSnapshotCustom);
    assertMigrationsWorked(preSnapshotTemplate1, postSnapshotTemplate1);

    assertEquals("Maximum system-generated OID differs between databases!"
        + " Migration bug?",
        getMaxSysGeneratedOid(postSnapshotTemplate1),
        getMaxSysGeneratedOid(postSnapshotCustom));
  }

  /** Test that migrations run without error in a geo-partitioned setup. */
  @Test
  public void migrationInGeoPartitionedSetup() throws Exception {
    setupGeoPartitioning();
    runMigrations(false /* useSingleConnection */);
  }

  /** Ensure migration filename comment makes sense. */
  @Test
  public void migrationFilenameComment() throws Exception {
    Pattern commentRe = Pattern.compile("^# .*(V(\\d+)\\.?(\\d+)?__\\S+__\\S+.sql)$");
    Pattern recordRe = Pattern.compile("^\\{ major => '(\\d+)', minor => '(\\d+)',");

    File datFile = new File(
        TestUtils.getBuildRootDir(), "postgres_build/src/include/catalog/pg_yb_migration.dat");
    assertTrue(datFile + " does not exist", datFile.exists());

    LineIterator it = FileUtils.lineIterator(datFile);
    try {
      String filename = null, commentMajor = null, commentMinor = null;
      while (it.hasNext()) {
        Matcher matcher = commentRe.matcher(it.nextLine());
        if (matcher.find()) {
          filename     = matcher.group(1);
          commentMajor = matcher.group(2);
          commentMinor = matcher.group(3) == null ? "0" : matcher.group(3);
          break;
        }
      }
      assertNotNull("Failed to find migration filename comment line", filename);

      // Check that a file with that filename exists.
      File migrationFile = new File(TestUtils.getBuildRootDir(),
                                    "share/ysql_migrations/" + filename);
      assertTrue("Migration file " + filename + " does not exist", migrationFile.exists());

      // Get record version.  Record line comes right after comment line:
      //         | # For better version control conflict detection, list latest migration filename
      // comment | # here: V19__6560__pg_collation_icu_70.sql
      // record  | { major => '19', minor => '0', name => '<baseline>', time_applied => '_null_' }
      assertTrue("Expected line after filename comment line", it.hasNext());
      String recordLine = it.nextLine();
      Matcher matcher = recordRe.matcher(recordLine);
      assertTrue(recordLine + " does not match regex " + recordRe, matcher.find());
      String recordMajor = matcher.group(1);
      String recordMinor = matcher.group(2);

      // Check comment version matches record version.
      assertEquals("Major version mismatch between comment and record:", commentMajor, recordMajor);
      assertEquals("Minor version mismatch between comment and record:", commentMinor, recordMinor);
    } finally {
      it.close();
    }
  }

  /** Invalid stuff which doesn't belong to other test cases. */
  @Test
  public void invalidUpgradeActions() throws Exception {
    try (Connection conn = customDbCb.connect();
         Statement stmt = conn.createStatement()) {
      setSystemRelsModificationGuc(stmt, true);

      runInvalidQuery(stmt, "CREATE DATABASE " + customDbName + "_2",
          "CREATE DATABASE is disallowed in YSQL upgrade mode");
    }
  }

  //
  // Helpers
  //

  /** Helper for creating db connections used in tests.  */
  public void createDbConnections() {
    customDbCb  = getConnectionBuilder().withDatabase(customDbName);
    template1Cb = getConnectionBuilder().withDatabase("template1");
  }

  /** Helper for upgradeIsIdempotent test. */
  public void upgradeCheckingIdempotency(boolean useSingleConnection) throws Exception {
    final int lastHardcodedMigrationVersion = 8;
    try (Connection conn = template1Cb.connect();
         Statement stmt = conn.createStatement()) {
      stmt.execute("CREATE DATABASE " + customDbName);
    }
    runMigrations(useSingleConnection);

    // Ignore pg_yb_catalog_version because we bump current_version disregarding
    // IF NOT EXISTS clause (whether the entity is actually created doesn't matter).
    // For pg_yb_migration, verify that all migrations were applied.
    try (Connection conn = customDbCb.connect();
         Statement stmt = conn.createStatement()) {
      SysCatalogSnapshot preSnapshot = takeSysCatalogSnapshot(stmt);
      final int latestMajorVersion = preSnapshot.catalog.get(MIGRATIONS_TABLE)
          .get(preSnapshot.catalog.get(MIGRATIONS_TABLE).size() - 1).getInt(0);
      final int latestMinorVersion = preSnapshot.catalog.get(MIGRATIONS_TABLE)
          .get(preSnapshot.catalog.get(MIGRATIONS_TABLE).size() - 1).getInt(1);
      final int totalMigrations = latestMajorVersion + latestMinorVersion;

      // Make sure the latest version is at least as big as the last hardcoded one (it will be
      // greater if more migrations were introduced after YSQL upgrade is released).
      assertTrue(latestMajorVersion >= lastHardcodedMigrationVersion);
      preSnapshot.catalog.remove(MIGRATIONS_TABLE);
      preSnapshot.catalog.remove(CATALOG_VERSION_TABLE);

      executeSystemTableDml(stmt, "DELETE FROM " + MIGRATIONS_TABLE);
      runMigrations(useSingleConnection);

      SysCatalogSnapshot postSnapshot = takeSysCatalogSnapshot(stmt);
      List<Row> appliedMigrations = postSnapshot.catalog.get(MIGRATIONS_TABLE);
      assertEquals("Expected an entry for the last hardcoded migration"
          + " and each migration past that!",
          totalMigrations - lastHardcodedMigrationVersion + 1,
          appliedMigrations.size());
      assertEquals(
          lastHardcodedMigrationVersion,
          appliedMigrations
              .get(0).getInt(0).intValue());
      assertEquals(
          latestMajorVersion,
          appliedMigrations
              .get(postSnapshot.catalog.get(MIGRATIONS_TABLE).size() - 1).getInt(0).intValue());
      assertEquals(
          latestMinorVersion,
          appliedMigrations
              .get(postSnapshot.catalog.get(MIGRATIONS_TABLE).size() - 1).getInt(1).intValue());
      postSnapshot.catalog.remove(MIGRATIONS_TABLE);
      postSnapshot.catalog.remove(CATALOG_VERSION_TABLE);

      assertSysCatalogSnapshotsEquals(preSnapshot, postSnapshot);
    }
  }

  /**
   * Assert that tables (represented by two instancs of {@link TableInfo}) are the same, minus
   * obvious differences in OIDs and names.
   * <p>
   * Both tables should have indexes listed in the same order.
   * <p>
   * JDBC statements for querying info about original and new table could be different.
   * <p>
   * If OIDs for {@link TableInfo} are zeros, they will be determined at runtime - and verified to
   * be in the system generated OIDs range.
   *
   * @param checkViewDefinition
   *          whether {@code pg_get_viewdef} should be checked, should be true unless views were
   *          deliberately created using different query.
   */
  private void assertTablesAreSimilar(
      TableInfo origTi,
      TableInfo newTi,
      Statement stmtForOrig,
      Statement stmtForNew,
      boolean checkViewDefinition) throws Exception {
    assertEquals("Invalid table definition", origTi.indexes.size(), newTi.indexes.size());
    assertEquals("TableInfos are of different class!", origTi.getClass(), newTi.getClass());

    // For exactly four non-shared tables (and their indexes), relfilenode is expected to be zero.
    // This is not typical and happens because:
    // - pg_class.dat contains initial data for each of them;
    // - Their BKI headers are marked with BKI_BOOTSTRAP.
    // if we want to test creating a similar table, we'd have to ignore this mismatch.
    boolean expectRelfilenodeMismatch = //
        Arrays.asList("pg_type", "pg_attribute", "pg_proc", "pg_class").contains(origTi.name);

    // Determine OIDs for tables if not predefined.
    for (TableInfo ti : Arrays.asList(origTi, newTi)) {
      if (ti.getOid() == 0) {
        Row row = getSingleRow(stmtForNew, "SELECT oid, reltype FROM pg_class"
            + " WHERE relname = '" + ti.name + "'"
            + " AND relnamespace = 'pg_catalog'::regnamespace");
        ti.setOid(row.getLong(0));
        ti.setTypeOid(row.getLong(1));
        assertSysGeneratedOid(ti.getOid());
        assertSysGeneratedOid(ti.getTypeOid());
      }

      if (ti instanceof ViewInfo) {
        ViewInfo vi = (ViewInfo) ti;
        vi.setRuleOid(getSingleRow(stmtForNew, "SELECT oid FROM pg_rewrite"
            + " WHERE ev_class = " + ti.oid + " AND is_instead = true").getLong(0));
        assertSysGeneratedOid(vi.getRuleOid());
      }
    }

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
          .map(r -> replaced(r, newTi.getOid(), origTi.getOid()))
          .map(r -> replaced(r, newTi.getTypeOid(), origTi.getTypeOid()))
          .map(r -> origTi instanceof ViewInfo
              ? replaced(r, ((ViewInfo) newTi).getRuleOid(), ((ViewInfo) origTi).getRuleOid())
              : r)
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
      // pg_class and pg_type (for rel and indexes), as well as pg_get_viewdef (view query)
      String sql = "SELECT cl.oid, cl.*, tp.oid, tp.*"
          + (checkViewDefinition ? ", pg_get_viewdef(cl.oid)" : "")
          + " FROM pg_class cl"
          + " LEFT JOIN pg_type tp ON cl.oid = tp.typrelid"
          + " WHERE cl.oid = %d ORDER By cl.oid, tp.oid";
      {
        TableInfoSqlFormatter fmt = (ti) -> String.format(sql, ti.getOid());
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
      TableInfoSqlFormatter fmt = (ti) -> String.format(sql, ti.getOid());
      assertRows("attributes",
          fetchExpected.fetch(fmt), fetchActual.fetch(fmt));
    }

    {
      // pg_index
      String sql = "SELECT * FROM pg_index idx"
          + " WHERE idx.indrelid = %d ORDER BY idx.indexrelid";
      TableInfoSqlFormatter fmt = (ti) -> String.format(sql, ti.getOid());
      assertRows("pg_index",
          fetchExpected.fetch(fmt), fetchActual.fetch(fmt));
    }

    {
      // pg_constraints
      String sql = "SELECT * FROM pg_constraint"
          + " WHERE conrelid = %d ORDER BY conname";
      TableInfoSqlFormatter fmt = (ti) -> String.format(sql, ti.getOid());
      assertRows("pg_constraint",
          fetchExpected.fetch(fmt), fetchActual.fetch(fmt));
    }

    {
      // pg_depend (for rel, type, rule and indexes)
      String sql = "SELECT * FROM pg_depend"
          + " WHERE objid = %d OR refobjid = %d"
          + " ORDER BY classid, objid, objsubid, refclassid, refobjid, refobjsubid, deptype";
      {
        TableInfoSqlFormatter fmt = (ti) -> String.format(sql, ti.getOid(), ti.getOid());
        assertRows("pg_depend for " + newTi.name,
            fetchExpected.fetch(fmt), fetchActual.fetch(fmt));
      }
      {
        TableInfoSqlFormatter fmt = (ti) -> String.format(sql, ti.getTypeOid(), ti.getTypeOid());
        assertRows("pg_depend for type of " + newTi.name,
            fetchExpected.fetch(fmt), fetchActual.fetch(fmt));
      }
      if (origTi instanceof ViewInfo) {
        TableInfoSqlFormatter fmt = (ti) ->
            String.format(sql, ((ViewInfo) ti).getRuleOid(), ((ViewInfo) ti).getRuleOid());
        assertRows("pg_depend for view rewrite rule of " + newTi.name,
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
        TableInfoSqlFormatter fmt = (ti) -> String.format(sql, ti.getOid(), ti.getOid());
        assertRows("pg_shdepend for " + newTi.name,
            fetchExpected.fetch(fmt), fetchActual.fetch(fmt));
      }
      {
        TableInfoSqlFormatter fmt = (ti) -> String.format(sql, ti.getTypeOid(), ti.getTypeOid());
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
        List<Long> list = new ArrayList<>(Arrays.asList(ti.getOid(), ti.getTypeOid()));
        list.addAll(ti.indexes.stream().map(p -> p.getRight()).collect(Collectors.toList()));
        return String.format(sql, StringUtils.join(list, ","));
      };
      assertRows("pg_init_privs",
          fetchExpected.fetch(fmt), fetchActual.fetch(fmt));
    }
  }

  /** Returns a new row with certain column removed. */
  private Row excluded(Row row, String colNameToRemove) {
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
  }

  /** Returns a new row with all occurrences of one value replaced with another. */
  private <T> Row replaced(Row row, T from, T to) {
    Row row2 = row.clone();
    ListIterator<Object> iter = row2.elems.listIterator();
    while (iter.hasNext()) {
      Object curr = iter.next();
      if (Objects.equals(curr, from)) {
        iter.set(to);
      }
    }
    return row2;
  }

  /** Returns a new row with substring replaced in all string columns. */
  private Row replacedString(Row row, String what, String replacement) {
    Row row2 = row.clone();
    ListIterator<Object> iter = row2.elems.listIterator();
    while (iter.hasNext()) {
      Object curr = iter.next();
      if (curr instanceof String && ((String) curr).contains(what)) {
        iter.set(((String) curr).replace(what, replacement));
      }
    }
    return row2;
  }

  /**
   * Take a "snapshot" (sorted content) of current pg_catalog state, filtering out test relations
   * and databases.
   */
  private SysCatalogSnapshot takeSysCatalogSnapshot(Statement stmt) throws Exception {
    String testRelOidsCsv = getRowList(stmt.executeQuery("SELECT oid"
        + " FROM pg_class"
        + " WHERE relname LIKE '" + SHARED_ENTITY_PREFIX + "%'"))
            .stream()
            .map(r -> r.getLong(0).toString())
            .collect(Collectors.joining(","));
    if (testRelOidsCsv.isEmpty()) {
      // SQL IN clause cannot be empty, use a safe placeholder value (OIDs are >= 0).
      testRelOidsCsv = "-1";
    }

    List<Row> tablesInfo = getSortedRowList(stmt.executeQuery("SELECT relname, relhasoids"
        + " FROM pg_class"
        + " WHERE relnamespace = 'pg_catalog'::regnamespace"
        + " AND relkind = 'r'"
        + " AND oid NOT IN (" + testRelOidsCsv + ")"));

    Map<String, List<Row>> catalog = new HashMap<>();

    for (Row tableInfoRow : tablesInfo) {
      String tableName = tableInfoRow.getString(0);
      String query;
      // Filter out stuff created for shared entities.
      switch (tableName) {
        // Databases
        case "pg_database":
          query = "SELECT oid, * FROM pg_database"
              + " WHERE datname NOT LIKE '" + SHARED_ENTITY_PREFIX + "%'";
          break;
        case "pg_shdepend":
          query = "SELECT sd.* FROM pg_shdepend sd"
              + " LEFT JOIN pg_database db ON sd.classid = 'pg_database'::regclass"
              + "   AND sd.objid = db.oid"
              + " WHERE (db.oid IS NULL OR db.datname NOT LIKE '" + SHARED_ENTITY_PREFIX + "%')";
          break;

        // Tables
        case "pg_class":
          query = "SELECT oid, * FROM pg_class"
              + " WHERE oid NOT IN (" + testRelOidsCsv + ")";
          break;
        case "pg_index":
          query = "SELECT * FROM pg_index"
              + " WHERE indrelid NOT IN (" + testRelOidsCsv + ")";
          break;
        case "pg_attribute":
          query = "SELECT * FROM pg_attribute"
              + " WHERE attrelid NOT IN (" + testRelOidsCsv + ")";
          break;
        case "pg_type":
          query = "SELECT oid, * FROM pg_type"
              + " WHERE typrelid NOT IN (" + testRelOidsCsv + ")";
          break;
        case "pg_init_privs":
          query = "SELECT ip.* FROM pg_init_privs ip"
              + " LEFT JOIN pg_class c ON ip.classoid = 'pg_class'::regclass"
              + "   AND ip.objoid = c.oid"
              + " WHERE (c.oid IS NULL OR c.oid NOT IN (" + testRelOidsCsv + "))";
          break;
        case "pg_depend":
          query = "SELECT d.* FROM pg_depend d"
              + " LEFT JOIN pg_class c ON d.refclassid = 'pg_class'::regclass"
              + "   AND d.refobjid = c.oid"
              + " LEFT JOIN pg_type t  ON d.refclassid = 'pg_type'::regclass"
              + "   AND d.refobjid = t.oid"
              + " WHERE (c.oid IS NULL OR c.oid NOT IN (" + testRelOidsCsv + "))"
              + "   AND (t.typrelid IS NULL OR t.typrelid NOT IN (" + testRelOidsCsv + "))";
          break;

        default:
          boolean hasOid = tableInfoRow.getBoolean(1);
          query = String.format("SELECT %s FROM %s", (hasOid ? "oid, *" : "*"), tableName);
      }
      List<Row> rows = getSortedRowList(stmt.executeQuery(query));
      catalog.put(
          tableName,
          rows.stream()
              .map(r -> excluded(r, "reltuples"))
              .collect(Collectors.toList()));
    }

    return new SysCatalogSnapshot(catalog);
  }

  private void assertSysCatalogSnapshotsEquals(
      SysCatalogSnapshot expected,
      SysCatalogSnapshot actual) throws Exception {
    if (expected.catalog.size() > actual.catalog.size()) {
      Set<String> difference = new TreeSet<>(expected.catalog.keySet());
      difference.removeAll(actual.catalog.keySet());
      fail("Tables lost: " + difference);
    }
    if (expected.catalog.size() < actual.catalog.size()) {
      Set<String> difference = new TreeSet<>(actual.catalog.keySet());
      difference.removeAll(expected.catalog.keySet());
      fail("Unexpected tables found: " + difference);
    }

    for (String tableName : expected.catalog.keySet()) {
      assertRows(tableName + ":", expected.catalog.get(tableName), actual.catalog.get(tableName));
    }
  }

  private void setSystemRelsModificationGuc(Statement stmt, boolean value) throws Exception {
    stmt.execute("SET ysql_upgrade_mode TO " + Boolean.toString(value));
    stmt.execute("SET yb_test_system_catalogs_creation TO " + Boolean.toString(value));
  }

  private void setAllowNonDdlTxnsGuc(Statement stmt, boolean value) throws Exception {
    stmt.execute("SET yb_non_ddl_txn_for_sys_tables_allowed TO " + Boolean.toString(value));
  }

  /** Since YB expects OIDs to never get reused, we need to always pick a previously unused OID. */
  private long newSysOid() {
    return ++LAST_USED_SYS_OID;
  }

  /** Whether this OID looks like it was auto-generated during initdb. */
  private boolean isSysGeneratedOid(Long oid) {
    return oid >= FIRST_BOOTSTRAP_OID && oid < FIRST_NORMAL_OID;
  }

  private void assertAllOidsAreSysGenerated(Statement stmt, String tableName) throws Exception {
    List<Row> rows = getRowList(stmt, "SELECT oid, * FROM " + tableName + " ORDER BY oid");
    assertTrue("Expected all rows in " + tableName
        + " to have system-generated OIDs assigned: " + rows,
        rows.stream().allMatch(r -> isSysGeneratedOid(r.getLong(0))));
  }

  private void assertSysGeneratedOid(Long oid) throws Exception {
    assertTrue(oid + " is not generated in system OID range", isSysGeneratedOid(oid));
  }

  private Long getMaxSysGeneratedOid(SysCatalogSnapshot snapshot) {
    return snapshot.catalog.get("pg_class").stream()
        .filter((classRow) -> classRow.getBoolean(18 /* relhasoids, with reltuples filtered out */))
        .mapToLong((classRow) -> {
          String tableName = classRow.getString(1);
          return snapshot.catalog.get(tableName).stream()
              .mapToLong((r) -> r.getLong(0))
              .filter(this::isSysGeneratedOid)
              .max().orElse(0L);
        })
        .max().orElse(0L);
  }

  /**
   * Given two system catalog snapshots (fresh one created by reinitdb, and an older one migrated to
   * the latest), verify that they are equivalent for all practical purposes (e.g. OIDs from
   * auto-generated range might differ).
   * <p>
   * Main idea here is simple - we "resolve" OIDs referenced in system table rows, replacing them
   * with entity names, so that values can be compared ignoring OID differences.
   * <p>
   * Also check that migration populates history table up to the latest.
   */
  private void assertMigrationsWorked(
      SysCatalogSnapshot freshSnapshot,
      SysCatalogSnapshot migratedSnapshot) {
    assertCollectionSizes("Migrated table set differs from the fresh one!",
        freshSnapshot.catalog.keySet(), migratedSnapshot.catalog.keySet());

    {
      // Reinitdb migrations table has just one baseline row.
      List<Row> reinitdbMigrations = freshSnapshot.catalog.get(MIGRATIONS_TABLE);
      assertEquals(
          "Expected \"fresh\" migrations table to have just one row, got " + reinitdbMigrations,
          1, reinitdbMigrations.size());
      final int latestMajorVersion = reinitdbMigrations.get(0).getInt(0);
      final int latestMinorVersion = reinitdbMigrations.get(0).getInt(1);
      final int totalMigrations = latestMajorVersion + latestMinorVersion;
      assertRow(new Row(latestMajorVersion, latestMinorVersion, "<baseline>", null),
                reinitdbMigrations.get(0));

      // Applied migrations table has a baseline row
      // followed by rows for all migrations (up to the latest).
      List<Row> appliedMigrations = migratedSnapshot.catalog.get(MIGRATIONS_TABLE);
      assertEquals(
          "Expected applied migrations table to have exactly "
              + (totalMigrations + 1) + " rows, got " + appliedMigrations,
          totalMigrations + 1, appliedMigrations.size());
      assertRow(new Row(0, 0, "<baseline>", null), appliedMigrations.get(0));
      for (int ver = 1; ver <= totalMigrations; ++ver) {
        // Rows should be like [1, 0, 'V1__...', <recent timestamp in ms>]
        Row migrationRow = appliedMigrations.get(ver);
        final int majorVersion = Math.min(ver, latestMajorVersion);
        final int minorVersion = ver - majorVersion;
        assertEquals(majorVersion, migrationRow.getInt(0).intValue());
        assertEquals(minorVersion, migrationRow.getInt(1).intValue());
        String migrationNamePrefix;
        if (minorVersion > 0) {
          migrationNamePrefix = "V" + majorVersion + "." + minorVersion + "__";
        } else {
          migrationNamePrefix = "V" + majorVersion + "__";
        }
        assertTrue(migrationRow.getString(2).startsWith(migrationNamePrefix));
        assertTrue("Expected migration timestamp to be at most 10 mins old!",
            migrationRow.getLong(3) != null &&
                System.currentTimeMillis() - migrationRow.getLong(3) < 10 * 60 * 1000);
      }
    }

    // reltuples column is filtered out, so column index is one less than normal.
    final int relkindColIdx = 15;
    final int relhasoidsColIdx = 18;

    Map<String, Row> pgClassByNameMap = freshSnapshot.catalog.get("pg_class").stream()
        .collect(Collectors.toMap((r) -> r.getString(1), (r) -> r));

    // Replace referenced OIDs with entity names, do a few more simplifications.
    Function<SysCatalogSnapshot, SysCatalogSnapshot> simplifyCatalogSnapshot = (snapshot) -> {
      Map<Long, String> pgTypeNameByOidMap = snapshot.catalog.get("pg_type").stream()
          .collect(Collectors.toMap((r) -> r.getLong(0), (r) -> r.getString(1)));
      Map<Long, String> pgOpfamilyNameByOidMap = snapshot.catalog.get("pg_opfamily").stream()
          .collect(Collectors.toMap((r) -> r.getLong(0), (r) -> r.getString(2)));

      Map<Long, Map<Long, String>> entityNamesMap = snapshot.catalog.get("pg_class").stream()
          .filter((classRow) -> classRow.getBoolean(relhasoidsColIdx))
          .collect(Collectors.toMap(
              (classRow) -> classRow.getLong(0),
              (classRow) -> {
                String tableName = classRow.getString(1);
                return snapshot.catalog.get(tableName).stream()
                    .collect(Collectors.toMap(
                        (r) -> r.getLong(0),
                        (r) -> {
                          switch (tableName) {
                            case "pg_amop":
                            case "pg_amproc":
                              return String.format("%s for %s",
                                  tableName,
                                  pgOpfamilyNameByOidMap.get(r.getLong(1)));
                            case "pg_cast":
                              return String.format("pg_cast for %s->%s",
                                  pgTypeNameByOidMap.get(r.getLong(1)),
                                  pgTypeNameByOidMap.get(r.getLong(2)));
                            case "pg_opclass":
                            case "pg_opfamily":
                              return r.getString(2);
                            default:
                              return r.getString(1);
                          }
                        }));
              }));

      Map<String, List<Row>> simplifiedCatalog = new HashMap<>();
      snapshot.catalog.forEach((tableName, rows) -> {
        simplifiedCatalog.put(tableName, copyWithResolvedOids(tableName, rows, entityNamesMap));
      });
      // Replace auto-generated OIDs with placeholders.
      simplifiedCatalog.forEach((tableName, rows) -> {
        if (pgClassByNameMap.get(tableName).getBoolean(relhasoidsColIdx)) {
          rows.stream()
              .filter((r) -> isSysGeneratedOid(r.getLong(0)))
              .forEach((r) -> r.elems.set(0, PLACEHOLDER_OID));
          Collections.sort(rows);
        }
      });
      return new SysCatalogSnapshot(simplifiedCatalog);
    };

    SysCatalogSnapshot simplifiedFreshSnapshot = simplifyCatalogSnapshot.apply(freshSnapshot);
    SysCatalogSnapshot simplifiedMigratedSnapshot = simplifyCatalogSnapshot.apply(migratedSnapshot);

    List<String> tablesToSkip = Arrays.asList(MIGRATIONS_TABLE, CATALOG_VERSION_TABLE);

    simplifiedMigratedSnapshot.catalog.forEach((tableName, migratedRows) -> {
      if (tablesToSkip.contains(tableName))
        return;

      // Snapshot only contains data for real tables.
      if (!pgClassByNameMap.get(tableName).getString(relkindColIdx).equals("r"))
        return;

      List<Row> reinitdbRows = simplifiedFreshSnapshot.catalog.get(tableName);

      assertCollectionSizes("Table '" + tableName + "' has different size after migration!",
          reinitdbRows, migratedRows);

      for (int i = 0; i < migratedRows.size(); ++i) {
        assertRow("Table '" + tableName + "': ", reinitdbRows.get(i), migratedRows.get(i));
      }
    });
  }

  /**
   * In issue #12258 we found that index's ybbasectid referencing table's ybctid was broken for
   * shared inserts (operations used to insert a row across all databases) and indexed tables
   * without primary key.
   * <p>
   * This test ensures this is no longer the case.
   */
  private void assertYbbasectidIsConsistent(String databaseName) throws Exception {
    try (Connection conn = getConnectionBuilder().withDatabase(databaseName).connect();
         Statement stmt = conn.createStatement()) {
      // pg_depend has no primary key, so we use it and its pg_depend_reference_index to ensure
      // index consistency after upgrade.
      String seqScanSql = "SELECT * FROM pg_depend";
      assertFalse(isIndexScan(stmt, seqScanSql, "" /* should not use any index */));
      List<Row> seqScanRows =
          getRowList(stmt, seqScanSql)
              .stream()
              .filter(r -> r.getLong(3) == 1259L /* pg_class oid*/)
              .filter(r -> r.getLong(4) >= FIRST_YB_OID && r.getLong(4) < FIRST_BOOTSTRAP_OID)
              .sorted()
              .collect(Collectors.toList());
      assertFalse(seqScanRows.isEmpty());

      // Without a fix for #12258, this query results in "DocKey(...) not found in indexed table".
      String indexScanSql =
          "SELECT * FROM pg_depend"
              + " WHERE refclassid = 1259"
              + " AND refobjid >= " + FIRST_YB_OID
              + " AND refobjid < " + FIRST_BOOTSTRAP_OID;
      assertTrue(isIndexScan(stmt, indexScanSql, "pg_depend_reference_index"));
      List<Row> indexScanRows = getRowList(stmt, indexScanSql);
      Collections.sort(indexScanRows);

      assertEquals(seqScanRows, indexScanRows);
    }
  }

  /** Returns the deep copy with referenced OIDs replaced with entity names. */
  private List<Row> copyWithResolvedOids(
      String tableName,
      List<Row> rows,
      Map<Long, Map<Long, String>> entityNamesMap) {
    List<Row> copy = deepCopyRows(rows);

    if (copy.isEmpty())
      return copy;

    final long pgTypeOid = 1247;
    final long pgProcOid = 1255;
    final long pgClassOid = 1259;
    final long pgNamespaceOid = 2615;
    final long pgTsDictOid = 3600;
    final long pgTsConfigOid = 3602;
    final long pgTsTemplateOid = 3764;

    Map<Long, String> flatEntityNamesMap = new HashMap<>();
    entityNamesMap.values().forEach(flatEntityNamesMap::putAll);

    /** Replace OIDs at the given position with resolved entity names from the given catalog. */
    BiConsumer<Integer, Map<Long, String>> replace = (oidColIdx, catalogRowByOidMap) -> {
      for (Row row : copy) {
        Long oid = row.getLong(oidColIdx);
        if (oid != null && oid != 0L) {
          assertTrue("Unknown OID " + oid, catalogRowByOidMap.containsKey(oid));
          row.elems.set(oidColIdx, catalogRowByOidMap.get(oid));
        } else {
          row.elems.set(oidColIdx, null);
        }
      }
    };

    /**
     * Given two OID columns, one denoting a table and the other - an entity in that table, replace
     * an entity OID with its resolved name.
     */
    BiConsumer<Integer, Integer> replaceByClass = (regclassColIdx, oidColIdx) -> {
      for (Row row : copy) {
        Long classOid = row.getLong(regclassColIdx);
        if (classOid != null && classOid != 0L) {
          assertTrue("Unknown pg_class OID " + classOid, entityNamesMap.containsKey(classOid));
          Long entityOid = row.getLong(oidColIdx);
          assertTrue(entityOid != null && entityOid > 0L);
          row.elems.set(oidColIdx, entityNamesMap.get(classOid).get(entityOid));
        } else {
          row.elems.set(oidColIdx, null);
        }
      }
    };

    /**
     * In pg_node_tree string-wrapping structure:
     * <ul>
     * <li>Replace auto-generated OIDs with resolved names.
     * <li>Replace values for :location, :stmt_location and :stmt_len with placeholders.
     * </ul>
     * Format the result as a plain string. We don't care about minor formatting losses as long as
     * it's consistent.
     */
    Consumer<Integer> simplifyPgNodeTree = (nodeTreeColIdx) -> {
      for (Row row : copy) {
        String nodeTree = ((PGobject) row.get(nodeTreeColIdx)).getValue();
        String[] nodeTreeParts = nodeTree.split("\\s+");
        for (int i = 0; i < nodeTreeParts.length; i++) {
          if (nodeTreeParts[i].matches("1\\d{4}\\)?" /* 10000 to 19999 for simplicity */)) {
            /* There may be ending ')' such as "13032)" and we need to remove the trailing ')'. */
            long oid = Long.parseLong(nodeTreeParts[i].replaceAll("\\)", ""));
            if (isSysGeneratedOid(oid)) {
              nodeTreeParts[i] = flatEntityNamesMap.get(oid);
            }
          } else if (nodeTreeParts[i].equals(":location")) {
            nodeTreeParts[i + 1] = "<location>";
          } else if (nodeTreeParts[i].equals(":stmt_location")) {
            nodeTreeParts[i + 1] = "<stmt_location>";
          } else if (nodeTreeParts[i].equals(":stmt_len")) {
            nodeTreeParts[i + 1] = "<stmt_len>";
          }
        }
        row.elems.set(nodeTreeColIdx, StringUtils.join(nodeTreeParts, " "));
      }
    };

    // NOTE: There are definitely more tables requiring special treatment like that, but those
    //       aren't affected by migrations, so we don't care (yet).
    switch (tableName) {
      case "pg_class":
        replace.accept(2 /* relnamespace */, entityNamesMap.get(pgNamespaceOid));
        replace.accept(3 /* reltype */, entityNamesMap.get(pgTypeOid));
        replace.accept(7 /* relfilenode */, entityNamesMap.get(pgClassOid));
        break;
      case "pg_type":
        replace.accept(2 /* typnamespace */, entityNamesMap.get(pgNamespaceOid));
        replace.accept(11 /* typrelid */, entityNamesMap.get(pgClassOid));
        replace.accept(12 /* typelem */, entityNamesMap.get(pgTypeOid));
        replace.accept(13 /* typarray */, entityNamesMap.get(pgTypeOid));
        break;
      case "pg_attribute":
        replace.accept(0 /* attrelid */, entityNamesMap.get(pgClassOid));
        replace.accept(2 /* atttypid */, entityNamesMap.get(pgTypeOid));
        break;
      case "pg_description":
      case "pg_shdescription":
      case "pg_init_privs":
        replaceByClass.accept(1 /* classoid */, 0 /* objoid */);
        break;
      case "pg_depend":
        replaceByClass.accept(0 /* classid */, 1 /* objid */);
        replaceByClass.accept(3 /* refclassid */, 4 /* refobjid */);
        break;
      case "pg_rewrite":
        replace.accept(2 /* ev_class */, entityNamesMap.get(pgClassOid));
        simplifyPgNodeTree.accept(7 /* ev_action */);
        break;
      case "pg_constraint":
        replace.accept(2 /* connamespace */, entityNamesMap.get(pgNamespaceOid));
        replace.accept(8 /* contypid */, entityNamesMap.get(pgTypeOid));
        break;
      case "pg_language":
        replace.accept(5 /* lanplcallfoid */, entityNamesMap.get(pgProcOid));
        replace.accept(6 /* laninline */, entityNamesMap.get(pgProcOid));
        replace.accept(7 /* lanvalidator */, entityNamesMap.get(pgProcOid));
        break;
      case "pg_proc":
        replace.accept(2 /* pronamespace */, entityNamesMap.get(pgNamespaceOid));
        break;
      case "pg_ts_dict":
        replace.accept(4 /* dicttemplate */, entityNamesMap.get(pgTsTemplateOid));
        break;
      case "pg_ts_config_map":
        replace.accept(0 /* mapcfg */, entityNamesMap.get(pgTsConfigOid));
        replace.accept(3 /* mapdict */, entityNamesMap.get(pgTsDictOid));
        break;
      default:
        return copy;
    }
    Collections.sort(copy);
    return copy;
  }

  private void runMigrations(boolean useSingleConnection) throws Exception {
    // Set upgrade timeout to 7 (11) minutes, adjusted.
    // Single-connection upgrade takes longer because of new connection overhead.
    long timeoutMs = BuildTypeUtil.adjustTimeout((useSingleConnection ? 11 : 7) * 60 * 1000);
    List<String> command = new ArrayList<>(Arrays.asList(
        TestUtils.findBinary("yb-admin"),
        "--master_addresses",
        masterAddresses,
        "-timeout_ms",
        String.valueOf(timeoutMs),
        "upgrade_ysql"));
    if (useSingleConnection) {
      command.add("use_single_connection");
    }
    List<String> lines = runProcess(command);
    String joined = String.join("\n", lines);
    if (!joined.toLowerCase().contains("successfully upgraded")) {
      throw new IllegalStateException("Unexpected migrations result: " + joined);
    }
  }

  private void setupGeoPartitioning() throws Exception {
    // Setup tablespaces and tables to emulate a geo-partitioned setup.
    try (Statement stmt = connection.createStatement()) {
      for (Map<String, String> tserverPlacement : perTserverZonePlacementFlags) {
        String cloud = tserverPlacement.get("placement_cloud");
        String region = tserverPlacement.get("placement_region");
        String zone = tserverPlacement.get("placement_zone");
        String suffix = cloud + "_" + region + "_" + zone;

        stmt.execute(
            "CREATE TABLESPACE tablespace_" + suffix + " WITH (replica_placement=" +
            "'{\"num_replicas\": 1, \"placement_blocks\":" +
            "[{\"cloud\":\"" + cloud + "\",\"region\":\"" + region + "\",\"zone\":\"" + zone +
            "\",\"min_num_replicas\":1}]}')");
        stmt.execute("CREATE TABLE table_" + suffix + " (a int) TABLESPACE tablespace_" + suffix);
      }
    }

    // Wait for tablespace info to be refreshed in load balancer.
    Thread.sleep(MASTER_REFRESH_TABLESPACE_INFO_SECS * 1000); // TODO(esheng) 2x?

    int expectedTServers = miniCluster.getTabletServers().size() + 1;
    miniCluster.startTServer(perTserverZonePlacementFlags.get(1));
    miniCluster.waitForTabletServers(expectedTServers);

    // Wait for loadbalancer to run.
    assertTrue(miniCluster.getClient().waitForLoadBalancerActive(
      MASTER_LOAD_BALANCER_WAIT_TIME_MS));

    // Wait for load balancer to become idle.
    assertTrue(miniCluster.getClient().waitForLoadBalance(Long.MAX_VALUE, expectedTServers));
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

    // OIDs, 0 means not known in advance or not applicable.
    private long oid;
    private long typeOid;

    public final List<Pair<String, Long>> indexes;

    public TableInfo(String name, long oid, long typeOid, List<Pair<String, Long>> indexes) {
      this.name = name;
      this.setOid(oid);
      this.setTypeOid(typeOid);
      this.indexes = Collections.unmodifiableList(new ArrayList<>(indexes));
    }

    public long getOid() {
      return oid;
    }

    public void setOid(long oid) {
      this.oid = oid;
    }

    public long getTypeOid() {
      return typeOid;
    }

    public void setTypeOid(long typeOid) {
      this.typeOid = typeOid;
    }
  }

  private class ViewInfo extends TableInfo {
    private long ruleOid = 0L;

    public ViewInfo(String name) {
      super(name, 0L, 0L, Collections.emptyList());
    }

    public long getRuleOid() {
      return ruleOid;
    }

    public void setRuleOid(long ruleOid) {
      this.ruleOid = ruleOid;
    }
  }

  /** A "snapshot" (sorted content) of pg_catalog. */
  private class SysCatalogSnapshot {
    /** We expect rows to be sorted according to Row default ordering. */
    public final Map<String, List<Row>> catalog;

    public SysCatalogSnapshot(Map<String, List<Row>> catalog) {
      this.catalog = new TreeMap<>(catalog);
    }
  }
}
