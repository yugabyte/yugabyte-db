package org.yb.minicluster;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.net.HttpURLConnection;
import java.net.MalformedURLException;
import java.net.URL;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Scanner;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.stream.Collectors;

import org.json.JSONArray;
import org.json.JSONObject;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.TestUtils;

import com.google.common.collect.Lists;

public class YugabytedTestUtils {

    private static final Logger LOG = LoggerFactory.getLogger(YugabytedTestUtils.class);

    private static final Object flagFilePathLock = new Object();

    private static Path flagFileTmpPath = null;

    private static final String BIN_DIR_PROP = "binDir";

    private static volatile String yugabytedBinariesDir = null;

    /**
     * @return the path of the flags file to pass to yugabyted processes
     *         started by the tests
     */
    public static String getFlagsPath() {
        try {
            synchronized (flagFilePathLock) {
                if (flagFileTmpPath == null || !Files.exists(flagFileTmpPath)) {
                    flagFileTmpPath = Files.createTempFile(
                            Paths.get(TestUtils.getBaseTmpDir()), "ybd-flags", ".conf");
                }
            }
            return flagFileTmpPath.toAbsolutePath().toString();
        } catch (IOException e) {
            throw new RuntimeException("Unable to extract flags file into tmp", e);
        }
    }

    /**
     * @param binName
     *            the binary to look for yugabyted
     * @return the absolute path of that binary
     * @throws FileNotFoundException
     *             if no such binary is found
     */
    public static String findYugabytedBinary(String binName) throws FileNotFoundException {
        String binDir = getYugabytedBinDir();

        File candidate = new File(binDir, binName);
        if (candidate.canExecute()) {
            return candidate.getAbsolutePath();
        }
        throw new FileNotFoundException("Cannot find binary " + binName +
                " in binary directory " + binDir);
    }

    public static String getYugabytedBinDir() {
        if (yugabytedBinariesDir != null)
            return yugabytedBinariesDir;

        // String binDir = System.getProperty(BIN_DIR_PROP);
        // if (binDir != null) {
        // LOG.info("Using binary directory specified by property: {}",
        // binDir);
        // } else {
        // binDir = TestUtils.findYbRootDir() + "/bin";
        // }
        String binDir = TestUtils.findYbRootDir() + "/bin";

        yugabytedBinariesDir = binDir;
        return binDir;
    }

    private static String findPgIsReadyPath() throws Exception {
        String binDir = findYugabytedBinary("yugabyted");
        File binDirFile = new File(binDir);
        File yugabyteDir = binDirFile.getParentFile().getParentFile();
        LOG.info("Looking for pg_isready in Yugabyte directory: {}", yugabyteDir.getAbsolutePath());
        return findPgIsReadyInYugabyteDir(yugabyteDir.getAbsolutePath());
    }

    private static String findPgIsReadyInYugabyteDir(String rootDir) throws Exception {
        LOG.info("Searching for pg_isready in root directory: {}", rootDir);

        List<String> directoriesWithPgIsReady = new ArrayList<>();

        Files.walk(Paths.get(rootDir))
             .filter(Files::isRegularFile)
             .forEach(filePath -> {
                 if (filePath.getFileName().toString().equals("pg_isready")) {
                     String parentDir = filePath.getParent().toString();
                     LOG.info("Found pg_isready in directory: {}", parentDir);
                     if (parentDir.contains("postgres/bin")) {
                         directoriesWithPgIsReady.add(parentDir);
                     }
                 }
             });

        if (!directoriesWithPgIsReady.isEmpty()) {
            // Return the first match that contains "postgres/bin"
            return Paths.get(directoriesWithPgIsReady.get(0), "pg_isready").toString();
        }

        throw new FileNotFoundException(
                    "pg_isready executable not found in any of the provided directories");
    }

    public static List<String> flagsToArgs(Map<String, String> flags) {
        return flags.entrySet().stream()
            .map(e -> e.getValue().isEmpty() ?
                "--" + e.getKey() : "--" + e.getKey() + "=" + e.getValue())
            .collect(Collectors.toList());
    }

