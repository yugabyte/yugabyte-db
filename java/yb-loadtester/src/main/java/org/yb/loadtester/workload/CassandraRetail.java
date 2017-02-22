// Copyright (c) YugaByte, Inc.

package org.yb.loadtester.workload;

import java.util.List;
import java.util.Random;

import org.apache.log4j.Logger;
import org.yb.loadtester.Workload;
import org.yb.loadtester.common.SimpleLoadGenerator;
import org.yb.loadtester.common.SimpleLoadGenerator.Key;

import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;

/**
 * Retail demo app.
 *
 * There is a product table which has all the retail items.
 */
public class CassandraRetail extends Workload {
  private static final Logger LOG = Logger.getLogger(CassandraRetail.class);
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

  // Total number of items in the inventory.
  private static final int NUM_ITEMS = 1000000;
  // Number of price lists per item.
  private static final int MIN_NUM_PRICE_LISTS_PER_ITEM = 1;
  // Constant for one hour in millis - to set time expiry.
  private static final long TIME_DELTA = 1 * 60 * 60 * 1000;
  // Instance of the load generator.
  private static SimpleLoadGenerator loadGenerator = new SimpleLoadGenerator(0, NUM_ITEMS);
  // The table name suffix.
  static final long tableSuffix = System.currentTimeMillis();
  // The table that has the raw metric data.
  private String pricingTable = "price_" + tableSuffix;
  static Random random = new Random();

  @Override
  public void createTableIfNeeded() {
    String create_stmt =
        "CREATE TABLE " + pricingTable + " (" +
        "  item_number bigint" +
        ", price_list int" +
        ", effective_from bigint" +
        ", effective_to bigint" +
        ", created_date bigint" +
        ", modified_date bigint" +
        ", item_cost double precision" +
        ", list_price double precision" +
        ", status int" +
        ", primary key ((item_number, price_list), effective_from, effective_to));";
    getCassandraClient().execute(create_stmt);
    LOG.info("Created a Cassandra table " + pricingTable + " using query: [" + create_stmt + "]");
  }

  @Override
  public long doRead() {
    Key key = loadGenerator.getKeyToRead();
    if (key == null) {
      // There are no keys to read yet.
      return 0;
    }
    String select_stmt =
        String.format("SELECT * from %s WHERE item_number=%d" +
                      " AND price_list=%d;",
                      pricingTable, key.asNumber(), 1);
    ResultSet rs = getCassandraClient().execute(select_stmt);
    List<Row> rows = rs.all();
    LOG.debug("Read " + rows.size() + " data points, query [" + select_stmt + "]");
    return 1;
  }

  @Override
  public long doWrite() {
    Key key = loadGenerator.getKeyToWrite();
    int price_list = 1;
    long now = System.currentTimeMillis();
    long effective_from = now - TIME_DELTA;
    long effective_to = now + TIME_DELTA;
    int item_cost = random.nextInt(1000);
    int list_price = random.nextInt(50);
    int status = 1;
    long ttl = TIME_DELTA * 2;
    String insert_stmt =
        String.format("INSERT INTO %s (item_number, price_list, effective_from, effective_to, " +
                                      "created_date, modified_date, item_cost, " +
                                      "list_price, status) VALUES " +
                                      "(%d, %d, %d, %d, %d, %d, %d, %d, %d) USING TTL %d;",
                      pricingTable, key.asNumber(), price_list, effective_from, effective_to,
                      now, now, item_cost, list_price, status, ttl);
    ResultSet resultSet = getCassandraClient().execute(insert_stmt);
    LOG.debug("Executed query: " + insert_stmt);
    loadGenerator.recordWriteSuccess(key);
    return 1;
  }
}
