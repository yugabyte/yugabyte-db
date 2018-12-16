package com.yugabyte.sample.apps;

import com.datastax.driver.core.*;
import com.yugabyte.sample.common.SimpleLoadGenerator;
import org.apache.log4j.Logger;

import java.nio.ByteBuffer;
import java.util.Arrays;
import java.util.List;

/**
 * Base class for all workloads that are based on key value tables.
 */
public abstract class CassandraKeyValueBase extends AppBase {
  private static final Logger LOG = Logger.getLogger(CassandraKeyValueBase.class);

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

  // The shared prepared select statement for fetching the data.
  private static volatile PreparedStatement preparedSelect;

  // The shared prepared statement for inserting into the table.
  private static volatile PreparedStatement preparedInsert;

  // Lock for initializing prepared statement objects.
  private static final Object prepareInitLock = new Object();

  public CassandraKeyValueBase() {
    buffer = new byte[appConfig.valueSize];
  }

  /**
   * Drop the table created by this app.
   */
  @Override
  public void dropTable() {
    dropCassandraTable(getTableName());
  }

  protected PreparedStatement getPreparedSelect(String selectStmt, boolean localReads)  {
    if (preparedSelect == null) {
      synchronized (prepareInitLock) {
        if (preparedSelect == null) {
          // Create the prepared statement object.
          preparedSelect = getCassandraClient().prepare(selectStmt);
          if (localReads) {
            LOG.debug("Doing local reads");
            preparedSelect.setConsistencyLevel(ConsistencyLevel.ONE);
          }
        }
      }
    }
    return preparedSelect;
  }

  protected abstract String getDefaultTableName();

  public String getTableName() {
    return appConfig.tableName != null ? appConfig.tableName : getDefaultTableName();
  }

  @Override
  public synchronized void resetClients() {
    synchronized (prepareInitLock) {
      preparedInsert = null;
      preparedSelect = null;
    }
    super.resetClients();
  }

  @Override
  public synchronized void destroyClients() {
    synchronized (prepareInitLock) {
      preparedInsert = null;
      preparedSelect = null;
    }
    super.destroyClients();
  }

  protected abstract BoundStatement bindSelect(String key);

  @Override
  public long doRead() {
    SimpleLoadGenerator.Key key = getSimpleLoadGenerator().getKeyToRead();
    if (key == null) {
      // There are no keys to read yet.
      return 0;
    }
    // Do the read from Cassandra.
    // Bind the select statement.
    BoundStatement select = bindSelect(key.asString());
    ResultSet rs = getCassandraClient().execute(select);
    List<Row> rows = rs.all();
    if (rows.size() != 1) {
      // If TTL is enabled, turn off correctness validation.
      if (appConfig.tableTTLSeconds <= 0) {
        LOG.fatal("Read key: " + key.asString() + " expected 1 row in result, got " + rows.size());
      }
      return 1;
    }
    if (appConfig.valueSize == 0) {
      ByteBuffer buf = rows.get(0).getBytes(1);
      String value = new String(buf.array());
      key.verify(value);
    } else {
      ByteBuffer value = rows.get(0).getBytes(1);
      byte[] bytes = new byte[value.capacity()];
      value.get(bytes);
      verifyRandomValue(key, bytes);
    }
    LOG.debug("Read key: " + key.toString());
    return 1;
  }

  protected PreparedStatement getPreparedInsert(String insertStmt)  {
    if (preparedInsert == null) {
      synchronized (prepareInitLock) {
        if (preparedInsert == null) {
          // Create the prepared statement object.
          preparedInsert = getCassandraClient().prepare(insertStmt);
        }
      }
    }
    return preparedInsert;
  }

  protected abstract BoundStatement bindInsert(String key, ByteBuffer value);

  @Override
  public long doWrite(int threadIdx) {
    SimpleLoadGenerator.Key key = getSimpleLoadGenerator().getKeyToWrite();
    if (key == null) {
      return 0;
    }

    try {
      // Do the write to Cassandra.
      BoundStatement insert;
      if (appConfig.valueSize == 0) {
        String value = key.getValueStr();
        insert = bindInsert(key.asString(), ByteBuffer.wrap(value.getBytes()));
      } else {
        byte[] value = getRandomValue(key);
        insert = bindInsert(key.asString(), ByteBuffer.wrap(value));
      }
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

  public void appendParentMessage(StringBuilder sb) {
    super.appendMessage(sb);
  }

  @Override
  public List<String> getWorkloadDescription() {
    return Arrays.asList(
        "Sample key-value app built on Cassandra. The app writes out 1M unique string keys",
        "each with a string value. There are multiple readers and writers that update these",
        "keys and read them indefinitely. Note that the number of reads and writes to",
        "perform can be specified as a parameter.");
  }

  @Override
  public List<String> getExampleUsageOptions() {
    return Arrays.asList(
        "--num_unique_keys " + appConfig.numUniqueKeysToWrite,
        "--num_reads " + appConfig.numKeysToRead,
        "--num_writes " + appConfig.numKeysToWrite,
        "--value_size " + appConfig.valueSize,
        "--num_threads_read " + appConfig.numReaderThreads,
        "--num_threads_write " + appConfig.numWriterThreads,
        "--table_ttl_seconds " + appConfig.tableTTLSeconds);
  }
}