    public static boolean testYsqlConnection(String baseDir,
                                                    String host) throws Exception {
        String command = YugabytedCommands.getYsqlConnectionCmd(baseDir);
        LOG.info("Running command: " + command);
        ProcessBuilder procBuilder = new ProcessBuilder("/bin/sh", "-c", command);
        procBuilder.redirectErrorStream(true);

        try {
            Process process = procBuilder.start();
            try (OutputStreamWriter writer =
                        new OutputStreamWriter(process.getOutputStream());
                BufferedReader reader =
                        new BufferedReader(new InputStreamReader(process.getInputStream()))) {

                writer.write("SELECT host FROM yb_servers();\n");
                writer.write("\\q\n");
                writer.flush();

                String line;
                while ((line = reader.readLine()) != null) {
                    LOG.info("YSQL output: " + line);
                    if (line.trim().equals(host)) {
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
        return false;
    }

    public static boolean testYcqlConnection(String baseDir,
                                                    String host) throws Exception {
        String command = YugabytedCommands.getYcqlConnectionCmd(baseDir);
        LOG.info("Running command: " + command);
        ProcessBuilder procBuilder = new ProcessBuilder("/bin/sh", "-c", command);
        procBuilder.redirectErrorStream(true);

        try {
            Process process = procBuilder.start();
            try (OutputStreamWriter writer =
                        new OutputStreamWriter(process.getOutputStream());
                BufferedReader reader =
                        new BufferedReader(new InputStreamReader(process.getInputStream()))) {

                writer.write("SELECT rpc_address FROM system.local;\n");
                writer.write("\\q\n");
                writer.flush();

                String line;
                while ((line = reader.readLine()) != null) {
                    LOG.info("YCQL output: " + line);
                    if (line.trim().equals(host)) {
                        return true;
                    }
                }
            } catch (IOException e) {
                LOG.error("IOException occurred while reading YCQL output: " +
                                                                    e.getMessage());
                e.printStackTrace();
                return false;
            }

            int exitCode = process.waitFor();
            if (exitCode != 0) {
                LOG.error("YSQL process exited with non-zero exit code: " + exitCode);
                return false;
            }
        } catch (IOException e) {
            LOG.error("IOException occurred while trying to connect to YCQL: " +
                                                                        e.getMessage());
            e.printStackTrace();
            return false;
        } catch (InterruptedException e) {
            LOG.error("InterruptedException occurred: " + e.getMessage());
            e.printStackTrace();
            return false;
        }
        return false;
    }

    public static List<String> getYugabytedStatus(String baseDirPath) throws Exception {
        String binDir = YugabytedTestUtils.findYugabytedBinary("yugabyted");
        LOG.info("bin directory", binDir);
        return Lists.newArrayList(
            YugabytedTestUtils.findYugabytedBinary("yugabyted"),
            "status",
            "--base_dir=" + baseDirPath
        );
    }

    public static void waitForNodeToStart(String baseDir, String yugabytedAdvertiseAddress,
        Integer ysqlPort, long timeoutMillis, long checkIntervalMillis) throws Exception {

        long startTime = System.currentTimeMillis();
        while (true) {
            if (checkNodeStatus(baseDir, "Running")) {
                if (isPostgresReady(yugabytedAdvertiseAddress, ysqlPort)) {
                    LOG.info("Node and PostgreSQL are both started and ready.");
                    break;
                } else {
                    LOG.info("Node is started, but PostgreSQL is not yet ready.");
                }
            } else {
                LOG.info("Node not yet started");
            }
            if (System.currentTimeMillis() - startTime > timeoutMillis) {
                throw new Exception(
                    "Node did not start within " + (timeoutMillis / 1000) + " seconds");
            }
            Thread.sleep(checkIntervalMillis);
        }
    }

    public static boolean checkNodeStatus(String baseDirPath, String status) throws Exception {

        List<String> statusCmd = YugabytedTestUtils.getYugabytedStatus(baseDirPath);
        LOG.info("Yugabyted status cmd: " + statusCmd);
        ProcessBuilder procBuilder =
                new ProcessBuilder(statusCmd).redirectErrorStream(true);
        Process proc = procBuilder.start();

        try (BufferedReader reader =
                    new BufferedReader(new InputStreamReader(proc.getInputStream()))) {
            String line;
            while ((line = reader.readLine()) != null) {
                LOG.info("Yugabyted lines: " + line);
                if (line.contains("Status") && line.contains(status)) {
                    return true;
                }
            }
        }
        int exitCode = proc.waitFor();
        if (exitCode != 0) {
            LOG.error("Status command failed with exit code: " + exitCode);
        }

        return false;
    }

    public static boolean isPostgresReady(String yugabytedAdvertiseAddress, Integer ysqlPort)
                                                                            throws Exception {
        String pgIsReadyPath = findPgIsReadyPath();
        LOG.info("Found pg_isready at: {}", pgIsReadyPath);

        ProcessBuilder processBuilder =
            new ProcessBuilder(pgIsReadyPath, "-h", yugabytedAdvertiseAddress,
                                                                "-p", ysqlPort.toString());
        processBuilder.redirectErrorStream(true);

        for (int i = 0; i < 4; i++) {
            LOG.info("Checking PostgreSQL status. Attempt: {}", i + 1);

            Process process = processBuilder.start();
            process.waitFor(5, TimeUnit.SECONDS);

            try (BufferedReader reader =
                        new BufferedReader(new InputStreamReader(process.getInputStream()))) {
                String line;
                while ((line = reader.readLine()) != null) {
                    LOG.info("pg_isready output: {}", line);
                    if (line.trim().contains("accepting connections")) {
                        return true;
                    }
                }
            }

            LOG.info("PostgreSQL not ready yet, waiting for 5 seconds before the next check.");
            Thread.sleep(5000);  // Wait for 5 seconds before the next check
        }

        LOG.info("PostgreSQL is not ready after 3 attempts.");
        return false;
    }

    public static String getClusterConfig(String host, int port) throws Exception {
        try {
            String urlstr = String.format("http://%s:%d/api/v1/cluster-config", host, port);
            LOG.info("url: " + urlstr);

            URL url = new URL(urlstr);
            HttpURLConnection huc = (HttpURLConnection) url.openConnection();
            huc.setRequestMethod("GET");
            huc.setConnectTimeout(50000);
            huc.setReadTimeout(50000);
            huc.connect();
            int statusCode = huc.getResponseCode();
            if (statusCode != 200) {
                throw new RuntimeException(
                    "Failed to get cluster config. HTTP error code: " + statusCode);
            }

            Scanner scanner = new Scanner(url.openStream());
            StringBuilder jsonResponse = new StringBuilder();
            while (scanner.hasNext()) {
                jsonResponse.append(scanner.nextLine());
            }
            scanner.close();
            return jsonResponse.toString();

        } catch (MalformedURLException e) {
            throw new InternalError(e.getMessage());
        }
    }

    public static String getVarz(String host, int port) throws Exception {
        try {
            String urlstr = String.format("http://%s:%d/api/v1/varz", host, port);
            LOG.info("url: " + urlstr);

            URL url = new URL(urlstr);
            HttpURLConnection huc = (HttpURLConnection) url.openConnection();
            huc.setRequestMethod("GET");
            huc.setConnectTimeout(50000);
            huc.setReadTimeout(50000);
            huc.connect();
            int statusCode = huc.getResponseCode();
            if (statusCode != 200) {
                throw new RuntimeException("Failed to get varz. HTTP error code: " + statusCode);
            }

            Scanner scanner = new Scanner(url.openStream());
            StringBuilder jsonResponse = new StringBuilder();
            while (scanner.hasNext()) {
                jsonResponse.append(scanner.nextLine());
            }
            scanner.close();
            return jsonResponse.toString();

        } catch (MalformedURLException e) {
            throw new InternalError(e.getMessage());
        }
    }

    public static boolean isDatabaseCreated(String host, int webPort) {
        try {
            String urlstr = String.format("http://%s:%d/api/v1/namespaces", host, webPort);
            LOG.info("Checking namespaces at URL: " + urlstr);

            URL url = new URL(urlstr);
            HttpURLConnection huc = (HttpURLConnection) url.openConnection();
            huc.setRequestMethod("GET");
            huc.setConnectTimeout(5000);
            huc.setReadTimeout(5000);
            huc.connect();

            int statusCode = huc.getResponseCode();
            if (statusCode != 200) {
                LOG.error("Failed to get namespaces. HTTP error code: " + statusCode);
                return false;
            }

            Scanner scanner = new Scanner(url.openStream());
            StringBuilder jsonResponse = new StringBuilder();
            while (scanner.hasNext()) {
                jsonResponse.append(scanner.nextLine());
            }
            scanner.close();

            LOG.info("Namespaces API response: " + jsonResponse.toString());

            JSONObject jsonObject = new JSONObject(jsonResponse.toString());
            JSONArray userNamespaces = jsonObject.getJSONArray("User Namespaces");

            for (int i = 0; i < userNamespaces.length(); i++) {
                JSONObject namespace = userNamespaces.getJSONObject(i);
                String namespaceName = namespace.getString("name");
                String namespaceState = namespace.getString("state");
                LOG.info("Namespace found: name=" + namespaceName + ", state=" + namespaceState);
                if ("yb_demo_northwind".equals(namespaceName) &&
                    "RUNNING".equals(namespaceState)) {
                    LOG.info("Database 'yb_demo_northwind' is in 'RUNNING' state.");
                    return true;
                }
            }
        } catch (Exception e) {
            LOG.error("Exception while checking namespaces: ", e);
        }
        return false;
    }


    public static boolean isDirectoryRemoved(String Dir) {
        File DirFile = new File(Dir);
        boolean isDirRemoved = !DirFile.exists();
        LOG.info("Directory removal status: " + isDirRemoved);
        return isDirRemoved;
    }

}
