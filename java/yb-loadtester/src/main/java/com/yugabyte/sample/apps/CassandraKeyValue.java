// Copyright (c) YugaByte, Inc.

package com.yugabyte.sample.apps;

import java.util.Arrays;
import java.util.List;

import com.yugabyte.sample.common.CmdLineOpts;
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
public class CassandraKeyValue extends AppBase {
  private static final Logger LOG = Logger.getLogger(CassandraKeyValue.class);

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
    dropCassandraTable(tableName);
  }

  @Override
  public List<String> getCreateTableStatements() {
    String create_stmt = String.format(
      "CREATE TABLE IF NOT EXISTS %s (k varchar, v varchar, primary key (k))",
      tableName);
    if (appConfig.tableTTLSeconds > 0) {
      create_stmt += " WITH default_time_to_live = " + appConfig.tableTTLSeconds;
    }
    create_stmt += ";";
    return Arrays.asList(create_stmt);
  }

  private PreparedStatement getPreparedSelect()  {
    if (preparedSelect == null) {
      synchronized (prepareInitLock) {
        if (preparedSelect == null) {
          // Create the prepared statement object.
          String select_stmt = String.format("SELECT k, v FROM %s WHERE k = ?;", tableName);
          preparedSelect = getCassandraClient().prepare(select_stmt);
          if (appConfig.localReads) {
            preparedSelect.setConsistencyLevel(ConsistencyLevel.ONE);
          }
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
      if (appConfig.tableTTLSeconds <= 0) {
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
    try {
      // Do the write to Cassandra.
      BoundStatement insert = getPreparedInsert().bind(key.asString(), key.getValueStr());
      ResultSet resultSet = getCassandraClient().execute(insert);
      LOG.debug("Wrote key: " + key.toString() + ", return code: " + resultSet.toString());
      getSimpleLoadGenerator().recordWriteSuccess(key);
      return 1;
    } catch (Exception e) {
      getSimpleLoadGenerator().recordWriteFailure(key);
      throw e;
    }
  }

  @Override
  public void appendMessage(StringBuilder sb) {
    super.appendMessage(sb);
    sb.append("maxWrittenKey: " + getSimpleLoadGenerator().getMaxWrittenKey() +  " | ");
    sb.append("maxGeneratedKey: " + getSimpleLoadGenerator().getMaxGeneratedKey() +  " | ");
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
    sb.append("--num_unique_keys " + appConfig.numUniqueKeysToWrite);
    sb.append(optsSuffix);
    sb.append(optsPrefix);
    sb.append("--num_reads " + appConfig.numKeysToRead);
    sb.append(optsSuffix);
    sb.append(optsPrefix);
    sb.append("--num_writes " + appConfig.numKeysToWrite);
    sb.append(optsSuffix);
    sb.append(optsPrefix);
    sb.append("--value_size " + appConfig.valueSize);
    sb.append(optsSuffix);
    sb.append(optsPrefix);
    sb.append("--num_threads_read " + appConfig.numReaderThreads);
    sb.append(optsSuffix);
    sb.append(optsPrefix);
    sb.append("--num_threads_write " + appConfig.numWriterThreads);
    sb.append(optsSuffix);
    sb.append(optsPrefix);
    sb.append("--table_ttl_seconds " + appConfig.tableTTLSeconds);
    sb.append(optsSuffix);
    return sb.toString();
  }
}
