// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.metrics;

import com.datastax.driver.core.Cluster;
import com.datastax.driver.core.JdkSSLOptions;
import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;
import com.datastax.driver.core.Session;
import com.datastax.driver.core.SSLOptions;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;

import com.google.inject.Inject;
import com.google.inject.Singleton;

import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.common.SslHelper;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.Universe;

import java.io.FileInputStream;
import java.io.IOException;

import java.net.InetSocketAddress;

import java.security.cert.CertificateFactory;
import java.security.cert.X509Certificate;
import java.security.KeyStore;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.Date;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Iterator;
import java.util.LinkedList;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.NavigableMap;
import java.util.Set;
import java.util.TreeMap;
import java.util.UUID;

import javax.net.ssl.SSLContext;
import javax.net.ssl.SSLEngine;
import javax.net.ssl.TrustManager;
import javax.net.ssl.TrustManagerFactory;


import org.joda.time.DateTime;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import org.yb.client.ListTabletServersResponse;
import org.yb.client.YBClient;

import org.yb.util.ServerInfo;

import play.Configuration;
import play.libs.Json;

@Singleton
public class YBMetricQueryComponent {
  public static final Logger LOG = LoggerFactory.getLogger(YBMetricQueryComponent.class);

  // The range in which to group metrics. Metrics in this range are considered
  // at the same timestamp. This needs to match the rate at which the cassandra
  // table records the metrics.
  private static final Integer TIMESTAMP_RANGE_SECS = 30;


  // Each data entry needs to be of the format:
  // [Timestamp, value]
  private static final String DATA_ENTRY_FORMAT = "[%d,\"%f\"]";

  // Each metric needs to be in the following format for each service method.
  /*
  {
   "metric":{
      "service_method": <method_name>
   },
   "values": List of DATA_ENTRY_FORMAT
  }
  */
  private static final String METRIC_DATA_FORMAT = "{\"metric\":{\"service_method\":\"%s\"}," +
                                                   "\"values\":%s}";

  // The final return format needs to be as follows:
  /*
  {
     "status":"success",
     "data":{
        "resultType":"matrix",
        "result": List of METRIC_DATA_FORMAT
     }
  }
  */
  private static final String RESPONSE_FORMAT = "{\"status\":\"success\"," +
                                                "\"data\":{\"resultType\":\"matrix\"," +
                                                "\"result\":%s}}";

  private static final String METRICS_TABLE = "system.metrics";
  private static final String QUERY_FORMAT = "select * from %s where metric = '%s' " +
                                             "and node = '%s' and ts >= %d and ts < %d";

  public List<String> serviceMethods = Arrays.asList("Read", "Write");

  // The count checks the number of RPCs.
  public String countMetricString = "handler_latency_yb_tserver_TabletServerService_%s_count";
  // The sum checks the total time taken for each RPC.
  public String sumMetricString = "handler_latency_yb_tserver_TabletServerService_%s_sum";

  @Inject
  YBClientService ybService;

  public enum Function {
    Sum,
    Average
  }

  private class CassandraConnection {
    Cluster cluster = null;
    Session session = null;
  }

  private CassandraConnection createCassandraConnection(UUID universeUUID) {
    CassandraConnection cc = new CassandraConnection();
    List<InetSocketAddress> addresses = Util.getNodesAsInet(universeUUID);
    if (addresses.isEmpty()) {
      return cc;
    }
    Cluster.Builder builder = Cluster.builder()
                              .addContactPointsWithPorts(addresses);
    String certificate = Universe.get(universeUUID).getCertificate();
    if (certificate != null) {
      builder.withSSL(SslHelper.getSSLOptions(certificate));
    }
    cc.cluster = builder.build();

    LOG.info("Connected to cluster: " + cc.cluster.getClusterName());
    LOG.info("Creating a session...");
    cc.session = cc.cluster.connect();
    return cc;
  }

  private Map<String, String> getTservers(Universe universe) {
    YBClient client = null;
    Map<String, String> tserverMap = new HashMap<>();
    String masterAddresses = universe.getMasterAddresses();
    String certificate = universe.getCertificate();
    try {
      client = ybService.getClient(masterAddresses, certificate);

      // Fetch the tablet servers.
      ListTabletServersResponse listTServerResp = client.listTabletServers();
      for (ServerInfo tserver : listTServerResp.getTabletServersList()) {
        String tserverUUID = tserver.getUuid();
        String host = tserver.getHost();
        NodeDetails node = universe.getNodeByPrivateIP(host);
        if (node != null) {
          tserverMap.put(node.nodeName, tserverUUID);
        }
      }
    } catch (Exception e) {
      LOG.error("Hit error: ", e);
    } finally {
      if (client != null) {
        ybService.closeClient(client, masterAddresses);
      }
    }
    return tserverMap;
  }

