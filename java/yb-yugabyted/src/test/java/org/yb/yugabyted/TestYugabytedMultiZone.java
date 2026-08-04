package org.yb.yugabyted;

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertTrue;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Map;
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
import org.yb.minicluster.YugabytedTestUtils;

import com.google.common.net.HostAndPort;
@RunWith(value = YBTestRunner.class)
public class TestYugabytedMultiZone extends BaseYbdClientTest {

    private static final Logger LOG = LoggerFactory.getLogger(TestYugabytedMultiZone.class);

    private final String[] CLOUD_LOCATIONS = {
            "aws.us-west-1.us-west-1a",
            "aws.us-west-1.us-west-1b",
            "aws.us-west-1.us-west-1c"
    };

    public TestYugabytedMultiZone() {

        clusterParameters = new MiniYugabytedClusterParameters.Builder()
                                .numNodes(3)
                                .build();

        clusterConfigurations = new ArrayList<>();
        for (int i = 0; i < clusterParameters.numNodes; i++) {

            Map<String, String> flags = new HashMap<>();
            flags.put("cloud_location", CLOUD_LOCATIONS[i]);

            MiniYugabytedNodeConfigurations nodeConfigurations =
                                new MiniYugabytedNodeConfigurations.Builder()
                .yugabytedFlags(flags)
                .build();

            clusterConfigurations.add(nodeConfigurations);
        }
    }

    @Test(timeout = 300000)
    public void testMultiZone() throws Exception {
        Map<HostAndPort, MiniYBDaemon> masters = miniYugabytedCluster.getYugabytedNodes();
        String host = "";
        int webPort = 0;
        for (HostAndPort host_and_port : masters.keySet()) {
            host = host_and_port.getHost();
            webPort = masters.get(host_and_port).getWebPort();
        }

        String jsonResponse = YugabytedTestUtils.getClusterConfig(host, webPort);
        JSONObject jsonObject = new JSONObject(jsonResponse);
        LOG.info("Cluster config JSON response: " + jsonResponse);

        // Assert multi-zone cloud locations
        JSONArray placementBlocks = jsonObject
                .getJSONObject("replication_info")
                .getJSONObject("live_replicas")
                .getJSONArray("placement_blocks");

        for (int i = 0; i < placementBlocks.length(); i++) {
            JSONObject block = placementBlocks.getJSONObject(i);
            JSONObject cloudInfo = block.getJSONObject("cloud_info");

            String cloud = cloudInfo.getString("placement_cloud");
            String region = cloudInfo.getString("placement_region");
            String zone = cloudInfo.getString("placement_zone");

            assertEquals("aws", cloud);
            assertEquals("us-west-1", region);
            assertTrue(zone.equals("us-west-1a") ||
                        zone.equals("us-west-1b") || zone.equals("us-west-1c"));
        }
    }
}
