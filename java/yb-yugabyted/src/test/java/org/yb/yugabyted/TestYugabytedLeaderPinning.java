package org.yb.yugabyted;

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertTrue;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.TimeUnit;

import org.json.JSONArray;
import org.json.JSONObject;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;
import org.yb.minicluster.MiniYBDaemon;
import org.yb.minicluster.MiniYugabytedClusterParameters;
import org.yb.minicluster.MiniYugabytedNodeConfigurations;
import org.yb.minicluster.YugabytedCommands;
import org.yb.minicluster.YugabytedTestUtils;

import com.google.common.net.HostAndPort;
@RunWith(value = YBTestRunner.class)
public class TestYugabytedLeaderPinning extends BaseYbdClientTest {

    private static final Logger LOG = LoggerFactory.getLogger(TestYugabytedLeaderPinning.class);

    private final String[] CLOUD_LOCATIONS = {
            "aws.us-west-1.us-west-1a",
            "aws.us-west-1.us-west-1b",
            "aws.us-west-1.us-west-1c"
    };

    public TestYugabytedLeaderPinning() {

        clusterParameters = new MiniYugabytedClusterParameters.Builder()
                                .numNodes(3)
                                .build();

        clusterConfigurations = new ArrayList<>();
        for (int i = 0; i < clusterParameters.numNodes; i++) {

            Map<String, String> flags = new HashMap<>();
            flags.put("cloud_location", CLOUD_LOCATIONS[i]);

            MiniYugabytedNodeConfigurations nodeConfigurations = new
                                        MiniYugabytedNodeConfigurations.Builder()
                .yugabytedFlags(flags)
                .build();

            clusterConfigurations.add(nodeConfigurations);
        }
    }

    @Test(timeout = 300000)
    public void TestLeaderPinning() throws Exception {
        String baseDir = clusterConfigurations.get(0).getBaseDir();
        Map<HostAndPort, MiniYBDaemon> masters = miniYugabytedCluster.getYugabytedNodes();
        String host = "";
        int webPort = 0;
        for (HostAndPort host_and_port : masters.keySet()) {
            host = host_and_port.getHost();
            webPort = masters.get(host_and_port).getWebPort();
        }

        boolean dataPlacementConfigured = YugabytedCommands.configureDataPlacement(baseDir);
        LOG.info("Data placement configured: " + dataPlacementConfigured);
        assertTrue("Data placement should be configured successfully", dataPlacementConfigured);

        // Sleep for 5 seconds to allow configuration changes to take effect
        TimeUnit.SECONDS.sleep(5);

        String jsonResponse = YugabytedTestUtils.getClusterConfig(host, webPort);
        JSONObject jsonObject = new JSONObject(jsonResponse);
        LOG.info("Cluster config JSON response: " + jsonResponse);
        JSONArray multiAffinitizedLeaders = jsonObject
                .getJSONObject("replication_info")
                .getJSONArray("multi_affinitized_leaders");

        String[] expectedZones = {"us-west-1b", "us-west-1c", "us-west-1a"};

        for (int i = 0; i < multiAffinitizedLeaders.length(); i++) {
            JSONObject leaderBlock = multiAffinitizedLeaders.getJSONObject(i);
            JSONObject cloudInfo = leaderBlock.getJSONArray("zones").getJSONObject(0);

            String zone = cloudInfo.getString("placement_zone");

            LOG.info("Expected zone: " + expectedZones[i] + ", Actual zone: " + zone);

            assertEquals(expectedZones[i], zone);
            LOG.info("Leader pinning matches for zone: " + zone);
        }
    }
}
