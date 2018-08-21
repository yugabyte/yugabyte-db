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
import java.util.HashSet;
import java.util.List;

import org.apache.log4j.Logger;

import com.datastax.driver.core.BatchStatement;
import com.datastax.driver.core.BoundStatement;
import com.datastax.driver.core.PreparedStatement;
import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;
import com.yugabyte.sample.common.SimpleLoadGenerator.Key;

/**
 * This workload writes and reads some random string keys from a YCQL table with a secondary index
 * on a non-primary-key column. When it reads a key, it queries the key by its associated value
 * which is indexed.
 */
public class CassandraSecondaryIndex extends CassandraKeyValue {
  private static final Logger LOG = Logger.getLogger(CassandraSecondaryIndex.class);

  // Static initialization of this workload's config. These are good defaults for getting a decent
  // read dominated workload on a reasonably powered machine. Exact IOPS will of course vary
  // depending on the machine and what resources it has to spare.
  static {
    // Disable the read-write percentage.
    appConfig.readIOPSPercentage = -1;
    // Set the read and write threads to 1 each.
    appConfig.numReaderThreads = 24;
    appConfig.numWriterThreads = 2;
    // The number of keys to read.
    appConfig.numKeysToRead = -1;
    // The number of keys to write. This is the combined total number of inserts and updates.
    appConfig.numKeysToWrite = -1;
    // The number of unique keys to write. This determines the number of inserts (as opposed to
    // updates).
    appConfig.numUniqueKeysToWrite = NUM_UNIQUE_KEYS;
  }

  // The default table name to create and use for CRUD ops.
  private static final String DEFAULT_TABLE_NAME = CassandraSecondaryIndex.class.getSimpleName();

  public CassandraSecondaryIndex() {
  }

  @Override
  public List<String> getCreateTableStatements() {
    return Arrays.asList(
        String.format(
            "CREATE TABLE IF NOT EXISTS %s (k varchar, v varchar, primary key (k)) %s;",
            getTableName(),
            appConfig.nonTransactionalIndex ? "" :  "WITH transactions = { 'enabled' : true }"),
        String.format(
            "CREATE INDEX IF NOT EXISTS %sByValue ON %s (v) %s;", getTableName(), getTableName(),
            appConfig.nonTransactionalIndex ?
            "WITH transactions = { 'enabled' : false, 'consistency_level' : 'user_enforced' }" :
            ""));
  }

  public String getTableName() {
    return appConfig.tableName != null ? appConfig.tableName : DEFAULT_TABLE_NAME;
  }

  private PreparedStatement getPreparedSelect()  {
    return getPreparedSelect(String.format("SELECT k, v FROM %s WHERE v = ?;", getTableName()),
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
    BoundStatement select = getPreparedSelect().bind(key.getValueStr());
    ResultSet rs = getCassandraClient().execute(select);
    List<Row> rows = rs.all();
    if (rows.size() != 1) {
      LOG.fatal("Read key: " + key.asString() + " expected 1 row in result, got " + rows.size());
    }
    if (!key.asString().equals(rows.get(0).getString(0))) {
      LOG.fatal("Read key: " + key.asString() + ", got " + rows.get(0).getString(0));
    }
    LOG.debug("Read key: " + key.toString());
    return 1;
  }

  protected PreparedStatement getPreparedInsert()  {
    return getPreparedInsert(String.format("INSERT INTO %s (k, v) VALUES (?, ?);",
                             getTableName()));
  }

  @Override
  public long doWrite(int threadIdx) {
    HashSet<Key> keys = new HashSet<Key>();

    try {
      if (appConfig.batchWrite) {
        BatchStatement batch = new BatchStatement();
        PreparedStatement insert = getPreparedInsert();
        for (int i = 0; i < appConfig.cassandraBatchSize; i++) {
          Key key = getSimpleLoadGenerator().getKeyToWrite();
          keys.add(key);
          batch.add(insert.bind(key.asString(), key.getValueStr()));
        }
        // Do the write to Cassandra.
        ResultSet resultSet = getCassandraClient().execute(batch);
        LOG.debug("Wrote keys count: " + keys.size() + ", return code: " + resultSet.toString());
        for (Key key : keys) {
          getSimpleLoadGenerator().recordWriteSuccess(key);
        }
        return keys.size();
      } else {
        Key key = getSimpleLoadGenerator().getKeyToWrite();
        if (key == null) {
          return 0;
        }
        keys.add(key);

        // Do the write to Cassandra.
        BoundStatement insert = getPreparedInsert().bind(key.asString(), key.getValueStr());
        ResultSet resultSet = getCassandraClient().execute(insert);
        LOG.debug("Wrote key: " + key.toString() + ", return code: " + resultSet.toString());
        getSimpleLoadGenerator().recordWriteSuccess(key);
        return 1;
      }
    } catch (Exception e) {
      for (Key key : keys) {
        getSimpleLoadGenerator().recordWriteFailure(key);
      }
      throw e;
    }
  }

  @Override
  public List<String> getWorkloadDescription() {
    return Arrays.asList(
      "Sample key-value app built on Cassandra. The app writes out unique string keys",
      "each with a string value to a YCQL table with an index on the value column.",
      "There are multiple readers and writers that update these keys and read them",
      "indefinitely, with the readers query the keys by the associated values that are",
      "indexed. Note that the number of reads and writes to perform can be specified as",
      "a parameter.");
  }

  @Override
  public List<String> getExampleUsageOptions() {
    return Arrays.asList(
      "--num_unique_keys " + appConfig.numUniqueKeysToWrite,
      "--num_reads " + appConfig.numKeysToRead,
      "--num_writes " + appConfig.numKeysToWrite,
      "--num_threads_read " + appConfig.numReaderThreads,
      "--num_threads_write " + appConfig.numWriterThreads,
      "--batch_write",
      "--batch_size " + appConfig.cassandraBatchSize);
  }
}
