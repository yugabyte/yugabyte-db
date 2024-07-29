package org.yb.yugabyted;

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertTrue;
import java.util.ArrayList;
import java.util.Map;
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
import org.json.JSONObject;

@RunWith(value = YBTestRunner.class)
public class TestYugabytedSingleNode extends BaseYbdClientTest {


    private static final Logger LOG = LoggerFactory.getLogger(TestYugabytedSingleNode.class);

    public TestYugabytedSingleNode() {
        clusterParameters = new MiniYugabytedClusterParameters.Builder()
                                .numNodes(1)
                                .build();

        clusterConfigurations = new ArrayList<>();
        for (int i = 0; i < clusterParameters.numNodes; i++) {
            MiniYugabytedNodeConfigurations nodeConfigurations =
                                    new MiniYugabytedNodeConfigurations.Builder()
                .build();

            clusterConfigurations.add(nodeConfigurations);
        }
    }

    @Test(timeout = 300000)
    public void testSingleNode() throws Exception {
        Map<HostAndPort, MiniYBDaemon> masters = miniYugabytedCluster.getYugabytedNodes();
        String expectedClusterUuid =
                        syncClient.getMasterClusterConfig().getConfig().getClusterUuid();
        LOG.info("Expected Cluster UUID: " + expectedClusterUuid);
        String host = "";
        int webPort = 0;
        for (HostAndPort host_and_port : masters.keySet()) {
            host = host_and_port.getHost();
            webPort = masters.get(host_and_port).getWebPort();
        }
        String jsonResponse = YugabytedTestUtils.getClusterConfig(host, webPort);
        JSONObject jsonObject = new JSONObject(jsonResponse);
        String actualClusterUuid = jsonObject.getString("cluster_uuid");
        LOG.info("Actual Cluster UUID: " + actualClusterUuid);
        String baseDir = clusterConfigurations.get(0).baseDir;
        // Assert Cluster UUIDs match
        assertEquals(expectedClusterUuid, actualClusterUuid);

        // Assert YSQL and YCQL connection
        boolean isYsqlConnected = YugabytedTestUtils.testYsqlConnection(baseDir, host);
        boolean isYcqlConnected = YugabytedTestUtils.testYcqlConnection(baseDir, host);

        assertTrue(isYsqlConnected);
        assertTrue(isYcqlConnected);
    }
}
