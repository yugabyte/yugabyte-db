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

import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.util.Arrays;
import java.util.List;

import org.apache.log4j.Logger;

import com.yugabyte.sample.common.SimpleLoadGenerator.Key;

/**
 * This workload writes and reads some random string keys from a postgresql table with a secondary
 * index on a non-primary-key column. When it reads a key, it queries the key by its associated
 * value which is indexed.
 */
public class PostgresqlSecondaryIndex extends AppBase {
  private static final Logger LOG = Logger.getLogger(PostgresqlSecondaryIndex.class);

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
  private static final String DEFAULT_TABLE_NAME = "postgresqlsecondaryindex";

  // The shared prepared select statement for fetching the data.
  private volatile PreparedStatement preparedSelect = null;

  // The shared prepared insert statement for inserting the data.
  private volatile PreparedStatement preparedInsert = null;

  public PostgresqlSecondaryIndex() {
  }

  @Override
  public void createTablesIfNeeded() throws Exception {
    Connection connection = getPostgresConnection();

    // Check if database already exists.
    ResultSet rs = connection.createStatement().executeQuery(String.format("SELECT datname FROM " +
        "pg_database WHERE datname='%s'", postgres_ybdemo_database));
    if (!rs.next()) {
      // Database doesnt't exist, creating...
      connection.createStatement().executeUpdate(
          String.format("CREATE DATABASE %s", postgres_ybdemo_database));
      LOG.info("Created database: " + postgres_ybdemo_database);
    }
    connection.close();

    // Connect to the new database.
    connection = getPostgresConnection(postgres_ybdemo_database);

    // Drop table if possible.
    connection.createStatement().executeUpdate(
        String.format("DROP TABLE IF EXISTS %s;", getTableName()));
    LOG.info(String.format("Dropped table: %s", getTableName()));

    // Create the table.
    connection.createStatement().executeUpdate(
        String.format("CREATE TABLE IF NOT EXISTS %s (k varchar PRIMARY KEY, v varchar);",
            getTableName()));
    LOG.info(String.format("Created table: %s", getTableName()));

    // Create an index on the table.
    connection.createStatement().executeUpdate(
        String.format("CREATE INDEX IF NOT EXISTS %s_index ON %s(v);",
            getTableName(), getTableName()));
    LOG.info(String.format("Created index on table: %s", getTableName()));
  }

  public String getTableName() {
    String tableName = appConfig.tableName != null ? appConfig.tableName : DEFAULT_TABLE_NAME;
    return tableName.toLowerCase();
  }

  private PreparedStatement getPreparedSelect() throws Exception {
    if (preparedSelect == null) {
      preparedSelect = getPostgresConnection(postgres_ybdemo_database).prepareStatement(
          String.format("SELECT k, v FROM %s WHERE v = ?;", getTableName()));
    }
    return preparedSelect;
  }

  @Override
  public long doRead() {
    Key key = getSimpleLoadGenerator().getKeyToRead();
    if (key == null) {
      // There are no keys to read yet.
      return 0;
    }

    try {
      PreparedStatement statement = getPreparedSelect();
      statement.setString(1, key.getValueStr());
      ResultSet rs = statement.executeQuery();
      if (!rs.next()) {
        LOG.fatal("Read key: " + key.getKeyWithHashPrefix() + " expected 1 row in result, got 0");
        return 0;
      }

      if (!key.getKeyWithHashPrefix().equals(rs.getString("k"))) {
        LOG.fatal("Read key: " + key.getKeyWithHashPrefix() + ", got " + rs.getString("k"));
      }
      LOG.debug("Read key: " + key.toString());

      if (rs.next()) {
        LOG.fatal("Read key: " + key.getKeyWithHashPrefix() +
            " expected 1 row in result, got more than one");
        return 0;
      }
    } catch (Exception e) {
      LOG.fatal("Failed reading value: " + key.getValueStr(), e);
      return 0;
    }
    return 1;
  }

  private PreparedStatement getPreparedInsert() throws Exception {
    if (preparedInsert == null) {
      preparedInsert = getPostgresConnection(postgres_ybdemo_database).prepareStatement(
          String.format("INSERT INTO %s (k, v) VALUES (?, ?);", getTableName()));
    }
    return preparedInsert;
  }

  @Override
  public long doWrite(int threadIdx) {
    Key key = getSimpleLoadGenerator().getKeyToWrite();
    if (key == null) {
      return 0;
    }

    int result = 0;
    try {
      PreparedStatement statement = getPreparedInsert();
      // Prefix hashcode to ensure generated keys are random and not sequential.
      statement.setString(1, key.getKeyWithHashPrefix());
      statement.setString(2, key.getValueStr());
      result = statement.executeUpdate();
      LOG.debug("Wrote key: " + key.asString() + ", " + key.getValueStr() + ", return code: " +
          result);
      getSimpleLoadGenerator().recordWriteSuccess(key);
    } catch (Exception e) {
      getSimpleLoadGenerator().recordWriteFailure(key);
      LOG.fatal("Failed writing key: " + key.asString(), e);
    }
    return result;
  }

  @Override
  public List<String> getWorkloadDescription() {
    return Arrays.asList(
        "Sample key-value app built on postgresql. The app writes out unique string keys",
        "each with a string value to a postgres table with an index on the value column.",
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
        "--num_threads_write " + appConfig.numWriterThreads);
  }
}
