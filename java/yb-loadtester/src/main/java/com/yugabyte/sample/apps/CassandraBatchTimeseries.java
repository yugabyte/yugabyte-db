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
import java.util.Random;
import java.util.concurrent.CopyOnWriteArrayList;

import org.apache.commons.cli.CommandLine;
import org.apache.log4j.Logger;

import com.datastax.driver.core.BatchStatement;
import com.datastax.driver.core.BoundStatement;
import com.datastax.driver.core.PreparedStatement;
import com.datastax.driver.core.ResultSet;

import com.yugabyte.sample.common.CmdLineOpts;

/**
 * A sample timeseries metric data application with batch processing.
 *
 * This app tracks a bunch of metrics which have a series of data points ordered by timestamp.
 * The metric's id can be thought of as a compound/concatenated unique id - which could include
 * user id, on a node id and device id (and id of the device). For simplicity, we use it
 * as a single metric id to cover all of them.
 */
public class CassandraBatchTimeseries extends AppBase {
  private static final Logger LOG = Logger.getLogger(CassandraBatchTimeseries.class);
  // Static initialization of this workload's config.
  static {
    // Set the default number of reader and writer threads.
    appConfig.numReaderThreads = 2;
    appConfig.numWriterThreads = 16;
    // Set the number of keys to read and write.
    appConfig.numKeysToRead = -1;
    appConfig.numKeysToWrite = -1;
    // The size of the value.
    appConfig.valueSize = 100;
    // Set the TTL for the raw table.
    appConfig.tableTTLSeconds = 24 * 60 * 60;
    // Default write batch size.
    appConfig.cassandraBatchSize = 500;
    // Default read batch size.
    appConfig.cassandraReadBatchSize = 100;
    // Default time delta from current time to read in a batch.
    appConfig.readBackDeltaTimeFromNow = 180;
  }

  static Random random = new Random();
  // The default table name that has the raw metric data.
  private final String DEFAULT_TABLE_NAME = "batch_ts_metrics_raw";
  // The structure to hold info per metric.
  static List<DataSource> dataSources = new CopyOnWriteArrayList<DataSource>();
  // The minimum number of metrics to simulate.
  private static int min_metrics_count = 5;
  // The maximum number of metrics to simulate.
  private static int max_metrics_count = 10;
  // The shared prepared select statement for fetching the data.
  private static volatile PreparedStatement preparedSelect;
  // The shared prepared statement for inserting into the table.
  private static volatile PreparedStatement preparedInsert;
  // Lock for initializing prepared statement objects.
  private static final Object prepareInitLock = new Object();

  @Override
  public void initialize(CmdLineOpts configuration) {
    synchronized (dataSources) {
      // If the datasources have already been created, we have already initialized the static
      // variables, so nothing to do.
      if (!dataSources.isEmpty()) {
        return;
      }

      // Read the various params from the command line.
      CommandLine commandLine = configuration.getCommandLine();
      if (commandLine.hasOption("min_metrics_count")) {
        min_metrics_count = Integer.parseInt(commandLine.getOptionValue("min_metrics_count"));
      }
      if (commandLine.hasOption("max_metrics_count")) {
        max_metrics_count = Integer.parseInt(commandLine.getOptionValue("max_metrics_count"));
      }

      // Generate the number of metrics this data source would emit.
      int num_metrics = min_metrics_count +
                        (max_metrics_count > min_metrics_count ?
                            random.nextInt(max_metrics_count - min_metrics_count) : 0);

      // Create all the metric data sources.
      for (int i = 0; i < num_metrics; i++) {
        DataSource dataSource = new DataSource(i);
        dataSources.add(dataSource);
      }
    }
  }

  public String getTableName() {
    return appConfig.tableName != null ? appConfig.tableName : DEFAULT_TABLE_NAME;
  }

  @Override
  public void dropTable() {
    dropCassandraTable(getTableName());
  }

  @Override
  protected List<String> getCreateTableStatements() {
    String create_stmt = "CREATE TABLE IF NOT EXISTS " + getTableName() + " (" +
                         " metric_id varchar" +
                         ", ts bigint" +
                         ", value varchar" +
                         ", primary key (metric_id, ts))";
    if (appConfig.tableTTLSeconds > 0) {
      create_stmt += " WITH default_time_to_live = " + appConfig.tableTTLSeconds;
    }
    create_stmt += ";";
    return Arrays.asList(create_stmt);
  }

  private PreparedStatement getPreparedInsert()  {
    if (preparedInsert == null) {
      synchronized (prepareInitLock) {
        if (preparedInsert == null) {
          // Create the prepared statement object.
          String insert_stmt = String.format("INSERT INTO %s (metric_id, ts, value) VALUES " +
                                             "(:metric_id, :ts, :value);", getTableName());
          preparedInsert = getCassandraClient().prepare(insert_stmt);
        }
      }
    }
    return preparedInsert;
  }

