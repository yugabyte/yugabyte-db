package org.yb.yugabyted;

import static org.yb.AssertionWrappers.assertEquals;
import java.util.ArrayList;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;
import org.yb.minicluster.MiniYugabytedClusterParameters;
import org.yb.minicluster.MiniYugabytedNodeConfigurations;
import org.yb.minicluster.YugabytedCommands;

@RunWith(value = YBTestRunner.class)
public class TestYugabytedEAR extends BaseYbdClientTest {


    private static final Logger LOG = LoggerFactory.getLogger(TestYugabytedEAR.class);

    public TestYugabytedEAR() {
        clusterParameters = new MiniYugabytedClusterParameters.Builder()
                                .numNodes(1)
                                .build();

        clusterConfigurations = new ArrayList<>();
        for (int i = 0; i < clusterParameters.numNodes; i++) {
            MiniYugabytedNodeConfigurations nodeConfigurations = new
                                            MiniYugabytedNodeConfigurations.Builder()
                .build();

            clusterConfigurations.add(nodeConfigurations);
        }
    }

    @Test(timeout = 300000)
    public void testEAR() throws Exception {
        String baseDir = clusterConfigurations.get(0).baseDir;
        boolean expectedEARStatus = YugabytedCommands.enableEncryptionAtRest(baseDir);
        LOG.info("Expected EAR Status: " + expectedEARStatus);
        boolean actualEARStatus = syncClient.isEncryptionEnabled().getFirst();
        LOG.info("Actual EAR Status: " + actualEARStatus);
        assertEquals("Expected and Actual EAR Status should match",
                                            expectedEARStatus, actualEARStatus);
    }
}
