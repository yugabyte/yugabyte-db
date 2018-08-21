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

import java.util.Arrays;
import java.util.Date;
import java.util.List;

import org.apache.log4j.Logger;

import com.datastax.driver.core.BoundStatement;
import com.datastax.driver.core.PreparedStatement;
import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;
import com.yugabyte.sample.common.SimpleLoadGenerator.Key;

/**
 * This workload writes and reads some random string keys from a CQL server. By default, this app
 * inserts a million keys, and reads/updates them indefinitely.
 */
public class CassandraUserId extends CassandraKeyValue {
  private static final Logger LOG = Logger.getLogger(CassandraUserId.class);

  // The default table name.
  private final String DEFAULT_TABLE_NAME = CassandraUserId.class.getSimpleName();

  // The newest timestamp we have read.
  static Date maxTimestamp = new Date(0);

  // Lock for updating maxTimestamp.
  private Object updateMaxTimestampLock = new Object();

  public String getTableName() {
    return appConfig.tableName != null ? appConfig.tableName : DEFAULT_TABLE_NAME;
  }

  /**
   * Drop the table created by this app.
   */
  @Override
  public void dropTable() {
    dropCassandraTable(getTableName());
  }

  @Override
  public List<String> getCreateTableStatements() {
    String create_stmt = String.format("CREATE TABLE IF NOT EXISTS %s " +
        "(user_name varchar, password varchar, update_time timestamp, primary key (user_name));",
      getTableName());
    return Arrays.asList(create_stmt);
  }

  private PreparedStatement getPreparedSelect() {
    return getPreparedSelect(String.format(
        "SELECT user_name, password, update_time FROM %s WHERE user_name = ?;", getTableName()),
        appConfig.localReads);
  }

  @Override
  public long doRead() {
    Key key = getSimpleLoadGenerator().getKeyToRead();
    if (key == null) {
      // There are no keys to read yet.
      return 0;
    }
    // Do the read from Cassandra.
    // Bind the select statement.
    BoundStatement select = getPreparedSelect().bind(key.asString());
    ResultSet rs = getCassandraClient().execute(select);
    List<Row> rows = rs.all();
    if (rows.size() != 1) {
      // If TTL is enabled, turn off correctness validation.
      if (appConfig.tableTTLSeconds <= 0) {
        LOG.fatal("Read user_name: " + key.asString() +
            " expected 1 row in result, got " + rows.size());
      }
      return 1;
    }
    String password = rows.get(0).getString("password");
    if (!password.equals((new StringBuilder(key.asString()).reverse().toString()))) {
      LOG.fatal("Invalid password for user_name: " + key.asString() +
          ", expected: " + key.asString() +
          ", got: " + password);
    };
    Date timestamp = rows.get(0).getTimestamp("update_time");
    synchronized (updateMaxTimestampLock) {
      if (timestamp.after(maxTimestamp)) {
        maxTimestamp.setTime(timestamp.getTime());
      }
    }
    LOG.debug("Read user_name: " + key.toString());
    return 1;
  }

  protected PreparedStatement getPreparedInsert()  {
    return getPreparedInsert(String.format(
        "INSERT INTO %s (user_name, password, update_time) VALUES (?, ?, ?);", getTableName()));
  }

  @Override
  public long doWrite(int threadIdx) {
    Key key = getSimpleLoadGenerator().getKeyToWrite();
    try {
      // Do the write to Cassandra.
      BoundStatement insert = getPreparedInsert().bind(
          key.asString(), (new StringBuilder(key.asString()).reverse().toString()),
          new Date(System.currentTimeMillis()));
      ResultSet resultSet = getCassandraClient().execute(insert);
      LOG.debug("Wrote user_name: " + key.toString() + ", return code: " + resultSet.toString());
      getSimpleLoadGenerator().recordWriteSuccess(key);
      return 1;
    } catch (Exception e) {
      getSimpleLoadGenerator().recordWriteFailure(key);
      throw e;
    }
  }

  @Override
  public void appendMessage(StringBuilder sb) {
    super.appendParentMessage(sb);
    sb.append("maxWrittenKey: " + getSimpleLoadGenerator().getMaxWrittenKey() +  " | ");
    sb.append("max timestamp: " + maxTimestamp.toString());
  }

  @Override
  public List<String> getWorkloadDescription() {
    return Arrays.asList(
      "Sample user id app built on Cassandra. The app writes out 1M unique user ids",
      "each with a string password. There are multiple readers and writers that update",
      "these user ids and passwords them indefinitely. Note that the number of reads and",
      "writes to perform can be specified as a parameter.");
  }
}
