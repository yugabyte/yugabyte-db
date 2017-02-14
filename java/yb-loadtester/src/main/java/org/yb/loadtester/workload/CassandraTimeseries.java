package org.yb.loadtester.workload;

import java.util.ArrayList;
import java.util.List;
import java.util.Random;
import java.util.concurrent.CopyOnWriteArrayList;

import org.apache.log4j.Logger;
import org.yb.loadtester.Workload;

import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;

/**
 * A sample timeseries metric data application.
 *
 * There are NUM_USERS users/controllers in the demo system. Each of these users has some number of
 * nodes (which is between MIN_NODES_PER_USER and MAX_NODES_PER_USER) that emit time series data.
 *
 * The usersTable has a list of users, the number of nodes they each have, etc.
 *
 * The metricsTable has the metric timeseries data for all the users' nodes.
 */
public class CassandraTimeseries extends Workload {
  private static final Logger LOG = Logger.getLogger(CassandraTimeseries.class);
  // The number of keys to write.
  private static final int NUM_KEYS_TO_WRITE = -1;
  // The number of keys to read.
  private static final int NUM_KEYS_TO_READ = -1;
  // Static initialization of this workload's config.
  static {
    // Disable the read-write percentage.
    workloadConfig.readIOPSPercentage = -1;
    // Set the read and write threads to 1 each.
    workloadConfig.numReaderThreads = 1;
    workloadConfig.numWriterThreads = 1;
    // Set the number of keys to read and write.
    workloadConfig.numKeysToRead = NUM_KEYS_TO_READ;
    workloadConfig.numKeysToWrite = NUM_KEYS_TO_WRITE;
  }

  // The number of users.
  private static final int NUM_USERS = 2;
  // The min nodes per user.
  private static final int MIN_NODES_PER_USER = 1;
  // The max nodes per user.
  private static final int MAX_NODES_PER_USER = 4;
  // The min metrics per user.
  private static final int MIN_METRICS_PER_USER = 5;
  // The max metrics per user.
  private static final int MAX_METRICS_PER_USER = 10;
  // The rate at which each metric is generated in millis.
  private static final long DATA_EMIT_RATE_MILLIS = 1 * 1000;
  static Random random = new Random();
  // The table names.
  static final long tableSuffix = System.currentTimeMillis();
  // The table that has the raw metric data.
  private String metricsTable = "ts_metrics_raw_" + tableSuffix;
  // The structure to hold all the user info.
  static List<DataSource> dataSources = new CopyOnWriteArrayList<DataSource>();
  static {
    for (int customer_idx = 0; customer_idx < NUM_USERS; customer_idx++) {
      // Generate the number of metrics this user would generate.
      int num_metrics =
          MIN_METRICS_PER_USER + random.nextInt(MAX_METRICS_PER_USER - MIN_METRICS_PER_USER);
      // Create an object per user, per metric, per node.
      for (int metric_idx = 0; metric_idx < num_metrics; metric_idx++) {
        DataSource dataSource = new DataSource(customer_idx, metric_idx);
        dataSources.add(dataSource);
        LOG.info("Created data source: " + dataSource.toString());
      }
    }
  }

  @Override
  public void initialize(String args) {
  }

  @Override
  public void createTableIfNeeded() {
    String create_stmt = "CREATE TABLE " + metricsTable + " (" +
                         "  controller_id varchar" +
                         ", metric_id varchar" +
                         ", node_id varchar" +
                         ", ts timestamp" +
                         ", value varchar" +
                         ", primary key ((controller_id, metric_id), node_id, ts))";
    if (workloadConfig.tableTTLSeconds > 0) {
      create_stmt += " WITH default_time_to_live = " + workloadConfig.tableTTLSeconds;
    }
    create_stmt += ";";
    getCassandraClient().execute(create_stmt);
    LOG.info("Created a Cassandra table " + metricsTable + " using query: [" + create_stmt + "]");
  }

  @Override
  public long doRead() {
    // Pick a ransom data source.
    DataSource dataSource = dataSources.get(random.nextInt(dataSources.size()));
    // Make sure it has emitted data, otherwise there is nothing to read.
    if (!dataSource.getHasEmittedData()) {
      return 0;
    }
    long startTs = dataSource.getStartTs();
    long endTs = dataSource.getEndTs();
    String select_stmt =
        String.format("SELECT * from %s WHERE controller_id='%s'" +
                      " AND metric_id='%s'" +
                      " AND node_id='%s'" +
                      " AND ts>%d AND ts<%d;",
                      metricsTable, dataSource.getControllerId(), dataSource.getMetric(),
                      dataSource.getRandomNode(), startTs, endTs);
    ResultSet rs = getCassandraClient().execute(select_stmt);
    List<Row> rows = rs.all();
    if (rows.size() != dataSource.getExpectedNumDataPoints(startTs, endTs)) {
      LOG.warn("Read " + rows.size() + " data points, expected rows: " +
               dataSource.getExpectedNumDataPoints(startTs, endTs) +
               ", query [" + select_stmt + "]");
    }
    LOG.debug("Read " + rows.size() + " data points, expected rows: " +
             dataSource.getExpectedNumDataPoints(startTs, endTs) +
             ", query [" + select_stmt + "]");
    return 1;
  }

