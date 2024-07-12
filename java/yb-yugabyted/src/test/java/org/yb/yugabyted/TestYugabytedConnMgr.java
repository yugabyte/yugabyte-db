package org.yb.yugabyted;

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
public class TestYugabytedConnMgr extends BaseYbdClientTest {

    private static final Logger LOG = LoggerFactory.getLogger(TestYugabytedConnMgr.class);

    public TestYugabytedConnMgr() {
        clusterParameters = new MiniYugabytedClusterParameters.Builder()
                                .numNodes(1)
                                .build();

        clusterConfigurations = new ArrayList<>();
        Map<String, String> tserverFlags = new HashMap<>();
        tserverFlags.put("enable_ysql_conn_mgr", "true");
        tserverFlags.put("allowed_preview_flags_csv", "{enable_ysql_conn_mgr}");

        for (int i = 0; i < clusterParameters.numNodes; i++) {
            MiniYugabytedNodeConfigurations nodeConfigurations =
                new MiniYugabytedNodeConfigurations.Builder()
                    .tserverFlags(tserverFlags)
                    .build();
            clusterConfigurations.add(nodeConfigurations);
        }
    }

    @Test(timeout = 180000)
    public void testConnMgr() throws Exception {
        Map<HostAndPort, MiniYBDaemon> masters = miniYugabytedCluster.getYugabytedNodes();
        String host = "";
        int tserverWebPort = 0;
        for (HostAndPort hostAndPort : masters.keySet()) {
            host = hostAndPort.getHost();
        }
        tserverWebPort = miniYugabytedCluster.getTserverWebPort();
        LOG.info("Tserver Web Port: " + tserverWebPort);

        String jsonResponse = YugabytedTestUtils.getVarz(host, tserverWebPort);
        LOG.info("Varz JSON response: " + jsonResponse);

        JSONObject jsonObject = new JSONObject(jsonResponse);
        JSONArray flags = jsonObject.getJSONArray("flags");

        boolean allowedPreviewFlagsCsvFound = false;
        boolean enableYsqlConnMgrFound = false;

        for (int i = 0; i < flags.length(); i++) {
            JSONObject flag = flags.getJSONObject(i);
            if (flag.getString("name").equals("allowed_preview_flags_csv") &&
                flag.getString("value").equals("enable_ysql_conn_mgr") &&
                flag.getString("type").equals("Custom")) {
                allowedPreviewFlagsCsvFound = true;
            }
            if (flag.getString("name").equals("enable_ysql_conn_mgr") &&
                flag.getString("value").equals("true") &&
                flag.getString("type").equals("Custom")) {
                enableYsqlConnMgrFound = true;
            }
        }

        assertTrue("allowed_preview_flags_csv flag should be found",
                                                    allowedPreviewFlagsCsvFound);
        assertTrue("enable_ysql_conn_mgr flag should be found",
                                                    enableYsqlConnMgrFound);
    }
}
