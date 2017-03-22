package org.yb.loadtester.workload;

import java.util.List;
import java.util.Random;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.atomic.AtomicLong;

import org.apache.commons.cli.CommandLine;
import org.apache.log4j.Logger;
import org.yb.loadtester.Workload;
import org.yb.loadtester.common.Configuration;
import org.yb.loadtester.common.TimeseriesLoadGenerator;

import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;

public class CassandraStockTicker extends Workload {
  private static final Logger LOG = Logger.getLogger(CassandraStockTicker.class);
  // Static initialization of this workload's config.
  static {
    // Disable the read-write percentage.
    workloadConfig.readIOPSPercentage = -1;
    // Set the read and write threads to 1 each.
    workloadConfig.numReaderThreads = 1;
    workloadConfig.numWriterThreads = 1;
    // Set the number of keys to read and write.
    workloadConfig.numKeysToRead = -1;
    workloadConfig.numKeysToWrite = -1;
  }
  private static int num_ticker_symbols = 10000;
  // The rate at which each metric is generated in millis.
  private static long data_emit_rate_millis = 1 * 1000;
  // The structure to hold all the stock ticker info.
  static List<TickerInfo> tickers = new CopyOnWriteArrayList<TickerInfo>();
  // The table that has the raw ticker data.
  private String tickerTableRaw = "stock_ticker_raw";
  // The table that has the downsampled minutely data.
  private String tickerTableMin = "stock_ticker_1min";
  // The total number of rows read from the DB so far.
  private static AtomicLong num_rows_read;
  static Random random = new Random();

  @Override
  public void initialize(Configuration configuration) {
    synchronized (tickers) {
      LOG.info("Initializing CassandraStockTicker app...");
      // If the datasources have already been created, we have already initialized the static
      // variables, so nothing to do.
      if (!tickers.isEmpty()) {
        return;
      }

      // Read the various params from the command line.
      CommandLine commandLine = configuration.getCommandLine();
      if (commandLine.hasOption("num_ticker_symbols")) {
        num_ticker_symbols = Integer.parseInt(commandLine.getOptionValue("num_ticker_symbols"));
        LOG.info("num_ticker_symbols: " + num_ticker_symbols);
      }
      if (commandLine.hasOption("data_emit_rate_millis")) {
        data_emit_rate_millis =
            Long.parseLong(commandLine.getOptionValue("data_emit_rate_millis"));
        LOG.info("data_emit_rate_millis: " + data_emit_rate_millis);
      }

      int ticker_symbol_idx = 0;
      while (ticker_symbol_idx < num_ticker_symbols) {
        // Create the data source.
        TickerInfo dataSource = new TickerInfo(ticker_symbol_idx, data_emit_rate_millis);
        tickers.add(dataSource);
        ticker_symbol_idx++;
        if (ticker_symbol_idx % 1000 == 0) {
          System.out.print(".");
        }
      }
      System.out.println("");
      // Initialize the number of rows read.
      num_rows_read = new AtomicLong(0);
      LOG.info("CassandraStockTicker app is ready.");
    }
  }

  @Override
  public String getExampleUsageOptions(String optsPrefix, String optsSuffix) {
    return optsPrefix + "--num_threads_read 32" + optsSuffix +
           optsPrefix + "--num_threads_write 4" + optsSuffix +
           optsPrefix + "--num_ticker_symbols 100000" + optsSuffix +
           optsPrefix + "--data_emit_rate_millis 1000" + optsSuffix +
           optsPrefix + "--table_ttl_seconds " + 24 * 60 * 60;
  }

  @Override
  public void dropTable() {
    // Drop the raw table.
    try {
      String drop_stmt = String.format("DROP TABLE %s;", tickerTableRaw);
      getCassandraClient().execute(drop_stmt);
      LOG.info("Dropped Cassandra table " + tickerTableRaw + " using query: [" + drop_stmt + "]");
    } catch (Exception e) {
      LOG.info("Ignoring exception dropping table: " + e.getMessage());
    }
    // Drop the minutely table.
    try {
      String drop_stmt = String.format("DROP TABLE %s;", tickerTableMin);
      getCassandraClient().execute(drop_stmt);
      LOG.info("Dropped Cassandra table " + tickerTableMin + " using query: [" + drop_stmt + "]");
    } catch (Exception e) {
      LOG.info("Ignoring exception dropping table: " + e.getMessage());
    }
  }

