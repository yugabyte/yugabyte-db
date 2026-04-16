package org.yb.yugabyted;

import static org.yb.AssertionWrappers.assertEquals;
import java.io.BufferedReader;
import java.io.InputStreamReader;
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
public class TestYugabytedPITR extends BaseYbdClientTest {


    private static final Logger LOG = LoggerFactory.getLogger(TestYugabytedPITR.class);

    public TestYugabytedPITR() {
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

    public static String getEarliestRecoverableTime(String baseDirPath) throws Exception {
        String command = YugabytedCommands.getPITRStatusCmd(baseDirPath);
        int maxRetries = 3;
        int retryIntervalMillis = 5000;

        for (int attempt = 1; attempt <= maxRetries; attempt++) {
            LOG.info("Running command (attempt " + attempt + "): " + command);
            ProcessBuilder procBuilder = new ProcessBuilder(command.split(" "));
            procBuilder.redirectErrorStream(true);
            Process proc = procBuilder.start();
            int exitCode = proc.waitFor();
            if (exitCode != 0) {
                LOG.error("PITR status command failed with exit code: " + exitCode);
                if (attempt == maxRetries) {
                    throw new RuntimeException(
                        "PITR status command failed after " + maxRetries + " attempts"
                    );
                } else {
                    LOG.info(
                        "Retrying in " + (retryIntervalMillis / 1000) + " seconds..."
                    );
                    Thread.sleep(retryIntervalMillis);
                    continue;
                }
            }

            try (BufferedReader reader = new BufferedReader(
                    new InputStreamReader(proc.getInputStream()))) {
                String line;
                while ((line = reader.readLine()) != null) {
                    LOG.info("PITR status output: " + line);
                    if (line.contains("Earliest Recoverable Time:")) {
                        String recoverableTime = line.split(
                            "Earliest Recoverable Time:"
                        )[1].trim();
                        // Remove trailing '|' character if present
                        if (recoverableTime.endsWith("|")) {
                            recoverableTime = recoverableTime.substring(
                                0, recoverableTime.length() - 1
                            ).trim();
                        }
                        return recoverableTime;
                    }
                }
            }
            if (attempt < maxRetries) {
                LOG.info(
                    "Earliest Recoverable Time not found, retrying in " +
                    (retryIntervalMillis / 1000) + " seconds..."
                );
                Thread.sleep(retryIntervalMillis);
            }
        }
        throw new RuntimeException(
            "Earliest Recoverable Time not found in PITR status output after " +
            maxRetries + " attempts"
        );
    }



    @Test(timeout = 300000)
    public void testYugabytedPITR() throws Exception {
        String baseDir = clusterConfigurations.get(0).baseDir;

        //Enable PITR
        boolean expectedPITRStatus = YugabytedCommands.enablePITR(baseDir);
        LOG.info("Expected PITR Status: " + expectedPITRStatus);
        assertEquals("Failed to enable PITR", true, expectedPITRStatus);

        //Get the earliest recoverable time
        String earliestRecoverableTime = getEarliestRecoverableTime(baseDir);
        LOG.info("Earliest Recoverable Time: " + earliestRecoverableTime);

        //Restore to the earliest recoverable time
        String recoveryPoint =
                YugabytedCommands.restoreToPointInTime(baseDir, earliestRecoverableTime);
        LOG.info("Recovery Point: " + recoveryPoint);
        assertEquals("Recovery point should match the earliest recoverable time",
                                                        earliestRecoverableTime, recoveryPoint);
    }

}
