// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied. See the License for the specific language governing permissions and limitations
// under the License.
//

package org.yb.ysqlconnmgr;

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Set;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.pgsql.ConnectionBuilder;

/**
 * Chooses which backend a logical client's next transaction runs on, by occupying backends of
 * the client's pool with holder clients that sit in open transactions. Connection Manager does
 * not hand a backend that is inside one client's transaction to another client, so:
 * <ul>
 *   <li>{@link #hold} makes the client's next transaction run on some other backend;</li>
 *   <li>{@link #steerTo} occupies every other backend, so the client's next transaction runs on
 *       the given one.</li>
 * </ul>
 * The builder must use the client's user and database, so that holders draw from the client's
 * pool. Use a non-superuser: with ysql_conn_mgr_superuser_sticky, which defaults to true but
 * BaseYsqlConnMgr sets to false, every superuser client is pinned to one backend. A backend that a
 * sticky client owns can be neither held nor steered to; both calls return false then.
 *
 * The observer is a direct (not pooled) connection to the same node, used to list backends.
 * Holders cannot do that themselves: pg_stat_activity is read once per transaction.
 */
public class BackendSteering implements AutoCloseable {
  private static final Logger LOG = LoggerFactory.getLogger(BackendSteering.class);

  private static final String WORKER_PIDS_QUERY =
      "SELECT pid FROM pg_stat_activity" +
      " WHERE backend_type = 'yb-conn-mgr worker connection' AND datname = current_database()";

  // Bounds the search if the pool keeps handing out backends we have not seen.
  private static final int MAX_HOLDERS = 64;

  // A holder sees its ROLLBACK complete before Connection Manager has put the backend back in
  // the idle list; a client query sent in that gap gets another backend.
  private static final long RELEASE_SETTLE_MS = 200;

  private final ConnectionBuilder builder;
  private final Connection observer;
  private final Map<Integer, Connection> holders = new LinkedHashMap<>();

  public BackendSteering(ConnectionBuilder builder, Connection observer) {
    this.builder = builder;
    this.observer = observer;
  }

  public static int backendPid(Statement stmt) throws SQLException {
    try (ResultSet rs = stmt.executeQuery("SELECT pg_backend_pid()")) {
      rs.next();
      return rs.getInt(1);
    }
  }

  /** Occupies backend pid. Returns false if pid was not idle in the pool. */
  public boolean hold(int pid) throws Exception {
    return holdUntil(pid);
  }

  /**
   * Occupies every idle backend of the pool except pid, so pid is the only idle one left.
   * Returns false if pid was not idle in the pool.
   */
  public boolean steerTo(int pid) throws Exception {
    holdUntil(-1);
    if (!holders.containsKey(pid)) {
      return false;
    }
    release(pid);
    return true;
  }

  /** Ends the holder transaction on pid, returning that backend to the pool. */
  public void release(int pid) throws Exception {
    try (Connection conn = holders.remove(pid);
         Statement stmt = conn.createStatement()) {
      stmt.execute("ROLLBACK");
    }
    Thread.sleep(RELEASE_SETTLE_MS);
  }

  /** Releases every held backend. */
  @Override
  public void close() throws Exception {
    Exception first = null;
    for (Integer pid : holders.keySet().toArray(new Integer[0])) {
      try (Connection conn = holders.remove(pid);
           Statement stmt = conn.createStatement()) {
        stmt.execute("ROLLBACK");
      } catch (Exception e) {
        if (first == null) {
          first = e;
        } else {
          first.addSuppressed(e);
        }
      }
    }
    Thread.sleep(RELEASE_SETTLE_MS);
    if (first != null) {
      throw first;
    }
  }

  /**
   * Adds holders until one lands on target, or until the pool has no idle backend left. The pool
   * is out of idle backends once a holder lands on a backend that did not exist before the first
   * holder connected, as Connection Manager spawns a backend only when none is idle.
   */
  private boolean holdUntil(int target) throws Exception {
    if (holders.containsKey(target)) {
      return true;
    }
    Set<Integer> existing = workerPids();
    while (holders.size() < MAX_HOLDERS) {
      Connection conn = builder.connect();
      int pid;
      try (Statement stmt = conn.createStatement()) {
        stmt.execute("BEGIN");
        pid = backendPid(stmt);
      } catch (Exception e) {
        conn.close();
        throw e;
      }
      holders.put(pid, conn);
      if (pid == target) {
        return true;
      }
      if (!existing.contains(pid)) {
        LOG.info("Pool out of idle backends: held {}, backends before holding {}, target {}",
                 holders.keySet(), existing, target);
        return false;
      }
    }
    throw new IllegalStateException("Pool kept handing out backends after " + MAX_HOLDERS +
                                    " holders; held " + holders.keySet());
  }

  private Set<Integer> workerPids() throws SQLException {
    Set<Integer> pids = new HashSet<>();
    try (Statement stmt = observer.createStatement();
         ResultSet rs = stmt.executeQuery(WORKER_PIDS_QUERY)) {
      while (rs.next()) {
        pids.add(rs.getInt(1));
      }
    }
    return pids;
  }
}
