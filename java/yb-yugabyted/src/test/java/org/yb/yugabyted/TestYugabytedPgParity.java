package org.yb.yugabyted;

import static org.yb.AssertionWrappers.assertTrue;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Map;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;
import org.yb.minicluster.MiniYugabytedClusterParameters;
import org.yb.minicluster.MiniYugabytedNodeConfigurations;
import org.yb.minicluster.YugabytedCommands;

@RunWith(value = YBTestRunner.class)
public class TestYugabytedPgParity extends BaseYbdClientTest {

    private static final Logger LOG = LoggerFactory.getLogger(TestYugabytedPgParity.class);

    public TestYugabytedPgParity() {
        clusterParameters = new MiniYugabytedClusterParameters.Builder()
                                .numNodes(1)
                                .build();

        clusterConfigurations = new ArrayList<>();
        Map<String, String> yugabytedFlags = new HashMap<>();
        // yugabytedFlags.put("enable_pg_parity_early_access", "");

        for (int i = 0; i < clusterParameters.numNodes; i++) {
            MiniYugabytedNodeConfigurations nodeConfigurations =
                                new MiniYugabytedNodeConfigurations.Builder()
                .yugabytedFlags(yugabytedFlags)
                .build();
            clusterConfigurations.add(nodeConfigurations);
        }
    }

    private ProcessBuilder getYsqlConnectionProcess(String baseDir) throws Exception {

        String command = YugabytedCommands.getYsqlConnectionCmd(baseDir);
        LOG.info("Running command: " + command);
        ProcessBuilder procBuilder = new ProcessBuilder("/bin/sh", "-c", command);
        procBuilder.redirectErrorStream(true);
        return procBuilder;
    }

    private boolean checkIsolationLevel(ProcessBuilder processBuilder) throws Exception {
        try {
            Process process = processBuilder.start();
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

    private boolean checkYbEnableCbo(ProcessBuilder processBuilder) throws Exception {
        try {
            Process process = processBuilder.start();
            try (OutputStreamWriter writer = new OutputStreamWriter(process.getOutputStream());
                 BufferedReader reader =
                        new BufferedReader(new InputStreamReader(process.getInputStream()))) {

                writer.write("SHOW yb_enable_cbo;\n");
                writer.write("\\q\n");
                writer.flush();

                String line;
                while ((line = reader.readLine()) != null) {
                    LOG.info("YSQL output: " + line);
                    if (line.trim().equals("on")) {
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
        LOG.info("YSQL yb_enable_cbo not found.");
        return false;
    }

    @Test(timeout = 180000)
    public void testPgParity() throws Exception {
        String baseDir = clusterConfigurations.get(0).baseDir;
        ProcessBuilder processBuilder = getYsqlConnectionProcess(baseDir);
        boolean isolationLevelChecked = checkIsolationLevel(processBuilder);
        LOG.info("Isolation level checked: " + isolationLevelChecked);
        assertTrue("YSQL isolation level checked should return 'read committed'",
                                                                isolationLevelChecked);
        boolean ybEnableCboChecked = checkYbEnableCbo(processBuilder);
        LOG.info("YB enable CBO checked: " + ybEnableCboChecked);
        assertTrue("YB enable CBO checked should return 'on'", ybEnableCboChecked);
    }
}
