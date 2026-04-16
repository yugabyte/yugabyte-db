package org.yb.yugabyted;

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertNotNull;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Map;
import java.util.Scanner;
import org.json.JSONArray;
import org.json.JSONObject;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;
import org.yb.minicluster.MiniYBDaemon;
import org.yb.minicluster.MiniYugabytedClusterParameters;
import org.yb.minicluster.MiniYugabytedNodeConfigurations;
import com.google.common.net.HostAndPort;
import org.yb.minicluster.YugabytedTestUtils;
import java.util.List;

@RunWith(value = YBTestRunner.class)
public class TestHealthCheckAPI extends BaseYbdClientTest {

  private static final org.slf4j.Logger LOG = LoggerFactory.getLogger(TestHealthCheckAPI.class);

  protected int DEFAULT_YUGABYTED_API_UI_PORT = 15433;

  private List<String> jsonArrayToSortedList(JSONArray array) {
    List<String> list = new ArrayList<>();

    int n = list.size();

    for (int i = 0; i < n; i++) {
      list.add(array.get(i).toString());
    }
    Collections.sort(list);
    return list;
  }

  public TestHealthCheckAPI() {
    // Starting a single node cluster in this constructor
    // with UI enabled so as to test API requests and response.
    clusterParameters = new MiniYugabytedClusterParameters.Builder()
        .numNodes(1)
        .build();
    clusterParameters.setEnableYugabytedUI(true);
    clusterConfigurations = new ArrayList<>();
    for (int i = 0; i < clusterParameters.numNodes; i++) {
      MiniYugabytedNodeConfigurations nodeConfigurations =
        new MiniYugabytedNodeConfigurations.Builder().build();
      clusterConfigurations.add(nodeConfigurations);
    }
  }
  private String fetchHealthCheck(String host, int port, String apiPath) throws Exception {
    String urlStr = String.format("http://%s:%d%s", host, port, apiPath);
    URL url = new URL(urlStr);
    HttpURLConnection connection = (HttpURLConnection) url.openConnection();
    connection.setRequestMethod("GET");
    connection.connect();

    int responseCode = connection.getResponseCode();
    if (responseCode != 200) {
      throw new RuntimeException("HttpResponseCode: " + responseCode);
    } else {
      StringBuilder inline = new StringBuilder();
      Scanner scanner = new Scanner(url.openStream());
      while (scanner.hasNext()) {
        inline.append(scanner.nextLine());
      }
      scanner.close();
      return inline.toString();
    }
  }

  @Test(timeout = 60000)
  public void testHealthCheckAPI() throws Exception {

    boolean testsPassed = false;

    try {
      Map<HostAndPort, MiniYBDaemon> masters = miniYugabytedCluster.getYugabytedNodes();
      String host = "";
      int webPort = 0;
      for (HostAndPort host_and_port : masters.keySet()) {
        host = host_and_port.getHost();
        webPort = masters.get(host_and_port).getWebPort();
      }

      HostAndPort leaderMasterHostAndPort = syncClient.getLeaderMasterHostAndPort();
      LOG.info("Leader host and port: " + leaderMasterHostAndPort);

      // Fetching health-check response from yugabyted-ui api
      String jsonResponse = fetchHealthCheck(leaderMasterHostAndPort.getHost(),
          DEFAULT_YUGABYTED_API_UI_PORT,
          "/api/health-check");
      LOG.info("JSON Response: " + jsonResponse);
      JSONObject yugabytedUIResponse = new JSONObject(jsonResponse);
      assertNotNull("Response from Yugabyted UI should not be null", yugabytedUIResponse);


      // Fetching health-check response from yugabyted-master-webport api
      String expectedOutputJSON = fetchHealthCheck(host, webPort, "/api/v1/health-check");
      LOG.info("JSON Response: " + expectedOutputJSON);
      JSONObject masterResponse = new JSONObject(expectedOutputJSON);
      assertNotNull("Response from Master endpoint should not be null", masterResponse);
      JSONObject dataFromYugabyted = yugabytedUIResponse.getJSONObject("data");

      List<String> deadNodesFromUI =
        jsonArrayToSortedList(dataFromYugabyted.getJSONArray("dead_nodes"));
      List<String> underReplicatedFromUI = jsonArrayToSortedList(
          dataFromYugabyted.getJSONArray("under_replicated_tablets"));
      int uptimeFromUI = dataFromYugabyted.getInt("most_recent_uptime");

      List<String> deadNodesFromMaster =
        jsonArrayToSortedList(masterResponse.getJSONArray("dead_nodes"));
      List<String> underReplicatedFromMaster = jsonArrayToSortedList(
          masterResponse.getJSONArray("under_replicated_tablets"));
      int uptimeFromMaster = masterResponse.getInt("most_recent_uptime");

      assertEquals("Dead nodes mismatch", deadNodesFromMaster, deadNodesFromUI);
      assertEquals("Under-replicated tablets mismatch", underReplicatedFromMaster,
        underReplicatedFromUI);
      assertEquals("Uptime mismatch", uptimeFromMaster, uptimeFromUI);

      testsPassed = true;

    } catch (Exception e) {
      LOG.error("Test failed with exception: " + e.getMessage(), e);
      throw e;
    } finally {
      if (testsPassed) {
        try {
          String baseDir = clusterConfigurations.get(0).baseDir;
          // if (baseDir.startsWith("~")) {
          //   baseDir = baseDir.replaceFirst("~", System.getProperty("user.home"));
          // }
          LOG.info("The current base directory is this: " + baseDir);
          YugabytedTestUtils.stopAndDestroyYugabytedTest(baseDir);
        } catch (Exception e) {
          LOG.error("Failed to stop and destroy the Yugabyte cluster: " + e.getMessage());
        }
      } else {
        LOG.info("Skipping cluster stop and teardown due to test failure.");
      }
    }
  }
}
