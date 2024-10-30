// package org.yb.yugabyted;

// import static org.yb.AssertionWrappers.assertTrue;

// import java.io.BufferedReader;
// import java.io.File;
// import java.io.IOException;
// import java.io.InputStreamReader;
// import java.io.OutputStreamWriter;
// import java.nio.file.Files;
// import java.nio.file.Path;
// import java.nio.file.Paths;
// import java.util.ArrayList;
// import java.util.HashMap;
// import java.util.Map;
// import java.util.concurrent.TimeUnit;

// import org.junit.Test;
// import org.junit.runner.RunWith;
// import org.slf4j.Logger;
// import org.slf4j.LoggerFactory;
// import org.yb.YBTestRunner;
// import org.yb.minicluster.MiniYugabytedCluster;
// import org.yb.minicluster.MiniYugabytedClusterParameters;
// import org.yb.minicluster.MiniYugabytedNodeConfigurations;
// import org.yb.minicluster.YugabytedTestUtils;
// import org.yb.util.Pair;

// @RunWith(value = YBTestRunner.class)
// public class TestYugabytedBackupRestore extends BaseYbdClientTest {

//     private static final Logger LOG = LoggerFactory.getLogger(TestYugabytedBackupRestore.class);

//     public TestYugabytedBackupRestore() {
//         clusterParameters = new MiniYugabytedClusterParameters.Builder()
//                                 .numNodes(1)
//                                 .build();

//         clusterConfigurations = new ArrayList<>();
//         for (int i = 0; i < clusterParameters.numNodes; i++) {

//             Map<String, String> flags = new HashMap<>();
//             flags.put("backup_daemon", "true");

//             MiniYugabytedNodeConfigurations nodeConfigurations =
//                               new MiniYugabytedNodeConfigurations.Builder()
//                 .yugabytedFlags(flags)
//                 .build();

//             clusterConfigurations.add(nodeConfigurations);
//         }
//     }

//     public static boolean createTestDatabase(String baseDir, String dbName) throws Exception {
//         String command = YugabytedTestUtils.getYsqlConnectionCmd(baseDir);
//         LOG.info("Running command: " + command);
//         ProcessBuilder processBuilder = new ProcessBuilder("/bin/sh", "-c", command);
//         processBuilder.redirectErrorStream(true);

//         Process process = processBuilder.start();
//         try (OutputStreamWriter writer = new OutputStreamWriter(process.getOutputStream());
//              BufferedReader reader =
//                     new BufferedReader(new InputStreamReader(process.getInputStream()))) {

//             writer.write("CREATE DATABASE " + dbName + ";\n");
//             writer.write("\\q\n");
//             writer.flush();

//             String line;
//             while ((line = reader.readLine()) != null) {
//                 LOG.info("YSQL output: " + line);
//                 if (line.trim().equals("CREATE DATABASE")) {
//                     return true;
//                 }
//             }
//         }
//         return process.waitFor() == 0;
//     }

//     // Function to check if the database is created
//     public static boolean isDatabaseCreated(String baseDir, String dbName) throws Exception {
//         String command = YugabytedTestUtils.getYsqlConnectionCmd(baseDir);
//         LOG.info("Running command: " + command);
//         ProcessBuilder processBuilder = new ProcessBuilder("/bin/sh", "-c", command);
//         processBuilder.redirectErrorStream(true);

//         Process process = processBuilder.start();
//         try (OutputStreamWriter writer = new OutputStreamWriter(process.getOutputStream());
//              BufferedReader reader =
//                      new BufferedReader(new InputStreamReader(process.getInputStream()))) {

//             writer.write(
//             "SELECT datname FROM pg_catalog.pg_database WHERE datname = '" + dbName +
//              "';\n" );
//             writer.write("\\q\n");
//             writer.flush();

//             String line;
//             while ((line = reader.readLine()) != null) {
//                 LOG.info("YSQL output: " + line);
//                 if (line.trim().equals(dbName)) {
//                     return true;
//                 }
//             }
//         }
//         return process.waitFor() == 0;
//     }

//     // Function to create necessary directories for backup
//     private Pair<String, String> setupBackupDirectories() throws Exception {
//         Path backupDir = Paths.get("/tmp/nfs/yb.backup");
//         if (!Files.exists(backupDir)) {
//             Files.createDirectories(backupDir);
//         }

