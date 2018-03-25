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

import java.nio.ByteBuffer;
import java.util.Arrays;
import java.util.Calendar;
import java.util.Collections;
import java.util.Date;
import java.util.List;
import java.util.Random;
import java.util.Vector;

import com.yugabyte.sample.common.CmdLineOpts;
import org.apache.log4j.Logger;

import com.datastax.driver.core.BatchStatement;
import com.datastax.driver.core.PreparedStatement;
import com.datastax.driver.core.BoundStatement;
import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;
import com.yugabyte.sample.common.SimpleLoadGenerator.Key;

/**
 * This workload writes and reads some coupons for some random customers repeatedly. The numbers of
 * concurrent writers and readers are configurable.
 */
public class CassandraPersonalization extends AppBase {
  private static final Logger LOG = Logger.getLogger(CassandraPersonalization.class);

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
  private static final String DEFAULT_TABLE_NAME = "coupon_recommendation";

  // The shared prepared select statement for fetching the data.
  private static volatile PreparedStatement preparedSelect;

  // The shared prepared statement for inserting into the table.
  private static volatile PreparedStatement preparedInsert;

  // Lock for initializing prepared statement objects.
  private static final Object prepareInitLock = new Object();

  static Random rand = new Random();

  Vector<Coupon> coupons;

  static class Coupon {
    public final String code;
    public final Date beginDate;
    public final Date endDate;

    public Coupon(String code) {
      this.code = code;
      this.beginDate = generateRandomDate(true);
      this.endDate = generateRandomDate(false);
    }

    private Date generateRandomDate(boolean past) {
      Calendar cal = Calendar.getInstance();
      cal.set(Calendar.HOUR, 0);
      cal.set(Calendar.MINUTE, 0);
      cal.set(Calendar.SECOND, 0);
      cal.set(Calendar.MILLISECOND, 0);
      cal.add(Calendar.DATE, (past ? -1 : 1) * rand.nextInt(7));
      return cal.getTime();
    }
  }

  public CassandraPersonalization() {
    if (appConfig.readOnly) {
      return;
    }
    coupons = new Vector<Coupon>(appConfig.maxCouponsPerCustomer);
    for (int i = 0; i < appConfig.maxCouponsPerCustomer; i++) {
      coupons.add(new Coupon(Long.toString(800000000000L + (long)i)));
    }
    if (appConfig.numNewCouponsPerCustomer < appConfig.maxCouponsPerCustomer) {
      Collections.shuffle(coupons);
    }
  }

  /**
   * Drop the table created by this app.
   */
  @Override
  public void dropTable() {
    dropCassandraTable(getTableName());
  }

  @Override
  public List<String> getCreateTableStatements() {
    String create_stmt = String.format(
      "CREATE TABLE IF NOT EXISTS %s (" +
      "  customer_id text," +
      "  store_id text," +
      "  coupon_code text," +
      "  begin_date timestamp," +
      "  end_date timestamp," +
      "  relevance_score double," +
      "  PRIMARY KEY ((customer_id, store_id), coupon_code));",
      getTableName());
    return Arrays.asList(create_stmt);
  }

  public String getTableName() {
    return appConfig.tableName != null ? appConfig.tableName : DEFAULT_TABLE_NAME;
  }

  protected PreparedStatement getPreparedSelect(String selectStmt)  {
    if (preparedSelect == null) {
      synchronized (prepareInitLock) {
        if (preparedSelect == null) {
          // Create the prepared statement object.
          preparedSelect = getCassandraClient().prepare(selectStmt);
        }
      }
    }
    return preparedSelect;
  }

  private PreparedStatement getPreparedSelect()  {
    return getPreparedSelect(
      String.format("SELECT * FROM %s WHERE customer_id = ? AND store_id = ?;", getTableName()));
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
    String customerId = key.asString();
    String storeId = Integer.toString(rand.nextInt(appConfig.numStores));
    BoundStatement select = getPreparedSelect().bind(customerId, storeId);
    ResultSet rs = getCassandraClient().execute(select);
    List<Row> rows = rs.all();
    LOG.debug("Read coupon count: " + rows.size());
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

  protected PreparedStatement getPreparedInsert()  {
    return getPreparedInsert(
      String.format(
        "INSERT INTO %s " +
        "(customer_id, store_id, coupon_code, begin_date, end_date, relevance_score) " +
        "VALUES (?, ?, ?, ?, ?, ?);",
        getTableName()));
  }

  private double generateRandomRelevanceScore() {
    return rand.nextDouble();
  }

  private int generateRandomCustomerScore() {
    return rand.nextInt(10);
  }

  @Override
  public long doWrite() {
    BatchStatement batch = new BatchStatement();
    PreparedStatement insert = getPreparedInsert();
    Key key = getSimpleLoadGenerator().getKeyToWrite();
    try {
      int totalCouponCount = 0;
      for (int i = 0; i < appConfig.numStores; i++) {
        String customerId = key.asString();
        String storeId = Integer.toString(i);
        int couponCount = appConfig.numNewCouponsPerCustomer / appConfig.numStores;
        for (int j = 0; j < couponCount; j++) {
          Coupon coupon = coupons.elementAt(j);
          batch.add(insert.bind(customerId, storeId, coupon.code, coupon.beginDate, coupon.endDate,
                                Double.valueOf(generateRandomRelevanceScore())));
        }
        totalCouponCount += couponCount;
      }
      ResultSet resultSet = getCassandraClient().execute(batch);
      LOG.debug("Wrote coupon count: " + totalCouponCount + ", return code: " +
                resultSet.toString());
      getSimpleLoadGenerator().recordWriteSuccess(key);
      return 1;
    } catch (Exception e) {
      getSimpleLoadGenerator().recordWriteFailure(key);
      throw e;
    }
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
      "Sample personalization app built on Cassandra. The app writes out unique customer",
      "ids, each with a set of coupons for different stores. There are multiple readers and",
      "writers that update these keys and read them indefinitely. Note that the number of",
      "reads and writes to perform can be specified as a parameter.");
  }

  @Override
  public List<String> getExampleUsageOptions() {
    return Arrays.asList(
      "--num_unique_keys " + appConfig.numUniqueKeysToWrite,
      "--num_reads " + appConfig.numKeysToRead,
      "--num_writes " + appConfig.numKeysToWrite,
      "--num_threads_read " + appConfig.numReaderThreads,
      "--num_threads_write " + appConfig.numWriterThreads,
      "--num_stores " + appConfig.numStores,
      "--num_new_coupons_per_customer " + appConfig.numNewCouponsPerCustomer,
      "--max_coupons_per_customer " + appConfig.maxCouponsPerCustomer);
  }
}
