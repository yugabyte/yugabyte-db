// Copyright (c) Yugabytedb, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied. See the License for the specific language governing permissions and limitations
// under the License.
//

package org.yb.pgsql;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;
import org.yb.minicluster.Metrics;
import org.yb.minicluster.Metrics.YSQLStat;
import org.yb.minicluster.MiniYBDaemon;
import com.google.common.collect.Range;
import com.google.common.collect.ImmutableMap;
import com.google.gson.JsonParser;
import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import org.json.JSONArray;
import org.json.JSONObject;
import org.yb.util.json.JsonUtil;

import java.lang.Math;
import java.net.URL;
import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.Iterator;
import java.util.Scanner;

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertGreaterThanOrEqualTo;
import static org.yb.AssertionWrappers.assertTrue;

@RunWith(value = YBTestRunner.class)
public class TestHdr extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestHdr.class);

  private static final int DEFAULT_YB_HDR_BUCKET_FACTOR = 16;
  private static final double DEFAULT_YB_HDR_RESOLUTION = 0.1;
  /* TODO: Use for overflow bucket testing after GUC definable max latency is implemented */
  private static final double DEFAULT_YB_HDR_MAX_LATENCY = 1677721.6;

  private static int getBucketIndex(long x, int bucketFactor) {
    return Long.numberOfTrailingZeros((Long.highestOneBit(x | (bucketFactor - 1)) << 1))
        - (int) (Math.log(bucketFactor) / Math.log(2));
  }

  private static int getSubbucketIndex(long x, int bucketIndex) {
    return (int) (x >> bucketIndex);
  }

  private static long getEquivalentValueRange(int bucketIndex, int subBucketIndex,
      int bucketFactor) {
    int adjustedBucket = (subBucketIndex >= bucketFactor) ? (bucketIndex + 1) : bucketIndex;
    long x = 1L << (adjustedBucket);
    return x;
  }

  private static long getSubbucketBottom(int bucketIndex, int subBucketIndex) {
    return ((long) subBucketIndex) << (bucketIndex);
  }

  private static long getSubbucketTop(long subBucketBottom, int bucketIndex, int subBucketIndex,
      int bucketFactor) {
    return subBucketBottom + getEquivalentValueRange(bucketIndex, subBucketIndex, bucketFactor);
  }

  static class BucketInterval {
    public double lower;
    public double upper;

    public BucketInterval(double first, double second) {
      this.lower = first;
      this.upper = second;
    }

    @Override
    public boolean equals(Object obj) {
      if (this == obj) {
        return true;
      }
      if (!(obj instanceof BucketInterval)) {
        return false;
      }
      BucketInterval other = (BucketInterval) obj;
      return Double.compare(this.lower, other.lower) == 0
          && Double.compare(this.upper, other.upper) == 0;
    }
  }

  /*
   * Current jsonb creation in pg_stat_statements truncates floats to 1 dec place, matching that
   * behavior here. TODO: account for variable decimal places in phase 3, after GUC definable
   * resolutions are implemented
   */
  private static double truncateDoubleOnePlace(double value) {
    double truncated_value = (long) (value * 10) / 10.0;
    return truncated_value;
  }

  private static BucketInterval getExpInterval(double latency, int bucketFactor, boolean round) {
    /*
     * Round for intervals (ex. truncation messes up expectations for 4.8/0.1), and do not round for
     * latencies (match hdr_histogram behavior)
     */
    long latencyLong = round ? Math.round(latency / DEFAULT_YB_HDR_RESOLUTION)
        : (long) (latency / DEFAULT_YB_HDR_RESOLUTION);
    int bucketIndex = getBucketIndex(latencyLong, bucketFactor);
    int subBucketIndex = getSubbucketIndex(latencyLong, bucketIndex);
    long subBucketBottom = getSubbucketBottom(bucketIndex, subBucketIndex);
    long sub_bucket_top =
        getSubbucketTop(subBucketBottom, bucketIndex, subBucketIndex, bucketFactor);
    double trunc_bottom =
        truncateDoubleOnePlace((double) subBucketBottom * DEFAULT_YB_HDR_RESOLUTION);
    double trunc_top = truncateDoubleOnePlace((double) sub_bucket_top * DEFAULT_YB_HDR_RESOLUTION);
    return new BucketInterval(trunc_bottom, trunc_top);
  }

  /*
   * Matches for strings formatted [num1,num2) and returns num1 and num2
   * If overflow bucket [num1,), returns num2 as INFINITY
   */
  private static BucketInterval parseInterval(String input) {
    int startIndex = input.indexOf('[') + 1;
    int commaIndex = input.indexOf(',');

    // Extract the substring containing num1
    String num1Str = input.substring(startIndex, commaIndex).trim();
    double num1 = Double.parseDouble(num1Str);

    int endIndex = input.indexOf(')');
    double num2 = Double.POSITIVE_INFINITY;
    if (endIndex - commaIndex > 1)
    {
      // Extract the substring containing num2
      String num2Str = input.substring(commaIndex + 1, endIndex).trim();
      num2 = Double.parseDouble(num2Str);
    }

    return new BucketInterval(num1, num2);
}

  private static void checkJSONObject(JSONObject jsonObject, int expCount, double actualLatency,
      BucketInterval expInterval) {
    Iterator<String> keys = jsonObject.keys();

    while (keys.hasNext()) {
      String key = keys.next();
      BucketInterval parsedInterval = parseInterval(key);
      double lowerBound = parsedInterval.lower;
      double upperBound = parsedInterval.upper;

      /* verify jsonb matches with derived interval for this latency */
      assertEquals(expInterval, parsedInterval);
      /* verify latency falls within jsonb interval */
      assertTrue(Range.closedOpen(lowerBound, upperBound).contains(actualLatency));
      /* verify counts += 1 for each pg_sleep with latency in the jsonb interval */
      assertEquals(expCount, jsonObject.getInt(key));
    }
  }

  private static void checkJSONArray(JSONArray jsonArray, int expCount, double minTime,
      double maxTime, int bucketFactor) {
    JSONObject jsonObject;
    double prevUpper = 0.0;
    int currCount = 0;

    for (int i = 0; i < jsonArray.length(); i++) {
      jsonObject = jsonArray.getJSONObject(i);
      Iterator<String> keys = jsonObject.keys();
      while (keys.hasNext()) {
        String key = keys.next();
        BucketInterval parsedInterval = parseInterval(key);
        double lowerBound = parsedInterval.lower;
        double upperBound = parsedInterval.upper;
        BucketInterval expInterval = getExpInterval(lowerBound, bucketFactor, true);
        currCount += jsonObject.getInt(key);

        /* verify ends of array contain global max/min */
        if (i == 0) {
          assertTrue(Range.closedOpen(lowerBound, upperBound).contains(minTime));
        } else if (i == jsonArray.length() - 1) {
          assertTrue(Range.closedOpen(lowerBound, upperBound).contains(maxTime));
        }
        /* verify each jsonObject matches its lower bound's expected interval */
        assertEquals(expInterval, parsedInterval);
        /* verify jsonObjects follow ascending ordering */
        assertGreaterThanOrEqualTo(lowerBound, prevUpper);
        prevUpper = upperBound;
      }
    }
    /* verify histogram total counts */
    assertEquals(expCount, currCount);
  }

  private static int checkJsonArray(JsonArray jsonArray, double minTime, double maxTime,
      int bucketFactor) {
    LOG.info("Response:\n" + JsonUtil.asPrettyString(jsonArray));
    JsonObject jsonObject;
    double prevUpper = 0.0;
    int currCount = 0;

    for (int i = 0; i < jsonArray.size(); i++) {
      jsonObject = jsonArray.get(i).getAsJsonObject();
      for (String key : jsonObject.keySet()) {
        BucketInterval parsedInterval = parseInterval(key);
        double lowerBound = parsedInterval.lower;
        double upperBound = parsedInterval.upper;
        BucketInterval expInterval = getExpInterval(lowerBound, bucketFactor, true);
        currCount += jsonObject.get(key).getAsInt();

        /* verify ends of array contain global max/min */
        if (i == 0) {
          assertTrue(Range.closedOpen(lowerBound, upperBound).contains(minTime));
        } else if (i == jsonArray.size() - 1) {
          assertTrue(Range.closedOpen(lowerBound, upperBound).contains(maxTime));
        }
        /* verify each jsonObject matches its lower bound's expected interval */
        assertEquals(expInterval, parsedInterval);
        /* verify jsonObjects follow ascending ordering */
        assertGreaterThanOrEqualTo(lowerBound, prevUpper);
        prevUpper = upperBound;
      }
    }
    /* return histogram total counts */
    return currCount;
  }

  private static void runCheckSleepQuery(Statement statement, int bucketFactor)
      throws SQLException {
    ResultSet rs = statement.executeQuery("SELECT query, min_time, max_time, yb_latency_histogram "
        + "FROM pg_stat_statements WHERE query like '%select pg_sleep%'");

    assertTrue(rs.next());
    double minTime = rs.getDouble(2);
    double maxTime = rs.getDouble(3);
    String latencyHistogram = rs.getString(4);
    BucketInterval minInterval = getExpInterval(minTime, bucketFactor, false);
    BucketInterval maxInterval = getExpInterval(maxTime, bucketFactor, false);

    JSONArray jsonArray = new JSONArray(latencyHistogram);
    JsonElement jsonPrint = JsonParser.parseString(latencyHistogram);
    LOG.info("Response:\n" + JsonUtil.asPrettyString(jsonPrint));

    if (minInterval.equals(maxInterval)) {
      assertEquals(1, jsonArray.length());
      checkJSONObject(jsonArray.getJSONObject(0), 2, minTime, minInterval);
      checkJSONObject(jsonArray.getJSONObject(0), 2, maxTime, maxInterval);
    } else {
      assertEquals(2, jsonArray.length());
      checkJSONObject(jsonArray.getJSONObject(0), 1, minTime, minInterval);
      checkJSONObject(jsonArray.getJSONObject(jsonArray.length() - 1), 1, maxTime, maxInterval);
    }
  }

  private static void runCheckHdrArray(Statement statement, int iterations, int bucketFactor)
      throws SQLException {
    ResultSet rs = statement.executeQuery("SELECT query, min_time, max_time, yb_latency_histogram "
        + "FROM pg_stat_statements WHERE query like '%select pg_sleep%'");

    assertTrue(rs.next());
    double minTime = rs.getDouble(2);
    double maxTime = rs.getDouble(3);
    String latencyHistogram = rs.getString(4);
    BucketInterval minInterval = getExpInterval(minTime, bucketFactor, false);
    BucketInterval maxInterval = getExpInterval(maxTime, bucketFactor, false);
    JSONArray jsonArray = new JSONArray(latencyHistogram);

    /* unlikely after 1000 pg_sleep calls, but needs to be accounted for */
    if (minInterval.equals(maxInterval)) {
      assertEquals(1, jsonArray.length());
      checkJSONObject(jsonArray.getJSONObject(0), iterations, minTime, minInterval);
      checkJSONObject(jsonArray.getJSONObject(0), iterations, maxTime, maxInterval);
    } else {
      /* expect an array of jsonb intervals */
      checkJSONArray(jsonArray, iterations, minTime, maxTime, bucketFactor);
    }
  }

  /* Basic hdr functionality test */
  @Test
  public void testMinMaxSleeps() throws SQLException {
    try (Statement statement = connection.createStatement()) {
      statement.execute("select pg_stat_statements_reset()");
      statement.execute("select pg_sleep(0.01)");
      statement.execute("select pg_sleep(0.03)");

      runCheckSleepQuery(statement, DEFAULT_YB_HDR_BUCKET_FACTOR);
    }
  }

  /* Basic hdr functionality test for bucket factor 8 */
  @Test
  public void testMinMaxSleepsBF8() throws Exception {
    int tserver = spawnTServerWithFlags(
        ImmutableMap.of("ysql_pg_conf_csv", "pg_stat_statements.yb_hdr_bucket_factor=8"));

    try (Connection connection = getConnectionBuilder().withTServer(tserver).connect();
        Statement statement = connection.createStatement()) {
      statement.execute("select pg_stat_statements_reset()");
      statement.execute("select pg_sleep(0.01)");
      statement.execute("select pg_sleep(0.03)");

      runCheckSleepQuery(statement, 8);
    }
  }

  /* Large no. of pg_sleep calls, validate range of histogram intervals */
  @Test
  public void testHdrRange() throws Exception {
    try (Statement statement = connection.createStatement()) {
      double minVal = 0.001;
      double maxVal = 0.01;
      int iterations = 1000;
      statement.execute("select pg_stat_statements_reset()");
      for (int i = 0; i < iterations; i++) {
        double randVal = minVal + (Math.random() * (maxVal - minVal));
        String randString = String.valueOf(randVal);
        statement.execute("select pg_sleep(" + randString + ");");
      }

      runCheckHdrArray(statement, iterations, DEFAULT_YB_HDR_BUCKET_FACTOR);
    }
  }

  private static void verifyStatementHist(String statName, int iterations) throws Exception {
    int currIterations = 0;
    for (MiniYBDaemon ts : miniCluster.getTabletServers().values()) {
      URL url = new URL(
          String.format("http://%s:%d/statements", ts.getLocalhostIP(), ts.getPgsqlWebPort()));
      try (Scanner scanner = new Scanner(url.openConnection().getInputStream())) {
        JsonElement tree = JsonParser.parseString(scanner.useDelimiter("\\A").next());
        JsonObject obj = tree.getAsJsonObject();
        YSQLStat ysqlStat = new Metrics(obj, true).getYSQLStat(statName);
        if (ysqlStat != null) {
          double minTime = ysqlStat.min_time;
          double maxTime = ysqlStat.max_time;
          currIterations += checkJsonArray(ysqlStat.yb_latency_histogram, minTime, maxTime,
              DEFAULT_YB_HDR_BUCKET_FACTOR);
        }
      }
    }
    assertEquals(iterations, currIterations);
  }

  @Test
  public void testStatementHist() throws Exception {
    try (Statement statement = connection.createStatement()) {
      double minVal = 0.001;
      double maxVal = 0.01;
      int iterations = 1000;
      statement.execute("select pg_stat_statements_reset()");
      for (int i = 0; i < iterations; i++) {
        double randVal = minVal + (Math.random() * (maxVal - minVal));
        String randString = String.format("select pg_sleep(%f)", randVal);
        statement.execute(randString);
      }

      verifyStatementHist("select pg_sleep($1)", iterations);
    }
  }

}