  @Override
  public long doWrite() {
    // Pick a ransom data source.
    DataSource dataSource = dataSources.get(random.nextInt(dataSources.size()));
    // Enter as many data points as are needed.
    long ts = dataSource.getDataEmitTs();
    long numKeysWritten = 0;
    while (ts > -1) {
      String value = String.format("value-%s", ts);
      for (String node : dataSource.getNodes()) {
        String insert_stmt =
            String.format("INSERT INTO %s (controller_id, metric_id, node_id, ts, value) VALUES " +
                          "('%s', '%s', '%s', %s, '%s');",
                          metricsTable, dataSource.getControllerId(), dataSource.getMetric(),
                          node, ts, value);
        ResultSet resultSet = getCassandraClient().execute(insert_stmt);
        numKeysWritten++;
        LOG.debug("Executed query: " + insert_stmt);
      }
      dataSource.setLastEmittedTs(ts);
      ts = dataSource.getDataEmitTs();
    }
    return numKeysWritten;
  }

  /**
   * This class represents a single customer, single metric combination that sends back timeseries
   * data for all the nodes owned by the user. The assumption is that all nodes for the user
   * generate data at a similar time interval.
   */
  public static class DataSource {
    Random random = new Random();
    // Customer or controller id.
    String customer_id;
    // The metric name.
    String metric_id;
    // The list of nodes for this customer, metric.
    List<String> nodes;
    // The timestamp at which the data emit started.
    long dataEmitStartTs = -1;
    // State variable tracking the last timestamp emitted by this source (assumed to be the same
    // across all the nodes). A -1 indicated no data point has been emitted.
    long lastEmittedTs = -1;
    // The time interval for generating data points. One data point is generated every
    // dataEmitRateMs milliseconds. Defaults to 1 min per data point.
    long dataEmitRateMs = DATA_EMIT_RATE_MILLIS;

    public DataSource(int customer_idx, int metric_idx) {
      this.customer_id = String.format("customer-%04d", customer_idx);
      metric_id = String.format("data.platform.metric.%04d", metric_idx);
      // Generate the number of nodes this user would have.
      int num_nodes = MIN_NODES_PER_USER + random.nextInt(MAX_NODES_PER_USER - MIN_NODES_PER_USER);
      nodes = new ArrayList<String>();
      for (int idx = 0; idx < num_nodes; idx++) {
        nodes.add(String.format("node-%04d.yugabyte.com", idx));
      }
    }

    public String getControllerId() {
      return customer_id;
    }

    public String getMetric() {
      return metric_id;
    }

    public List<String> getNodes() {
      return nodes;
    }

    public String getRandomNode() {
      return nodes.get(random.nextInt(nodes.size()));
    }

    /**
     * Returns the epoch time when the next data point should be emitted. Returns -1 if no data
     * point needs to be emitted in order to satisfy dataEmitRateMs.
     */
    public long getDataEmitTs() {
      long ts = System.currentTimeMillis();
      // Check if too little time has elapsed since the last data point was emitted.
      if (ts - lastEmittedTs < dataEmitRateMs) {
        return -1;
      }
      // Return the data point at the time boundary needed.
      if (lastEmittedTs == -1) {
        return ts - (ts % dataEmitRateMs);
      }
      return lastEmittedTs + dataEmitRateMs;
    }

    public boolean getHasEmittedData() {
      return (lastEmittedTs > -1);
    }

    public synchronized void setLastEmittedTs(long ts) {
      // Set the time when we started emitting data.
      if (dataEmitStartTs == -1) {
        dataEmitStartTs = ts;
      }
      if (lastEmittedTs < ts) {
        lastEmittedTs = ts;
      }
    }

    public synchronized long getLastEmittedTs() {
      return lastEmittedTs;
    }

    public long getEndTs() {
      return getLastEmittedTs() + 1;
    }

    public long getStartTs() {
      // Return 1/2/3 hour interval.
      long deltaT = 1L * (1 + random.nextInt(3)) * 60 * 60 * 1000;
      return getEndTs() - deltaT;
    }

    public int getExpectedNumDataPoints(long startTime, long endTime) {
      long effectiveStartTime = (startTime < dataEmitStartTs)?dataEmitStartTs:startTime;
      // TODO: enable this when the TTL in cassandra is interpreted in secs instead of millis.
//      if (endTime - effectiveStartTime > workloadConfig.tableTTLSeconds * 1000L) {
//        effectiveStartTime = endTime - workloadConfig.tableTTLSeconds * 1000L;
//      }
      int expectedRows = (int)((endTime - effectiveStartTime) / dataEmitRateMs) + 1;
      return expectedRows;
    }

    @Override
    public String toString() {
      return getControllerId() + ":" + getMetric() + "[" + getNodes().size() + " nodes]";
    }
  }
}