  @Override
  public void createTableIfNeeded() {
    // Create the raw table.
    String create_stmt = "CREATE TABLE " + tickerTableRaw + " (" +
                         "  ticker_id varchar" +
                         ", ts timestamp" +
                         ", value varchar" +
                         ", primary key ((ticker_id), ts))" +
                         " WITH CLUSTERING ORDER BY (ts DESC)";
    if (workloadConfig.tableTTLSeconds > 0) {
      create_stmt += " AND default_time_to_live = " + workloadConfig.tableTTLSeconds;
    }
    create_stmt += ";";
    try {
      getCassandraClient().execute(create_stmt);
    } catch (Exception e) {
      LOG.info("Ignoring exception when creating table: " + e.getMessage());
    }
    LOG.info("Created a Cassandra table " + tickerTableRaw + " using query: [" + create_stmt + "]");

    // Create the minutely table.
    create_stmt = "CREATE TABLE " + tickerTableMin + " (" +
                  "  ticker_id varchar" +
                  ", ts timestamp" +
                  ", value varchar" +
                  ", primary key ((ticker_id), ts))" +
                  " WITH CLUSTERING ORDER BY (ts DESC)";
    if (workloadConfig.tableTTLSeconds > 0) {
    create_stmt += " AND default_time_to_live = " + (10 * workloadConfig.tableTTLSeconds);
    }
    create_stmt += ";";
    try {
      getCassandraClient().execute(create_stmt);
    } catch (Exception e) {
      LOG.info("Ignoring exception when creating table: " + e.getMessage());
    }
    LOG.info("Created a Cassandra table " + tickerTableMin +
             " using query: [" + create_stmt + "]");
  }

  @Override
  public long doRead() {
    // Pick a ransom data source.
    TickerInfo dataSource = tickers.get(random.nextInt(tickers.size()));
    // Make sure it has emitted data, otherwise there is nothing to read.
    if (!dataSource.getHasEmittedData()) {
      return 0;
    }
    long startTs = dataSource.getStartTs();
    long endTs = dataSource.getEndTs();
    String select_stmt =
        String.format("SELECT * from %s WHERE ticker_id='%s' LIMIT 1",
                      tickerTableRaw, dataSource.getTickerId());
    ResultSet rs = getCassandraClient().execute(select_stmt);
    List<Row> rows = rs.all();
    num_rows_read.addAndGet(rows.size());
    return 1;
  }

  @Override
  public long doWrite() {
    // Pick a random data source.
    TickerInfo dataSource = tickers.get(random.nextInt(tickers.size()));
    // Enter as many data points as are needed.
    long ts = dataSource.getDataEmitTs();
    long numKeysWritten = 0;
    String value = String.format("value-%s", ts);
    // If we have nothing to write, we're done.
    if (ts == -1) {
      try {
        Thread.sleep(100 /* millisecs */);
      } catch (Exception e) {}
      return 0; /* numKeysWritten */
    }
    // Insert the row.
    String insert_stmt =
        String.format("INSERT INTO %s (ticker_id, ts, value) VALUES ('%s', %s, '%s');",
                      tickerTableRaw, dataSource.getTickerId(), ts, value);
    ResultSet resultSet = getCassandraClient().execute(insert_stmt);
    numKeysWritten++;
    LOG.debug("Executed query: " + insert_stmt + ", result: " + resultSet.toString());
    dataSource.setLastEmittedTs(ts);

    // With some probability, insert into the minutely table.
    if (random.nextInt(60000) < data_emit_rate_millis) {
      insert_stmt =
          String.format("INSERT INTO %s (ticker_id, ts, value) VALUES ('%s', %s, '%s');",
                        tickerTableMin, dataSource.getTickerId(), ts, value);
      resultSet = getCassandraClient().execute(insert_stmt);
      numKeysWritten++;
      LOG.debug("Executed query: " + insert_stmt + ", result: " + resultSet.toString());
    }

    return numKeysWritten;
  }

  @Override
  public void appendMessage(StringBuilder sb) {
    super.appendMessage(sb);
    sb.append("Rows read: " + num_rows_read.get());
    sb.append(" | ");
  }

  /**
   * This class represents a single data source, which sends back timeseries data for a bunch of
   * metrics. Each data source generates data for all metrics at the same time interval, which is
   * governed by emit rate.
   */
  public static class TickerInfo extends TimeseriesLoadGenerator {
    // The ticker id this data source represents.
    String ticker_id;
    // The data emit rate.
    long dataEmitRateMs;

    public TickerInfo(int ticker_idx, long dataEmitRateMs) {
      super(ticker_idx, dataEmitRateMs, workloadConfig.tableTTLSeconds * 1000L);
      this.dataEmitRateMs = dataEmitRateMs;
      this.ticker_id = super.getId();
    }

    public String getTickerId() {
      return ticker_id;
    }

    public long getEndTs() {
      return getLastEmittedTs() + 1;
    }

    public long getStartTs() {
      // Return an interval that reads 30-120 data points.
      long deltaT = 30L * (1 + random.nextInt(90)) * dataEmitRateMs;
      return getEndTs() - deltaT;
    }

    @Override
    public String toString() {
      return getId();
    }
  }
}
