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

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.Statement;
import java.util.Map;

/**
 * Common cluster configuration and query helpers for the logical replication
 * apply-worker lifecycle tests.  Subclasses choose the cluster shape
 * (getInitialNumTServers / getReplicationFactor) and how they hold
 * connections.
 */
public abstract class BasePgSubscriptionLifecycleTest extends BasePgSQLTest {
  protected static final String PUB_DB = "pub_db";
  protected static final String SUB_DB = "sub_db";
  protected static final String SUB_NAME = "test_sub";
  protected static final String PUB_NAME = "test_pub";
  protected static final int POLL_INTERVAL_MS = 500;

  @Override
  public int getTestMethodTimeoutSec() {
    return 600;
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    appendToYsqlPgConf(flagMap, "yb_enable_pg_subscription=true");
    // The production default (60s) only slows the takeover and restart waits
    // in the multi-node suite; probe at the worker's reread cadence instead.
    appendToYsqlPgConf(flagMap,
                       "yb_subscription_takeover_probe_interval_ms=10000");
    flagMap.put("ysql_cdc_active_replication_slot_window_ms", "0");
    flagMap.put("cdc_send_null_before_image_if_not_exists", "true");
    flagMap.put("TEST_dcheck_for_missing_schema_packing", "false");
    // The apply worker takes a cluster-wide advisory lock to exclude workers
    // on other nodes, and refuses to start without one.
    flagMap.put("ysql_yb_enable_advisory_locks", "true");
    // DROP SUBSCRIPTION drops the slot with WAIT, which waits on the slot's
    // cluster-wide advisory lock.
    flagMap.put("allowed_preview_flags_csv",
                "ysql_yb_enable_replication_slot_exclusive_lock");
    flagMap.put("ysql_yb_enable_replication_slot_exclusive_lock", "true");
    return flagMap;
  }

  @Override
  protected Map<String, String> getMasterFlags() {
    Map<String, String> flagMap = super.getMasterFlags();
    flagMap.put("TEST_dcheck_for_missing_schema_packing", "false");
    flagMap.put("ysql_yb_enable_advisory_locks", "true");
    return flagMap;
  }

  /** CREATE SUBSCRIPTION statement pointing at PUB_DB on tserver 0. */
  protected String createSubscriptionSql(String subName, String pubName,
                                         boolean copyData) {
    return createSubscriptionSql(subName, pubName,
                                 "copy_data = " + (copyData ? "true" : "false"));
  }

  /** Same, with a caller-supplied WITH (...) option list. */
  protected String createSubscriptionSql(String subName, String pubName,
                                         String withOptions) {
    return String.format(
        "CREATE SUBSCRIPTION %s CONNECTION 'host=%s port=%d dbname=%s user=%s' "
        + "PUBLICATION %s WITH (%s)",
        subName, getPgHost(0), getPgPort(0), PUB_DB, TEST_PG_USER, pubName,
        withOptions);
  }

  /**
   * Returns true if the node behind conn reports a running apply worker for
   * subName.  pg_stat_subscription is per-node, so the connection decides
   * which node is inspected.  relid IS NULL distinguishes the apply worker
   * from tablesync workers, which report the table they are copying.
   */
  protected static boolean workerAlive(Connection conn, String subName)
      throws Exception {
    try (Statement stmt = conn.createStatement();
         ResultSet rs = stmt.executeQuery(
             "SELECT pid FROM pg_stat_subscription "
             + "WHERE subname = '" + subName + "' AND pid IS NOT NULL "
             + "AND relid IS NULL")) {
      return rs.next();
    }
  }

  /** Apply worker PID on the node behind conn, or -1 if none is running. */
  protected static int applyWorkerPid(Connection conn, String subName)
      throws Exception {
    try (Statement stmt = conn.createStatement();
         ResultSet rs = stmt.executeQuery(
             "SELECT pid FROM pg_stat_subscription "
             + "WHERE subname = '" + subName + "' AND pid IS NOT NULL "
             + "AND relid IS NULL")) {
      return rs.next() ? rs.getInt(1) : -1;
    }
  }

  /**
   * True if the node behind conn has an apply worker that has connected to the
   * publisher.  A worker that lost the subscription advisory lock exits before
   * walrcv_connect, so it never reports received_lsn: this distinguishes the
   * one real worker from a transient loser that raced another node for a
   * freshly freed lock.
   */
  protected static boolean streamingWorkerAlive(Connection conn, String subName)
      throws Exception {
    try (Statement stmt = conn.createStatement();
         ResultSet rs = stmt.executeQuery(
             "SELECT pid FROM pg_stat_subscription "
             + "WHERE subname = '" + subName + "' AND pid IS NOT NULL "
             + "AND relid IS NULL AND received_lsn IS NOT NULL")) {
      return rs.next();
    }
  }

  /** Number of tablesync workers currently running on the node behind conn. */
  protected static int tablesyncWorkerCount(Connection conn, String subName)
      throws Exception {
    try (Statement stmt = conn.createStatement();
         ResultSet rs = stmt.executeQuery(
             "SELECT count(*) FROM pg_stat_subscription "
             + "WHERE subname = '" + subName + "' AND pid IS NOT NULL "
             + "AND relid IS NOT NULL")) {
      return rs.next() ? rs.getInt(1) : 0;
    }
  }
}
