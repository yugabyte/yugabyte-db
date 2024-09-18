package org.yb.minicluster;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class YugabytedCommands {

    private static final Logger LOG = LoggerFactory.getLogger(YugabytedCommands.class);

    public static String getYsqlConnectionCmd(String baseDir) {
        String binDir = YugabytedTestUtils.getYugabytedBinDir();
        return binDir + "/yugabyted connect ysql --base_dir " + baseDir;
    }

    public static String getYcqlConnectionCmd(String baseDir) {
        String binDir = YugabytedTestUtils.getYugabytedBinDir();
        return binDir + "/yugabyted connect ycql --base_dir " + baseDir;
    }

    public static String getEnableEncryptionAtRestCmd(String baseDir) {
        String binDir = YugabytedTestUtils.getYugabytedBinDir();
        return binDir + "/yugabyted configure encrypt_at_rest --enable --base_dir " + baseDir;
    }

    public static boolean enableEncryptionAtRest(String baseDirPath) throws Exception {
        String command = getEnableEncryptionAtRestCmd(baseDirPath);
        LOG.info("Running command: " + command);
        ProcessBuilder procBuilder = new ProcessBuilder(command.split(" "));
        procBuilder.redirectErrorStream(true);
        Process proc = procBuilder.start();
        int exitCode = proc.waitFor();
        if (exitCode != 0) {
            LOG.error("Enable encryption command failed with exit code: " + exitCode);
            return false;
        }
        return true;
    }

    public static String getStopCmd(String baseDir) {
        String binDir = YugabytedTestUtils.getYugabytedBinDir();
        return binDir + "/yugabyted stop --base_dir " + baseDir;
    }

    public static boolean stop(String baseDir) throws Exception {
        String command = YugabytedCommands.getStopCmd(baseDir);
        LOG.info("Running command: " + command);
        ProcessBuilder procBuilder = new ProcessBuilder(command.split(" "));
        procBuilder.redirectErrorStream(true);
        Process proc = procBuilder.start();
        int exitCode = proc.waitFor();
        if (exitCode != 0) {
            LOG.error("Yugabyted stop command failed with exit code: " + exitCode);
            return false;
        }
        return true;
    }

    public static String getDestroyCmd(String baseDir) {
        String binDir = YugabytedTestUtils.getYugabytedBinDir();
        return binDir + "/yugabyted destroy --base_dir " + baseDir;
    }

    public static boolean destroy(String baseDir) throws Exception {
        String command = YugabytedCommands.getDestroyCmd(baseDir);
        LOG.info("Running command: " + command);
        ProcessBuilder procBuilder = new ProcessBuilder(command.split(" "));
        procBuilder.redirectErrorStream(true);
        Process proc = procBuilder.start();
        int exitCode = proc.waitFor();
        if (exitCode != 0) {
            LOG.error("Yugabyted destroy command failed with exit code: " + exitCode);
            return false;
        }
        return true;
    }

    public static String getCollectLogsCmd(String baseDir) {
        String binDir = YugabytedTestUtils.getYugabytedBinDir();
        return binDir + "/yugabyted collect_logs --base_dir " + baseDir;
    }

    public static boolean collectLogs(String baseDir) throws Exception {
        String command = getCollectLogsCmd(baseDir);
        LOG.info("Running collect logs command: " + command);
        ProcessBuilder procBuilder = new ProcessBuilder("/bin/sh", "-c", command);
        procBuilder.redirectErrorStream(true);

        try {
            Process proc = procBuilder.start();
            try (BufferedReader reader = new BufferedReader(new
                                    InputStreamReader(proc.getInputStream()))) {
                String line;
                while ((line = reader.readLine()) != null) {
                    LOG.info("Collect logs command output: " + line);
                    if (line.contains("Status") && line.contains("Logs collected successfully.")) {
                        return true;
                    }
                }
            }
            int exitCode = proc.waitFor();
            if (exitCode != 0) {
                LOG.error("Collect logs command failed with exit code: " + exitCode);
                return false;
            }
            LOG.info("Collect logs command completed successfully");
        } catch (Exception e) {
            LOG.error("Exception while running collect logs command: ", e);
            return false;
        }

        return false;
    }

    public static String getPreferredLeaderCmd(String baseDirPath) {
        String binDir = YugabytedTestUtils.getYugabytedBinDir();
        return binDir + "/yugabyted configure data_placement " +
        "--constraint_value=aws.us-west-1.us-west-1a:3," +
        "aws.us-west-1.us-west-1c:2,aws.us-west-1.us-west-1b:1" +
        " --rf 3 --base_dir=" + baseDirPath;
    }

    public static boolean configureDataPlacement(String baseDirPath) throws Exception {
        String command = getPreferredLeaderCmd(baseDirPath);
        LOG.info("Running command: " + command);
        ProcessBuilder procBuilder = new ProcessBuilder("/bin/sh", "-c", command);
        procBuilder.redirectErrorStream(true);
        Process proc = procBuilder.start();
        int exitCode = proc.waitFor();
        if (exitCode != 0) {
            LOG.error("Configure data placement command failed with exit code: " + exitCode);
            return false;
        }
        return true;
    }

    public static String getDemoConnectCmd(String baseDir) {
        String binDir = YugabytedTestUtils.getYugabytedBinDir();
        return binDir + "/yugabyted demo connect --base_dir " + baseDir;
    }

    public static Process startDemoConnect(String baseDir) throws IOException {
        String command = YugabytedCommands.getDemoConnectCmd(baseDir);
        LOG.info("Running command: " + command);
        ProcessBuilder procBuilder = new ProcessBuilder("/bin/sh", "-c", command);
        procBuilder.redirectErrorStream(true);
        return procBuilder.start();
    }

    public static String getEnablePITRCmd(String baseDir) {
        String binDir = YugabytedTestUtils.getYugabytedBinDir();
        return binDir + "/yugabyted configure point_in_time_recovery " +
        "--enable --database yugabyte --base_dir " + baseDir;
    }

    public static boolean enablePITR(String baseDirPath) throws Exception {
        String command = YugabytedCommands.getEnablePITRCmd(baseDirPath);
        LOG.info("Running command: " + command);
        ProcessBuilder procBuilder = new ProcessBuilder(command.split(" "));
        procBuilder.redirectErrorStream(true);
        Process proc = procBuilder.start();
        int exitCode = proc.waitFor();
        if (exitCode != 0) {
            LOG.error("Enable PITR command failed with exit code: " + exitCode);
            return false;
        }
        return true;
    }

    public static String getPITRStatusCmd(String baseDir) {
        String binDir = YugabytedTestUtils.getYugabytedBinDir();
        return binDir + "/yugabyted configure point_in_time_recovery " +
        "--status --database yugabyte --base_dir " + baseDir;
    }

    public static String getRestoreCmd(String baseDirPath, String recoverableTime) {
        String binDir = YugabytedTestUtils.getYugabytedBinDir();
        return binDir + "/yugabyted restore --recover_to_point_in_time \"" + recoverableTime
        + "\" " + "--database yugabyte --base_dir " + baseDirPath;
    }

    public static String restoreToPointInTime(
        String baseDirPath, String recoverableTime) throws Exception {
        String command = getRestoreCmd(baseDirPath, recoverableTime);
        LOG.info("Running restore command: " + command);
        ProcessBuilder procBuilder = new ProcessBuilder("/bin/sh", "-c", command);
        procBuilder.redirectErrorStream(true);
        StringBuilder output = new StringBuilder();
        String recoveryPoint = null;

        try {
            Process proc = procBuilder.start();
            try (BufferedReader reader = new BufferedReader(
                    new InputStreamReader(proc.getInputStream()))) {
                String line;
                while ((line = reader.readLine()) != null) {
                    LOG.info("Restore command output: " + line);
                    output.append(line).append("\n");
                    if (line.contains("Recovery Point:")) {
                        recoveryPoint = line.split("Recovery Point:")[1].trim();
                        if (recoveryPoint.endsWith("|")) {
                            recoveryPoint = recoveryPoint.substring(
                                0, recoveryPoint.length() - 1
                            ).trim();
                        }
                    }
                }
            }
            int exitCode = proc.waitFor();
            if (exitCode != 0) {
                LOG.error("Restore command failed with exit code: " + exitCode);
                return null;
            }
            LOG.info("Restore command completed successfully");
        } catch (Exception e) {
            LOG.error("Exception while running restore command: ", e);
            return null;
        }
        return recoveryPoint;
    }
}
