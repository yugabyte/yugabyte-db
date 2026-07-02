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

import static org.yb.AssertionWrappers.*;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.sql.Connection;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import org.yb.YBTestRunner;
import org.yb.util.ProcessUtil;
import org.yb.util.SkipOnASAN;
import org.yb.util.SkipOnTSAN;

/**
 * Tests the legacy ysql_dump split/yb_presplit format produced when the
 * {@code yb_dump_presplit_in_create} AutoFlag is NOT promoted (i.e. during the
 * upgrade monitoring phase, before finalize).
 *
 * <p>In this state ysql_dump must emit the older, restore-compatible form:
 * yb_presplit is kept out of the CREATE statement's WITH clause and re-emitted
 * as a separate {@code ALTER TABLE/INDEX ... SET (yb_presplit=...)}, and the
 * empty-string suppress-auto-derive sentinel is never emitted.  This is what
 * lets a backup taken on an upgraded-but-not-finalized cluster restore on an
 * older version after a rollback (older versions reject yb_presplit alongside a
 * SPLIT clause).  The promoted (folded) form is covered by
 * {@link TestYsqlDumpSplitOptions}.
 *
 * The AutoFlag is forced off by setting the backing gflag explicitly; a fresh
 * cluster would otherwise promote it to its target value.
 */
@SkipOnTSAN
@SkipOnASAN
@RunWith(value = YBTestRunner.class)
public class TestYsqlDumpSplitOptionsLegacy extends BasePgSQLTest {
  private static final Logger LOG =
      LoggerFactory.getLogger(TestYsqlDumpSplitOptionsLegacy.class);

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    // Force the AutoFlag to its un-promoted (initial) value so ysql_dump uses
    // the legacy ALTER ... SET (yb_presplit=...) form.
    flagMap.put("ysql_yb_dump_presplit_in_create", "false");
    return flagMap;
  }

  @Override
  public int getTestMethodTimeoutSec() {
    return super.getTestMethodTimeoutSec() * 5;
  }

  private String runDump(String dbName) throws Exception {
    File pgBinDir = PgRegressBuilder.getPgBinDir();
    File ysqlDumpExec = new File(pgBinDir, "ysql_dump");
    File dumpFile = File.createTempFile("legacy_split_dump", ".sql");
    dumpFile.deleteOnExit();

    List<String> dumpArgs = Arrays.asList(
        ysqlDumpExec.toString(),
        "-h", getPgHost(0),
        "-p", Integer.toString(getPgPort(0)),
        "-U", DEFAULT_PG_USER,
        "-d", dbName,
        "-f", dumpFile.toString(),
        "--include-yb-metadata");
    LOG.info("Running ysql_dump: " + dumpArgs);
    ProcessUtil.executeSimple(dumpArgs, "ysql_dump");
    return new String(Files.readAllBytes(dumpFile.toPath()), StandardCharsets.UTF_8);
  }

  /**
   * With the AutoFlag off, ysql_dump emits the legacy ALTER form and never the
   * folded WITH (yb_presplit=...) / empty sentinel, and the dump restores
   * cleanly.
   */
  @Test
  public void testLegacyDumpFormatWhenAutoFlagOff() throws Exception {
    final String sourceDb = "legacy_split_src_db";
    final String targetDb = "legacy_split_tgt_db";

    try (Statement stmt = connection.createStatement()) {
      stmt.executeUpdate("CREATE DATABASE " + sourceDb);
      stmt.executeUpdate("CREATE DATABASE " + targetDb);
    }

    try (Connection conn = getConnectionBuilder().withDatabase(sourceDb).connect();
         Statement stmt = conn.createStatement()) {
      // Hash table with an explicit SPLIT INTO -> auto-derives yb_presplit=5.
      stmt.executeUpdate(
          "CREATE TABLE split_table (k INT PRIMARY KEY, v INT) SPLIT INTO 5 TABLETS");
      // Hash table with no SPLIT -> no yb_presplit on the source.
      stmt.executeUpdate("CREATE TABLE no_split_table (k INT PRIMARY KEY, v INT)");
      // Secondary index with an explicit SPLIT INTO -> auto-derives yb_presplit=4.
      stmt.executeUpdate("CREATE TABLE indexed_table (k INT PRIMARY KEY, v INT)");
      stmt.executeUpdate(
          "CREATE INDEX split_index ON indexed_table(v) SPLIT INTO 4 TABLETS");
    }

    String dump = runDump(sourceDb);
    LOG.info("Legacy dump:\n" + dump);

    // The legacy form must never emit the empty-string sentinel...
    assertFalse("Legacy dump must not emit the yb_presplit='' sentinel; dump=\n" + dump,
        dump.contains("yb_presplit=''"));
    // ...and must re-emit yb_presplit via separate ALTER statements that an
    // older restore target accepts.
    assertTrue("Legacy dump should re-emit the table's yb_presplit via ALTER TABLE SET; dump=\n"
            + dump,
        dump.contains("ALTER TABLE public.split_table SET (yb_presplit='5')"));
    assertTrue("Legacy dump should re-emit the index's yb_presplit via ALTER INDEX SET; dump=\n"
            + dump,
        dump.contains("ALTER INDEX public.split_index SET (yb_presplit='4')"));

    // Sanity: the legacy dump restores cleanly (ON_ERROR_STOP=1) and preserves
    // the explicitly-split table's yb_presplit value.
    File pgBinDir = PgRegressBuilder.getPgBinDir();
    File ysqlshExec = new File(pgBinDir, "ysqlsh");
    File dumpFile = File.createTempFile("legacy_split_restore", ".sql");
    dumpFile.deleteOnExit();
    Files.write(dumpFile.toPath(), dump.getBytes(StandardCharsets.UTF_8));

    List<String> restoreArgs = new ArrayList<>(Arrays.asList(
        ysqlshExec.toString(),
        "-h", getPgHost(0),
        "-p", Integer.toString(getPgPort(0)),
        "-U", DEFAULT_PG_USER,
        "-d", targetDb,
        "-f", dumpFile.getAbsolutePath(),
        "-v", "ON_ERROR_STOP=1"));
    ProcessUtil.executeSimple(restoreArgs, "ysqlsh (restore)");

    try (Connection conn = getConnectionBuilder().withDatabase(targetDb).connect();
         Statement stmt = conn.createStatement()) {
      String reloptions = getReloptions(stmt, "split_table");
      assertNotNull("Restored split_table should have reloptions", reloptions);
      assertTrue("Restored split_table should preserve yb_presplit=5; got: " + reloptions,
          reloptions.contains("yb_presplit=5"));
    }

    try (Statement stmt = connection.createStatement()) {
      stmt.executeUpdate("DROP DATABASE " + sourceDb);
      stmt.executeUpdate("DROP DATABASE " + targetDb);
    }
  }

  private String getReloptions(Statement stmt, String relationName) throws Exception {
    try (java.sql.ResultSet rs = stmt.executeQuery(
        String.format("SELECT reloptions FROM pg_class WHERE relname = '%s'", relationName))) {
      if (rs.next()) {
        return rs.getString("reloptions");
      }
      return null;
    }
  }
}
