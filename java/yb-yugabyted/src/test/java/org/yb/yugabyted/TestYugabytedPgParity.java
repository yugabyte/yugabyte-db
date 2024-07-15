package org.yb.yugabyted;

import static org.yb.AssertionWrappers.assertTrue;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Map;
import java.util.Scanner;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;
import org.yb.minicluster.MiniYBDaemon;
import org.yb.minicluster.MiniYugabytedClusterParameters;
import org.yb.minicluster.MiniYugabytedNodeConfigurations;
import org.json.JSONObject;
import org.json.JSONArray;
import org.yb.minicluster.YugabytedCommands;
import org.yb.minicluster.YugabytedTestUtils;

import com.google.common.net.HostAndPort;

@RunWith(value = YBTestRunner.class)
public class TestYugabytedPgParity extends BaseYbdClientTest {

    private static final Logger LOG = LoggerFactory.getLogger(TestYugabytedPgParity.class);

    public TestYugabytedPgParity() {
        clusterParameters = new MiniYugabytedClusterParameters.Builder()
                                .numNodes(1)
                                .build();

        clusterConfigurations = new ArrayList<>();
        Map<String, String> yugabytedFlags = new HashMap<>();
        yugabytedFlags.put("enable_pg_parity_tech_preview", "");

        for (int i = 0; i < clusterParameters.numNodes; i++) {
            MiniYugabytedNodeConfigurations nodeConfigurations =
                                new MiniYugabytedNodeConfigurations.Builder()
                .yugabytedFlags(yugabytedFlags)
                .build();
            clusterConfigurations.add(nodeConfigurations);
        }
    }

    private boolean connectToYsqlAndCheckIsolationLevel(String baseDir) throws Exception {
        String command = YugabytedCommands.getYsqlConnectionCmd(baseDir);
        LOG.info("Running command: " + command);
        ProcessBuilder procBuilder = new ProcessBuilder("/bin/sh", "-c", command);
        procBuilder.redirectErrorStream(true);

        try {
            Process process = procBuilder.start();
            try (OutputStreamWriter writer = new OutputStreamWriter(process.getOutputStream());
                 BufferedReader reader =
                        new BufferedReader(new InputStreamReader(process.getInputStream()))) {

                writer.write("SELECT yb_get_effective_transaction_isolation_level();\n");
                writer.write("\\q\n");
                writer.flush();

                String line;
                while ((line = reader.readLine()) != null) {
                    LOG.info("YSQL output: " + line);
                    if (line.trim().equals("read committed")) {
                        return true;
                    }
                }
            } catch (IOException e) {
                LOG.error("IOException occurred while reading YSQL output: " + e.getMessage());
                e.printStackTrace();
                return false;
            }

            int exitCode = process.waitFor();
            if (exitCode != 0) {
                LOG.error("YSQL process exited with non-zero exit code: " + exitCode);
                return false;
            }
        } catch (IOException e) {
            LOG.error("IOException occurred while trying to connect to YSQL: " + e.getMessage());
            e.printStackTrace();
            return false;
        } catch (InterruptedException e) {
            LOG.error("InterruptedException occurred: " + e.getMessage());
            e.printStackTrace();
            return false;
        }

        LOG.info("YSQL isolation level 'read committed' not found.");
        return false;
    }

    @Test(timeout = 180000)
    public void testPgParity() throws Exception {
        String baseDir = clusterConfigurations.get(0).baseDir;
        Map<HostAndPort, MiniYBDaemon> masters = miniYugabytedCluster.getYugabytedNodes();
        String host = "";
        int tserverWebPort = 0;
        for (HostAndPort hostAndPort : masters.keySet()) {
            host = hostAndPort.getHost();
        }
        tserverWebPort = miniYugabytedCluster.getTserverWebPort();
        LOG.info("Tserver Web Port: " + tserverWebPort);
        boolean isolationLevelChecked = connectToYsqlAndCheckIsolationLevel(baseDir);
        LOG.info("Isolation level checked: " + isolationLevelChecked);
        assertTrue("YSQL isolation level checked should return 'read committed'",
                                                                isolationLevelChecked);
        String jsonResponse = YugabytedTestUtils.getVarz(host, tserverWebPort);
        LOG.info("Varz JSON response: " + jsonResponse);

        JSONObject jsonObject = new JSONObject(jsonResponse);
        JSONArray flags = jsonObject.getJSONArray("flags");

        boolean ysqlPgConfCsvFound = false;
        String expectedValue = "yb_enable_base_scans_cost_model=true," +
            "yb_enable_optimizer_statistics=true,yb_bnl_batch_size=1024," +
            "yb_fetch_row_limit=0,yb_fetch_size_limit=1MB,yb_use_hash_splitting_by_default=false";

        for (int i = 0; i < flags.length(); i++) {
            JSONObject flag = flags.getJSONObject(i);
            if (flag.getString("name").equals("ysql_pg_conf_csv")) {
                ysqlPgConfCsvFound = true;
                String value = flag.getString("value");
                LOG.info("ysql_pg_conf_csv value: " + value);
                assertTrue("ysql_pg_conf_csv value should match expected value",
                           value.equals(expectedValue));
                break;
            }
        }

        assertTrue("ysql_pg_conf_csv flag should be found", ysqlPgConfCsvFound);
    }
}