  private ResultSet cassandraTserverSelectQuery(String metric, String tserverUUID,
                                                Session session, Universe universe,
                                                long startMs, long endMs) {
    int count = 0;
    ResultSet rs = null;
    String queryString = String.format(QUERY_FORMAT, METRICS_TABLE, metric,
                                       tserverUUID.toString(), (startMs * 1000),
                                       (endMs * 1000));
    return session.execute(queryString);
  }

  // Convert the map to the required output format.
  private List<String> mapToStringList(Map<Long, Double> metrics) {
    List<String> vals = new ArrayList<>();
    for (Entry<Long, Double> entry : metrics.entrySet()) {
      vals.add(String.format(DATA_ENTRY_FORMAT, entry.getKey(), entry.getValue()));
    }
    return vals;
  }

  private double compute(Function function, double initialVal, double valToUpdate, int count) {
    switch (function) {
      case Sum:
        return initialVal + valToUpdate;
      case Average:
        return initialVal + (valToUpdate / count);
      default:
        throw new RuntimeException("Function not supported.");
    }
  }

  public NavigableMap<Long, Double> calculateRate(List<ResultSet> results, Function function,
                                              int numTservers) {
    NavigableMap<Long, Double> timeRangeMap = new TreeMap<>();
    List<String> metricResults = new ArrayList<>();
    for (ResultSet rs : results) {
      List<String> vals = new ArrayList<>();
      long currTimestamp = 0;
      long prevTimestamp = 0;
      double currRate;
      long currVal = 0;
      long prevVal = 0;
      boolean start = true;
      Iterator<Row> rowIter = rs.iterator();
      while (rowIter.hasNext()) {
        Row row = rowIter.next();
        currTimestamp = row.getTimestamp("ts").getTime() / 1000;
        currVal = row.getLong("value");
        if (start) {
          if (!rowIter.hasNext()) {
            return timeRangeMap;
          }
          row = rowIter.next();
          prevVal = currVal;
          prevTimestamp = currTimestamp;
          currTimestamp = row.getTimestamp("ts").getTime() / 1000;
          currVal = row.getLong("value");
          start = false;
        }
        if (!rowIter.hasNext()) {
          break;
        } else {
          currRate = ((double) prevVal - (double) currVal) /
                     ((double) prevTimestamp - (double) currTimestamp);
        }
        Entry<Long, Double> entry = timeRangeMap.floorEntry(currTimestamp);
        // In case there is no entry lower than that, or the entry lower than the key
        // is older than the range for the same timestamped metric.
        if (entry == null || currTimestamp - entry.getKey() > TIMESTAMP_RANGE_SECS) {
          double calcVal = compute(function, 0.0, currRate, numTservers);
          timeRangeMap.put(currTimestamp, calcVal);
        } else {
          double calcVal = compute(function, entry.getValue(), currRate, numTservers);
          timeRangeMap.put(entry.getKey(), calcVal);
        }
        prevVal = currVal;
        prevTimestamp = currTimestamp;
      }
    }
    return timeRangeMap;
  }

  private TreeMap<Long, Double> metricDivide(NavigableMap<Long, Double> metricsCount,
                                             NavigableMap<Long, Double> metricsSum) {
    TreeMap<Long, Double> timeToVal = new TreeMap<>();
    for (Entry<Long, Double> entry : metricsCount.entrySet()) {
      Entry<Long, Double> entry2 = metricsSum.floorEntry(entry.getKey());
      if (entry2 != null && Math.abs(entry2.getKey() - entry.getKey()) < TIMESTAMP_RANGE_SECS) {
        Double val = entry2.getValue() / entry.getValue();
        // Due to the metrics being written and read into/from a user table, we get
        // some rpcs when no workload is running. This causes the latency
        // graph to be jittery. The following code can be uncommented if we want
        // to get rid of the jitters/mark the values only when the RPC count is
        // significant.
        /*
        Double val = 0.0;
        if (entry.getValue() > 10) {
          val = entry2.getValue() / entry.getValue();
        }
        */
        timeToVal.put(entry.getKey(), val);
      }
    }
    return timeToVal;
  }