  private PreparedStatement getPreparedSelect()  {
    if (preparedSelect == null) {
      synchronized (prepareInitLock) {
        if (preparedSelect == null) {
          // Create the prepared statement object.
          String select_stmt = String.format("SELECT * from %s WHERE metric_id = :metricId AND " +
                                             "ts > :startTs AND ts < :endTs ORDER BY ts DESC " +
                                             "LIMIT :readBatchSize;", getTableName());
          preparedSelect = getCassandraClient().prepare(select_stmt);
        }
      }
    }
    return preparedSelect;
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

  @Override
  public long doRead() {
    // Pick a random data source.
    DataSource dataSource = dataSources.get(random.nextInt(dataSources.size()));
    // Make sure it has emitted data, otherwise there is nothing to read.
    if (!dataSource.getHasEmittedData()) {
      return 0;
    }
    long startTs = dataSource.getStartTs();
    long endTs = dataSource.getEndTs();

    // Bind the select statement.
    BoundStatement select =
        getPreparedSelect().bind().setString("metricId", dataSource.getMetricId())
                                  .setLong("startTs", startTs)
                                  .setLong("endTs", endTs)
                                  .setInt("readBatchSize", appConfig.cassandraReadBatchSize);
    // Make the query.
    ResultSet rs = getCassandraClient().execute(select);
    return 1;
  }

  private String getValue(long ts) {
    // Add the timestamp as the value.
    StringBuilder sb = new StringBuilder();
    sb.append(ts);
    int suffixSize = appConfig.valueSize <= sb.length() ? 0 : (appConfig.valueSize - sb.length());
    // Pad with random bytes if needed to create a string of the desired length.
    if (suffixSize > 0) {
      byte[] randBytesArr = new byte[suffixSize];
      random.nextBytes(randBytesArr);
      sb.append(randBytesArr);
    }
    return sb.toString();
  }

  @Override
  public long doWrite(int threadIdx) {
    // Pick a random data source.
    DataSource dataSource = dataSources.get(random.nextInt(dataSources.size()));
    long numKeysWritten = 0;

    BatchStatement batch = new BatchStatement();
    // Enter a batch of data points.
    long ts = dataSource.getDataEmitTs();
    for (int i = 0; i < appConfig.cassandraBatchSize; i++) {
      batch.add(getPreparedInsert().bind().setString("metric_id", dataSource.getMetricId())
                                          .setLong("ts", ts)
                                          .setString("value", getValue(ts)));
      numKeysWritten++;
      ts++;
    }
    dataSource.setLastEmittedTs(ts);

    getCassandraClient().execute(batch);

    return numKeysWritten;
  }

  /**
   * This class represents a single metric data source, which sends back timeseries data for that
   * metric. It generates data governed by the emit rate.
   */
  public static class DataSource {
    // The list of metrics to emit for this data source.
    private String metric_id;
    // The timestamp at which the data emit started.
    private long dataEmitStartTs = 1;
    // State variable tracking the last time emitted by this source.
    private long lastEmittedTs = -1;

    public DataSource(int index) {
      this.metric_id = String.format("metric-%05d", index);
    }

    public String getMetricId() {
      return metric_id;
    }

    public boolean getHasEmittedData() {
      return lastEmittedTs >= dataEmitStartTs;
    }

    public long getEndTs() {
      return getLastEmittedTs() + 1;
    }

    public long getStartTs() {
      return getEndTs() > appConfig.readBackDeltaTimeFromNow ?
          getEndTs() - appConfig.readBackDeltaTimeFromNow : dataEmitStartTs;
    }

    public long getDataEmitTs() {
      if (lastEmittedTs == -1) {
        lastEmittedTs = dataEmitStartTs;
      }
      return lastEmittedTs;
    }

    public synchronized long getLastEmittedTs() {
      return lastEmittedTs;
    }

    public synchronized void setLastEmittedTs(long ts) {
      if (lastEmittedTs < ts) {
        lastEmittedTs = ts;
      }
    }

    @Override
    public String toString() {
      return getMetricId();
    }
  }

  @Override
  public List<String> getWorkloadDescription() {
    return Arrays.asList(
      "Sample timeseries/IoT app built on batched CQL. The app models few metrics per second. ",
      "The data is written into the 'batch_ts_metrics_raw' table, which retains data for one day.",
      "Note that the number of metrics written is a lot more than the number of metrics read as ",
      "is typical in such workloads, and the payload size for each write is 100 bytes. Every ",
      "read query fetches the a limited batch from recently written values for a randome metric.");
  }

  @Override
  public List<String> getExampleUsageOptions() {
    return Arrays.asList(
      "--num_threads_read " + appConfig.numReaderThreads,
      "--num_threads_write " + appConfig.numWriterThreads,
      "--max_metrics_count " + max_metrics_count,
      "--table_ttl_seconds " + appConfig.tableTTLSeconds,
      "--batch_size " + appConfig.cassandraBatchSize,
      "--read_batch_size " + appConfig.cassandraReadBatchSize);
  }
}
