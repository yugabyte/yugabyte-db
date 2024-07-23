package org.yb.yugabyted;

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
import org.yb.minicluster.YugabytedCommands;
import org.yb.minicluster.YugabytedTestUtils;
import com.google.common.net.HostAndPort;

@RunWith(value = YBTestRunner.class)
public class TestYugabytedDemoConnect extends BaseYbdClientTest {

    private static final Logger LOG = LoggerFactory.getLogger(TestYugabytedDemoConnect.class);

    public TestYugabytedDemoConnect() {
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
    public void testDemoConnect() throws Exception {
        String baseDir = clusterConfigurations.get(0).baseDir;
        Map<HostAndPort, MiniYBDaemon> masters = miniYugabytedCluster.getYugabytedNodes();
        String host = "";
        int webPort = 0;
        for (HostAndPort host_and_port : masters.keySet()) {
            host = host_and_port.getHost();
            webPort = masters.get(host_and_port).getWebPort();
        }

        Process demoConnectProcess = YugabytedCommands.startDemoConnect(baseDir);
        LOG.info("Demo connect command started.");

        boolean isDatabaseCreated= false;
        for (int i = 0; i < 3; i++) {
            if (YugabytedTestUtils.isDatabaseCreated(host, webPort)) {
                isDatabaseCreated = true;
                break;
            }
            LOG.info("Database 'yb_demo_northwind' not running yet, waiting for 30 seconds...");
            Thread.sleep(30000); // Wait for 30 seconds before the next check
        }

        // Destroy the demo connect process if it's still running
        if (demoConnectProcess.isAlive()) {
            demoConnectProcess.destroy();
            LOG.info("Demo connect process destroyed.");
        }

        LOG.info("Database running status: " + isDatabaseCreated);
        assertTrue("Database 'yb_demo_northwind' should be in 'RUNNING' state",
                                                                        isDatabaseCreated);
    }
}
