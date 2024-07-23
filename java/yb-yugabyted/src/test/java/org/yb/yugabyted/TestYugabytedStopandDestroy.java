package org.yb.yugabyted;

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertTrue;
import java.io.File;
import java.util.ArrayList;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;
import org.yb.minicluster.MiniYugabytedClusterParameters;
import org.yb.minicluster.MiniYugabytedNodeConfigurations;
import org.yb.minicluster.YugabytedCommands;
import org.yb.minicluster.YugabytedTestUtils;

@RunWith(value = YBTestRunner.class)
public class TestYugabytedStopandDestroy extends BaseYbdClientTest {

    private static final Logger LOG = LoggerFactory.getLogger(TestYugabytedStopandDestroy.class);

    public TestYugabytedStopandDestroy() {
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
    public void testYugabytedStopDestroy() throws Exception {
        String baseDir = clusterConfigurations.get(0).baseDir;

        // Run the Yugabyted stop command
        boolean expectedStopStatus = YugabytedCommands.stop(baseDir);
        LOG.info("Yugabyted stop command success status: " + expectedStopStatus);

        // Check the actual status in a loop with retries and timeout
        boolean actualStopStatus = false;
        long startTime = System.currentTimeMillis();

        while (!actualStopStatus) {
            LOG.info("Checking node stop status...");
            actualStopStatus = YugabytedTestUtils.checkNodeStatus(baseDir, "Stopped");
            if (actualStopStatus) {
                break;
            }
            if (System.currentTimeMillis() - startTime > 30000) {
                throw new Exception("Node did not stop within 30 seconds");
            }
            Thread.sleep(5000);
        }

        LOG.info("Yugabyted node actual stop status: " + actualStopStatus);

        // Assert the stop status
        assertEquals("Expected and actual stop statuses should match",
                                            expectedStopStatus, actualStopStatus);

        // Run the Yugabyted destroy command
        boolean expectedDestroyStatus = YugabytedCommands.destroy(baseDir);
        LOG.info("Yugabyted destroy command success status: " + expectedDestroyStatus);

        // Check if the base directory is removed
        boolean isBaseDirRemoved = YugabytedTestUtils.isDirectoryRemoved(baseDir);
        LOG.info("Base directory removal status: " + isBaseDirRemoved);

        // Assert the base directory removal status
        assertTrue("Base directory should be removed after destroy command",
                                                                    isBaseDirRemoved);
    }
}