//         String timestamp = String.valueOf(System.currentTimeMillis());
//         Path ybBaseDir = backupDir.resolve("yb_" + timestamp);
//         if (!Files.exists(ybBaseDir)) {
//             Files.createDirectories(ybBaseDir);
//         }
//         return new Pair<>(ybBaseDir.toString(), "/yb.backup/yb_" + timestamp);
//     }

//     // Function to run the backup command
//     public static void runBackupCommand(String baseDirPath, String backupDir) throws Exception {
//         String command =
//            YugabytedTestUtils.getYugabytedBinDir() + "/yugabyted backup --cloud_storage_uri " +
//                      "backupDir + " --database yb_demo_northwind --base_dir " + baseDirPath;
//         LOG.info("Running backup command: " + command);
//         ProcessBuilder procBuilder = new ProcessBuilder("/bin/sh", "-c", command);
//         procBuilder.redirectErrorStream(true);
//         Process proc = procBuilder.start();
//         int exitCode = proc.waitFor();
//         if (exitCode != 0) {
//             throw new RuntimeException("Backup command failed with exit code: " + exitCode);
//         }
//     }

//     // Function to check the backup status
//     public static boolean checkBackupStatus(
//                String baseDirPath, String backupDir) throws Exception {
//         String command = YugabytedTestUtils.getYugabytedBinDir() +
//         "/yugabyted backup --cloud_storage_uri " + backupDir + " --database yb_demo_northwind"
//           +  " --status --base_dir " + baseDirPath;
//         LOG.info("Running backup status command: " + command);
//         ProcessBuilder procBuilder = new ProcessBuilder("/bin/sh", "-c", command);
//         procBuilder.redirectErrorStream(true);

//         while (true) {
//             Process proc = procBuilder.start();
//             StringBuilder output = new StringBuilder();
//             try (BufferedReader reader =
//                    new BufferedReader(new InputStreamReader(proc.getInputStream()))) {
//                 String line;
//                 while ((line = reader.readLine()) != null) {
//                     LOG.info("Backup status output: " + line);
//                     output.append(line).append("\n");
//                     if (line.contains("Status") && line.contains("Backup is complete")) {
//                         return true;
//                     }
//                 }
//             }
//             int exitCode = proc.waitFor();
//             if (exitCode != 0) {
//                 throw new RuntimeException(
//                   "Backup status command failed with exit code: " + exitCode);
//             }
//             LOG.info("Backup not completed yet, waiting for 30 seconds...");
//             TimeUnit.SECONDS.sleep(30);
//         }
//     }

//     @Test(timeout = 300000)
//     public void testYugabytedBackupRestore() throws Exception {
//         String baseDir = clusterConfigurations.get(0).baseDir;
//         boolean isCreated = createTestDatabase(baseDir, "test");
//         LOG.info("Test database creation status: " + isCreated);

//         // Check if the database is created
//         isCreated = false;
//         long startTime = System.currentTimeMillis();
//         while (System.currentTimeMillis() - startTime < 120000) { // 120 seconds timeout
//             if (isDatabaseCreated(baseDir, "test")) {
//                 isCreated = true;
//                 break;
//             }
//             LOG.info("Database not created yet, waiting for 30 seconds...");
//             TimeUnit.SECONDS.sleep(30);
//         }

//         LOG.info("Database creation status: " + isCreated);
//         assertTrue("Database 'test' should be created", isCreated);


//         // Step 3: Setup necessary directories for backup
//         Pair<String, String> backupDirs = setupBackupDirectories();
//         String backupDirFullPath = backupDirs.getFirst();
//         String backupDirRelativePath = backupDirs.getSecond();
//         LOG.info("Backup dir full path: " + backupDirFullPath);
//         LOG.info("Backup dir relative path: " + backupDirRelativePath);

//         assertTrue("Database yb_demo_northwind should be created", isCreated);

//         // // Step 4: Run the backup command
//         // String backupDir = "/tmp/nfs/yb.backup/yb_" + baseDir.replaceAll("[^a-zA-Z0-9]", "_");
//         // runBackupCommand(baseDir, backupDir);

//         // // Step 5: Check the backup status until completion
//         // boolean backupComplete = checkBackupStatus(baseDir, backupDir);
//         // assertTrue("Backup should be complete", backupComplete);
//     }
// }
