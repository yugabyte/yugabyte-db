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
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class CacheMemoryContextTracker {
  private static final Logger LOG = LoggerFactory.getLogger(CacheMemoryContextTracker.class);

  private final Connection connection;
  private long initialBytes;
  private long finalBytes;

  public CacheMemoryContextTracker(Connection connection, long initialBytes) throws SQLException {
    this.connection = connection;
    this.initialBytes = initialBytes;
    measureFinalBytes();
  }


  public CacheMemoryContextTracker(Connection connection) throws SQLException {
    this.connection = connection;
    measureInitialBytes();
  }

  /**
   * Gets the memory usage of the cache memory context in bytes. Note that child memory contexts of
   * the cache memory context are not included in this total.
   *
   * @return CacheMemoryContext total_bytes
   * @throws SQLException if the query fails
   */
  public long getCacheMemoryUsageBytes() throws SQLException {
    try (Statement statement = connection.createStatement()) {
      statement.execute(
          "SELECT used_bytes " +
               " FROM pg_get_backend_memory_contexts() " +
               "WHERE name = 'CacheMemoryContext'");
      try (ResultSet rs = statement.getResultSet()) {
        assertTrue(rs.next());
        return rs.getInt(1);
      }
    }
  }

  public long measureInitialBytes() throws SQLException {
    initialBytes = getCacheMemoryUsageBytes();
    LOG.info("Initial cache memory usage: {}", initialBytes);
    return initialBytes;
  }

  public long measureFinalBytes() throws SQLException {
    finalBytes = getCacheMemoryUsageBytes();
    LOG.info("Final cache memory usage: {}", finalBytes);
    return finalBytes;
  }

  public void assertMemoryUsageGreaterThan(long expectedIncrease) {
    long actualIncrease = finalBytes - initialBytes;
    assertGreaterThan(actualIncrease, expectedIncrease);
  }

  public void assertMemoryUsageLessThan(long expectedIncrease) {
    long actualIncrease = finalBytes - initialBytes;
    assertLessThan(actualIncrease, expectedIncrease);
  }

  public Connection getConnection() {
    return connection;
  }
}
