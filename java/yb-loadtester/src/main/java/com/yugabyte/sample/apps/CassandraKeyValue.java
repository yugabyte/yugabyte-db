// Copyright (c) YugaByte, Inc.

package com.yugabyte.sample.apps;

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
public class CassandraKeyValue extends AppBase {
  private static final Logger LOG = Logger.getLogger(CassandraKeyValue.class);

  private static final int NUM_UNIQUE_KEYS = 1000000;

  // Static initialization of this workload's config. These are good defaults for getting a decent
  // read dominated workload on a reasonably powered machine. Exact IOPS will of course vary
  // depending on the machine and what resources it has to spare.
  static {
    // Disable the read-write percentage.
    workloadConfig.readIOPSPercentage = -1;
    // Set the read and write threads to 1 each.
    workloadConfig.numReaderThreads = 24;
    workloadConfig.numWriterThreads = 2;
    // The number of keys to read.
    workloadConfig.numKeysToRead = -1;
    // The number of keys to write. This is the combined total number of inserts and updates.
    workloadConfig.numKeysToWrite = -1;
    // The number of unique keys to write. This determines the number of inserts (as opposed to
    // updates).
    workloadConfig.numUniqueKeysToWrite = 1000000;
  }

  // The table name.
  private String tableName = CassandraKeyValue.class.getSimpleName();

  // The prepared select statement for fetching the data.
  PreparedStatement preparedSelect;

  // The prepared statement for inserting into the table.
  PreparedStatement preparedInsert;

  // Lock for initializing prepared statement objects.
  Object prepareInitLock = new Object();

  /**
   * Drop the table created by this app.
   */
  @Override
  public void dropTable() {
    try {
      String drop_stmt = String.format("DROP TABLE %s;", tableName);
      getCassandraClient().execute(drop_stmt);
      LOG.info("Dropped Cassandra table " + tableName + " using query: [" + drop_stmt + "]");
    } catch (Exception e) {
      LOG.info("Ignoring exception dropping table: " + e.getMessage());
    }
  }

  @Override
  public void createTableIfNeeded() {
    try {
      String create_stmt =
          String.format("CREATE TABLE %s (k varchar, v varchar, primary key (k))",
                        tableName);
      if (workloadConfig.tableTTLSeconds > 0) {
        create_stmt += " WITH default_time_to_live = " + workloadConfig.tableTTLSeconds;
      }
      create_stmt += ";";
      getCassandraClient().execute(create_stmt);
      LOG.info("Created a Cassandra table " + tableName + " using query: [" + create_stmt + "]");
    } catch (Exception e) {
      LOG.info("Ignoring exception creating table: " + e.getMessage());
    }
  }

  private PreparedStatement getPreparedSelect()  {
    if (preparedSelect == null) {
      synchronized (prepareInitLock) {
        if (preparedSelect == null) {
          // Create the prepared statement object.
          String select_stmt = String.format("SELECT k, v FROM %s WHERE k = ?;", tableName);
          preparedSelect = getCassandraClient().prepare(select_stmt);
        }
      }
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
    // Do the read from Cassandra.
    // Bind the select statement.
    BoundStatement select = getPreparedSelect().bind(key.asString());
    ResultSet rs = getCassandraClient().execute(select);
    List<Row> rows = rs.all();
    if (rows.size() != 1) {
      // If TTL is enabled, turn off correctness validation.
      if (workloadConfig.tableTTLSeconds <= 0) {
        LOG.fatal("Read key: " + key.asString() + " expected 1 row in result, got " + rows.size());
      }
      return 1;
    }
    String value = rows.get(0).getString(1);
    key.verify(value);
    LOG.debug("Read key: " + key.toString());
    return 1;
  }

  private PreparedStatement getPreparedInsert()  {
    if (preparedInsert == null) {
      synchronized (prepareInitLock) {
        if (preparedInsert == null) {
          // Create the prepared statement object.
          String insert_stmt =
              String.format("INSERT INTO %s (k, v) VALUES (?, ?);", tableName);
          preparedInsert = getCassandraClient().prepare(insert_stmt);
        }
      }
    }
    return preparedInsert;
  }

  @Override
  public long doWrite() {
    Key key = getSimpleLoadGenerator().getKeyToWrite();
    // Do the write to Cassandra.
    BoundStatement insert = getPreparedInsert().bind(key.asString(), key.getValueStr());
    ResultSet resultSet = getCassandraClient().execute(insert);
    LOG.debug("Wrote key: " + key.toString() + ", return code: " + resultSet.toString());
    getSimpleLoadGenerator().recordWriteSuccess(key);
    return 1;
  }

  @Override
  public String getWorkloadDescription(String optsPrefix, String optsSuffix) {
    StringBuilder sb = new StringBuilder();
    sb.append(optsPrefix);
    sb.append("Sample key-value app built on Cassandra. The app writes out 1M unique string keys");
    sb.append(optsSuffix);
    sb.append(optsPrefix);
    sb.append("each with a string value. There are multiple readers and writers that update these");
    sb.append(optsSuffix);
    sb.append(optsPrefix);
    sb.append("keys and read them indefinitely. Note that the number of reads and writes to");
    sb.append(optsSuffix);
    sb.append(optsPrefix);
    sb.append("perform can be specified as a parameter.");
    sb.append(optsSuffix);
    return sb.toString();
  }

  @Override
  public String getExampleUsageOptions(String optsPrefix, String optsSuffix) {
    StringBuilder sb = new StringBuilder();
    sb.append(optsPrefix);
    sb.append("--num_unique_keys " + workloadConfig.numUniqueKeysToWrite);
    sb.append(optsSuffix);
    sb.append(optsPrefix);
    sb.append("--num_reads " + workloadConfig.numKeysToRead);
    sb.append(optsSuffix);
    sb.append(optsPrefix);
    sb.append("--num_writes " + workloadConfig.numKeysToWrite);
    sb.append(optsSuffix);
    sb.append(optsPrefix);
    sb.append("--num_threads_read " + workloadConfig.numReaderThreads);
    sb.append(optsSuffix);
    sb.append(optsPrefix);
    sb.append("--num_threads_write " + workloadConfig.numWriterThreads);
    sb.append(optsSuffix);
    sb.append(optsPrefix);
    sb.append("--table_ttl_seconds " + workloadConfig.tableTTLSeconds);
    sb.append(optsSuffix);
    return sb.toString();
  }
}
