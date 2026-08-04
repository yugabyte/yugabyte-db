package org.yb.yugabyted;

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertNotEquals;

import java.net.HttpURLConnection;
import java.net.MalformedURLException;
import java.net.URL;
import java.util.ArrayList;
import java.util.Map;
import java.util.Scanner;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;
import org.yb.client.TestMasterStatus;
import org.yb.minicluster.MiniYBDaemon;
import org.yb.minicluster.MiniYugabytedClusterParameters;
import org.yb.minicluster.MiniYugabytedNodeConfigurations;

import com.google.common.net.HostAndPort;

@RunWith(value = YBTestRunner.class)
public class TestYugabytedMasterStatus extends BaseYbdClientTest {

    private static final Logger LOG = LoggerFactory.getLogger(TestMasterStatus.class);

    public TestYugabytedMasterStatus() {
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

    private String isMasterLeader(String host, int port) throws Exception {
        try {
            // call the <web>/api/v1/is-leader JSON endpoint
            String raft_role = "LEADER";
            String urlstr = String.format("http://%s:%d/api/v1/is-leader",
                    host, port);

            URL url = new URL(urlstr);
            HttpURLConnection huc = (HttpURLConnection) url.openConnection();
            huc.setRequestMethod("HEAD");
            huc.connect();
            int statusCode = huc.getResponseCode();

            if (statusCode != 200) {
                raft_role = "FOLLOWER";
            }

            LOG.info("Master RAFT Role: " + host + ":" + Integer.toString(port) +
                    " = " + raft_role);
            return raft_role;
        } catch (MalformedURLException e) {
            throw new InternalError(e.getMessage());
        }
    }

    private String allMasterStatuses(String host, int port) throws Exception {
        try {
            // call the <web>/api/v1/masters JSON endpoint
            String urlstr = String.format("http://%s:%d/api/v1/masters",
                    host, port);

            URL url = new URL(urlstr);
            Scanner scanner = new Scanner(url.openConnection().getInputStream());
            scanner.useDelimiter("\\A");
            String output = scanner.hasNext() ? scanner.next() : "";
            scanner.close();

            LOG.info("Master Statuses: \n" + host + ":" + Integer.toString(port) +
                    " = " + output);
            return output;
        } catch (MalformedURLException e) {
            throw new InternalError(e.getMessage());
        }
    }

    @Test(timeout = 60000)
    public void testSingleLeader() throws Exception {
        Map<HostAndPort, MiniYBDaemon> masters = miniYugabytedCluster.getYugabytedNodes();
        HostAndPort leaderMasterHostAndPort = syncClient.getLeaderMasterHostAndPort();

        LOG.info("Leader host and port: " + leaderMasterHostAndPort.toString());
        for (HostAndPort host_and_port : masters.keySet()) {
            String RAFT_role = isMasterLeader(host_and_port.getHost(),
                    masters.get(host_and_port).getWebPort());
            if (RAFT_role == "LEADER") {
                assertEquals(host_and_port.toString(), leaderMasterHostAndPort.toString());
            }

            if (RAFT_role == "FOLLOWER") {
                assertNotEquals(host_and_port.toString(), leaderMasterHostAndPort.toString());
            }
        }
    }

}
