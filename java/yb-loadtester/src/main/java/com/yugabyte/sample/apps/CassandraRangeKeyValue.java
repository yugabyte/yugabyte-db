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

package com.yugabyte.sample.apps;

import java.nio.ByteBuffer;
import java.util.Arrays;
import java.util.List;

import com.yugabyte.sample.common.SimpleLoadGenerator;
import org.apache.log4j.Logger;

import com.datastax.driver.core.BoundStatement;
import com.datastax.driver.core.ConsistencyLevel;
import com.datastax.driver.core.PreparedStatement;
import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;
import com.yugabyte.sample.common.SimpleLoadGenerator.Key;

/**
 * This workload writes and reads some random string keys from a CQL server. By default, this app
 * inserts a million keys, and reads/updates them indefinitely.
 */
public class CassandraRangeKeyValue extends CassandraKeyValueBase {
  // The default table name to create and use for CRUD ops.
  private static final String DEFAULT_TABLE_NAME = CassandraRangeKeyValue.class.getSimpleName();

  @Override
  public List<String> getCreateTableStatements() {
    String create_stmt = String.format(
        "CREATE TABLE IF NOT EXISTS %s (k varchar, r1 varchar, r2 varchar, r3 varchar, v blob, " +
            "primary key ((k), r1, r2, r3))", getTableName());

    if (appConfig.tableTTLSeconds > 0) {
      create_stmt += " WITH default_time_to_live = " + appConfig.tableTTLSeconds;
    }
    create_stmt += ";";
    return Arrays.asList(create_stmt);
  }

  @Override
  protected String getDefaultTableName() {
    return DEFAULT_TABLE_NAME;
  }

  @Override
  protected BoundStatement bindSelect(String key) {
    PreparedStatement prepared_stmt = getPreparedSelect(String.format(
        "SELECT k, r1, r2, r3, v FROM %s WHERE k = ? AND r1 = ? AND r2 = ? AND r3 = ?;",
        getTableName()), appConfig.localReads);
    return prepared_stmt.bind(key, key, key, key);
  }

  @Override
  protected BoundStatement bindInsert(String key, ByteBuffer value)  {
    PreparedStatement prepared_stmt = getPreparedInsert(String.format(
        "INSERT INTO %s (k, r1, r2, r3, v) VALUES (?, ?, ?, ?, ?);",
        getTableName()));
    return prepared_stmt.bind(key, key, key, key, value);
  }

  @Override
  public List<String> getWorkloadDescription() {
    return Arrays.asList(
        "Sample key-value app built on Cassandra. The app writes out unique keys",
        "each has one hash and three range string parts.",
        "There are multiple readers and writers that update these",
        "keys and read them indefinitely. Note that the number of reads and writes to",
        "perform can be specified as a parameter.");
  }
}
