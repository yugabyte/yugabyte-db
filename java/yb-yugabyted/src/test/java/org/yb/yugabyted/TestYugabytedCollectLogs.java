// package org.yb.yugabyted;

// import static org.yb.AssertionWrappers.assertEquals;
// import java.util.ArrayList;
// import org.junit.Test;
// import org.junit.runner.RunWith;
// import org.slf4j.Logger;
// import org.slf4j.LoggerFactory;
// import org.yb.YBTestRunner;
// import org.yb.minicluster.MiniYugabytedClusterParameters;
// import org.yb.minicluster.MiniYugabytedNodeConfigurations;
// import org.yb.minicluster.YugabytedCommands;

// @RunWith(value = YBTestRunner.class)
// public class TestYugabytedCollectLogs extends BaseYbdClientTest {

//     private static final Logger LOG = LoggerFactory.getLogger(TestYugabytedCollectLogs.class);

//     public TestYugabytedCollectLogs() {
//         clusterParameters = new MiniYugabytedClusterParameters.Builder()
//                                 .numNodes(1)
//                                 .build();

//         clusterConfigurations = new ArrayList<>();
//         for (int i = 0; i < clusterParameters.numNodes; i++) {
//             MiniYugabytedNodeConfigurations nodeConfigurations = new
//                                             MiniYugabytedNodeConfigurations.Builder()
//                 .build();

//             clusterConfigurations.add(nodeConfigurations);
//         }
//     }

//     @Test(timeout = 60000)
//     public void testYugabytedCollectLogs() throws Exception {
//         //Run the collect logs command
//         String baseDir = clusterConfigurations.get(0).baseDir;
//         boolean success = YugabytedCommands.collectLogs(baseDir);
//         LOG.info("Collect logs command success status: " + success);
//         assertEquals("Collect logs command did not indicate success.", true, success);
//     }
// }
