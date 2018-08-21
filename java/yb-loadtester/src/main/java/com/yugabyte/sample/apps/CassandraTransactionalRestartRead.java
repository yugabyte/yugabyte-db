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
import java.util.List;
import java.util.concurrent.atomic.AtomicLongArray;

import com.datastax.driver.core.*;
import com.datastax.driver.core.policies.RoundRobinPolicy;
import com.yugabyte.sample.common.SimpleLoadGenerator;
import org.apache.log4j.Logger;

import com.yugabyte.sample.common.SimpleLoadGenerator.Key;

/**
 * This workload writes one key per thread, each time incrementing it's value and storing it
 * in array.
 * During read with pick random key and check that it's value at least is as big as was stored
 * before read started.
 */
public class CassandraTransactionalRestartRead extends CassandraKeyValue {
  private static final Logger LOG = Logger.getLogger(CassandraTransactionalRestartRead.class);
  // The default table name to create and use for CRUD ops.
  private static final String DEFAULT_TABLE_NAME =
      CassandraTransactionalRestartRead.class.getSimpleName();

  // Static initialization of this workload's config. These are good defaults for getting a decent
  // read dominated workload on a reasonably powered machine. Exact IOPS will of course vary
  // depending on the machine and what resources it has to spare.
  static {
    // Disable the read-write percentage.
    appConfig.readIOPSPercentage = -1;
    // Set the read and write threads to 1 each.
    appConfig.numReaderThreads = 6;
    appConfig.numWriterThreads = 12;
    // The number of keys to read.
    appConfig.numKeysToRead = -1;
    // The number of keys to write. This is the combined total number of inserts and updates.
    appConfig.numKeysToWrite = -1;
    // The number of unique keys to write. This determines the number of inserts (as opposed to
    // updates).
    appConfig.numUniqueKeysToWrite = 10;
  }

  private AtomicLongArray lastValues = new AtomicLongArray((int)appConfig.numWriterThreads);

  public CassandraTransactionalRestartRead() {
  }

  @Override
  public List<String> getCreateTableStatements() {
    String createStmt = String.format(
        "CREATE TABLE IF NOT EXISTS %s (k varchar, v bigint, primary key (k)) " +
            "WITH transactions = { 'enabled' : true };", getTableName());
    return Arrays.asList(createStmt);
  }

  public String getTableName() {
    return appConfig.tableName != null ? appConfig.tableName : DEFAULT_TABLE_NAME;
  }

  private PreparedStatement getPreparedSelect()  {
    return getPreparedSelect(String.format(
        "SELECT k, v FROM %s WHERE k = :k;",
        getTableName()),
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
    long minExpected = lastValues.get((int) key.asNumber());
    ResultSet rs = getCassandraClient().execute(select);
    List<Row> rows = rs.all();
    if (rows.size() != 1) {
      LOG.fatal("Read key: " + key.asString() + " expected 1 row in result, got " +
          rows.size());
      return 1;
    }
    long readValue = rows.get(0).getLong(1);
    if (readValue < minExpected) {
      LOG.fatal("Value too small for key: " + key.toString() + ": " + readValue + " < " +
          minExpected);
    }

    LOG.debug("Read key: " + key.toString());
    return 1;
  }

  protected PreparedStatement getPreparedInsert()  {
    return getPreparedInsert(String.format(
        "BEGIN TRANSACTION" +
            "  INSERT INTO %s (k, v) VALUES (:k, :v);" +
            "END TRANSACTION;",
        getTableName()));
  }

  @Override
  public long doWrite(int threadIdx) {
    SimpleLoadGenerator generator = getSimpleLoadGenerator();
    Key key = generator.generateKey(threadIdx);
    try {
      // Do the write to Cassandra.
      long keyNum = key.asNumber();
      long newValue = lastValues.get((int) (keyNum)) + 1;
      BoundStatement insert = getPreparedInsert()
          .bind()
          .setString("k", key.asString())
          .setLong("v", newValue);

      ResultSet resultSet = getCassandraClient().execute(insert);
      lastValues.set((int) keyNum, newValue);
      LOG.debug("Wrote key: " + key.toString() + ", return code: " + resultSet.toString());
      getSimpleLoadGenerator().recordWriteSuccess(key);
      return 1;
    } catch (Exception e) {
      getSimpleLoadGenerator().recordWriteFailure(key);
      throw e;
    }
  }

  @Override
  public List<String> getWorkloadDescription() {
    return Arrays.asList(
        "This workload writes one key per thread, each time incrementing it's value and " +
            "storing it in array.",
        "During read with pick random key and check that it's value at least is as big " +
            "as was stored before read started.");
  }

  @Override
  public List<String> getExampleUsageOptions() {
    return Arrays.asList(
        "--num_unique_keys " + appConfig.numUniqueKeysToWrite,
        "--num_reads " + appConfig.numKeysToRead,
        "--num_writes " + appConfig.numKeysToWrite,
        "--num_threads_read " + appConfig.numReaderThreads,
        "--num_threads_write " + appConfig.numWriterThreads);
  }

  protected void setupLoadBalancingPolicy(Cluster.Builder builder) {
    builder.withLoadBalancingPolicy(new RoundRobinPolicy());
  }
}
