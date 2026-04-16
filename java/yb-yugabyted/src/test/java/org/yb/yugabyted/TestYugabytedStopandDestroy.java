package org.yb.yugabyted;

import java.util.ArrayList;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;
import org.yb.minicluster.MiniYugabytedClusterParameters;
import org.yb.minicluster.MiniYugabytedNodeConfigurations;
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
        YugabytedTestUtils.stopAndDestroyYugabytedTest(baseDir);
    }
}