  private List<ResultSet> queryRunner(String metricName, Session session,
                                      Map<String, String> tserverMap,
                                      JsonNode params, Universe universe,
                                      long start, long end) {
    List<ResultSet> results = new ArrayList<>();
    // Check if metric needs to be reported for only a single tserver.
    if (params.has("exported_instance")) {
      String tserverName = params.path("exported_instance").asText();
      ResultSet rs = cassandraTserverSelectQuery(metricName,
                                                 tserverMap.get(tserverName),
                                                 session, universe,
                                                 start, end);
      if (rs != null && rs.iterator().hasNext()) {
        results.add(rs);
      }
    } else {
      // TODO: Maybe query for all tservers and split here rather than make more
      // database calls.
      for (Entry<String, String> entry : tserverMap.entrySet()) {
        ResultSet rs = cassandraTserverSelectQuery(metricName, entry.getValue(),
                                                   session, universe, start, end);
        if (rs != null && rs.iterator().hasNext()) {
          results.add(rs);
        }
      }
    }
    return results;
  }

  /**
   * Query the metrics table in YB for a given metricType and query params
   * @param queryParams, Query params like start, end timestamps, even filters
   *                     Ex: {"metricKey": "cpu_usage_user",
   *                     "start": <start timestamp>,
   *                     "end": <end timestamp>}
   * @return JsonNode Object
   */
  public JsonNode query(Map<String, String> queryParam) {

    // total_rpcs_per_sec
    // tserver_ops_latency
    JsonNode responseJson = null;
    List<String> metricResults = new ArrayList<>();
    String queryKey = queryParam.get("queryKey");
    Date date = new Date();
    Long startTime = Long.parseLong(queryParam.getOrDefault("start", "0"));
    Long endTime = Long.parseLong(queryParam.getOrDefault("end", String.valueOf(date.getTime())));
    if (startTime == 0) {
      throw new RuntimeException("Start time needs to be provided.");
    }
    if (queryParam.containsKey("filters")) {
      JsonNode params = Util.convertStringToJson(queryParam.get("filters"));
      // Since node prefix format is yb-customer_code-universe_name.
      String[] nodePrefix = params.path("node_prefix").asText().split("-", 3);
      String universeName = null;
      if (nodePrefix.length >= 3) {
        universeName = nodePrefix[2];
      }
      Universe universe = Universe.getUniverseByName(universeName);
      if (universe == null) {
        return null;
      }
      CassandraConnection cc = null;
      Map<String, String> tserverNameToUUID;
      switch (queryKey) {
        case "total_rpcs_per_sec":
          cc = createCassandraConnection(universe.universeUUID);
          if (cc.session == null) {
            return responseJson;
          }
          universe = Universe.get(universe.universeUUID);
          tserverNameToUUID = getTservers(universe);
          for (String method : serviceMethods) {
            String metricName = String.format(countMetricString, method);
            List<ResultSet> results = queryRunner(metricName, cc.session, tserverNameToUUID, params,
                                                  universe, startTime, endTime);
            NavigableMap<Long, Double> metricsVals = calculateRate(results, Function.Sum,
                                                                   results.size());
            if (!metricsVals.isEmpty()) {
              metricResults.add(String.format(METRIC_DATA_FORMAT, method,
                                              mapToStringList(metricsVals)));
            }
          }
          break;
        case "tserver_ops_latency":
          cc = createCassandraConnection(universe.universeUUID);
          if (cc.session == null) {
            return responseJson;
          }
          universe = Universe.get(universe.universeUUID);
          tserverNameToUUID = getTservers(universe);
          for (String method : serviceMethods) {
            String metricCount = String.format(countMetricString, method);
            String metricSum = String.format(sumMetricString, method);
            List<ResultSet> resultCount = queryRunner(metricCount, cc.session, tserverNameToUUID,
                                                      params, universe, startTime, endTime);
            List<ResultSet> resultSum = queryRunner(metricSum, cc.session, tserverNameToUUID,
                                                    params, universe, startTime, endTime);
            NavigableMap<Long, Double> metricsCount = calculateRate(resultCount, Function.Average,
                                                                    resultCount.size());
            NavigableMap<Long, Double> metricsSum = calculateRate(resultSum, Function.Average,
                                                                  resultSum.size());
            TreeMap<Long, Double> metricsVals = metricDivide(metricsCount, metricsSum);
            if (!metricsVals.isEmpty()) {
              metricResults.add(String.format(METRIC_DATA_FORMAT, method,
                                              mapToStringList(metricsVals)));
            }
          }
          break;
        default:
          LOG.warn("Query: " + queryKey + " not supported.");
      }
      if (cc != null) {
        if (cc.session != null) cc.session.close();
        if (cc.cluster != null) cc.cluster.close();
      }
      if (!metricResults.isEmpty()) {
        String returnJson = String.format(RESPONSE_FORMAT, metricResults);
        responseJson = Util.convertStringToJson(returnJson);
      }
    }
    return responseJson;
  }
}
