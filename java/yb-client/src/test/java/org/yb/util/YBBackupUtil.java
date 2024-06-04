// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
package org.yb.util;

import static com.google.common.base.Preconditions.checkArgument;

import com.google.common.net.HostAndPort;
import org.json.JSONObject;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.TestUtils;
import org.yb.minicluster.MiniYBDaemon;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.InputStreamReader;
import java.net.InetSocketAddress;
import java.util.*;
import java.util.concurrent.TimeUnit;
import java.util.stream.Collectors;

public final class YBBackupUtil {
  private static final Logger LOG = LoggerFactory.getLogger(YBBackupUtil.class);
  public static final int defaultYbBackupTimeoutInSeconds = 180;

  // Comma separate describing the master addresses and ports.
  private static String masterAddresses;
  private static String tsWebHostsAndPorts;
  private static InetSocketAddress postgresContactPoint;
  private static boolean verboseMode = false;

  public static void setPostgresContactPoint(InetSocketAddress contactPoint) {
    postgresContactPoint = contactPoint;
  }

  public static void setMasterAddresses(String addresses) {
    masterAddresses = addresses;
  }

  public static void setTSWebAddresses(String hostsAndPorts) {
    tsWebHostsAndPorts = hostsAndPorts;
  }

  public static void setTSAddresses(Map<HostAndPort, MiniYBDaemon> tserversMap) {
    String hostsAndPorts = "";
    for (MiniYBDaemon tserver : tserversMap.values()) {
      hostsAndPorts += (hostsAndPorts.isEmpty() ? "" : ",") + tserver.getWebHostAndPort();
    }
    setTSWebAddresses(hostsAndPorts);
  }

  // Use it to get more detailed log from the backup script for debugging.
  public static void enableVerboseMode() {
    verboseMode = true;
  }

  public static String runProcess(List<String> args, int timeoutSeconds) throws Exception {
    String processStr = "";
    for (String arg: args) {
      processStr += (processStr.isEmpty() ? "" : " ") + arg;
    }
    LOG.info("RUN:" + processStr);

    ProcessBuilder processBuilder = new ProcessBuilder(args);
    final Process process = processBuilder.start();
    String line = null;

    final BufferedReader stderrReader =
        new BufferedReader(new InputStreamReader(process.getErrorStream()));
    while ((line = stderrReader.readLine()) != null) {
      LOG.info("STDERR: " + line);
    }

    final BufferedReader stdoutReader =
        new BufferedReader(new InputStreamReader(process.getInputStream()));
    StringBuilder stdout = new StringBuilder();
    while ((line = stdoutReader.readLine()) != null) {
      stdout.append(line + "\n");
    }

    if (!process.waitFor(timeoutSeconds, TimeUnit.SECONDS)) {
      throw new YBBackupException(
          "Timeout of process run (" + timeoutSeconds + " seconds): [" + processStr + "]");
    }

    final int exitCode = process.exitValue();
    LOG.info("Process [" + processStr + "] exit code: " + exitCode);

    if (exitCode != 0) {
      LOG.info("STDOUT:\n" + stdout.toString());
      throw new YBBackupException(
          "Failed process with exit code " + exitCode + ": [" + processStr + "]");
    }

    return stdout.toString();
  }

  public static String runYbBackup(List<String> args) throws Exception {
    checkArgument(args.contains("create") || args.contains("restore")
    || args.contains("delete"), "argument create/restore/delete is missing");
    checkArgument(args.contains("--backup_location"), "argument --backup_location is missing");
    final String ybAdminPath = TestUtils.findBinary("yb-admin");
    final String ysqlDumpPath = TestUtils.findBinary("../postgres/bin/ysql_dump");
    final String ysqlShellPath = TestUtils.findBinary("../postgres/bin/ysqlsh");
    final String ybBackupPath = TestUtils.findBinary("../../../managed/devops/bin/yb_backup.py");
    final String pythonVenvWrapperPath =
        TestUtils.findBinary("../../../build-support/run_in_build_python_venv.sh");

    List<String> processCommand = new ArrayList<String>(Arrays.asList(
        pythonVenvWrapperPath, ybBackupPath,
        "--masters", masterAddresses,
        "--remote_yb_admin_binary=" + ybAdminPath,
        "--remote_ysql_dump_binary=" + ysqlDumpPath,
        "--remote_ysql_shell_binary=" + ysqlShellPath,
        "--storage_type", "nfs",
        "--nfs_storage_path", TestUtils.getBaseTmpDir(),
        "--no_ssh",
        "--no_auto_name",
        "--TEST_never_fsync"));

    if (postgresContactPoint != null) {
      processCommand.add("--ysql_host=" + postgresContactPoint.getHostName());
      processCommand.add("--ysql_port=" + postgresContactPoint.getPort());
    }

    if (tsWebHostsAndPorts != null) {
      processCommand.add("--ts_web_hosts_ports=" + tsWebHostsAndPorts);
    }

    if (verboseMode) {
      processCommand.add("--verbose");
    }

    if (!SystemUtil.IS_LINUX) {
      processCommand.add("--mac");
    }

    processCommand.addAll(args);
    final String output = runProcess(processCommand, defaultYbBackupTimeoutInSeconds);
    LOG.info("yb_backup output: " + output);

    JSONObject json = new JSONObject(output);
    if (json.has("error")) {
      final String error = json.getString("error");
      LOG.info("yb_backup failed with error: " + error);
      throw new YBBackupException("yb_backup failed with error: " + error);
    }

    return output;
  }

  public static String getTempBackupDir() {
    return TestUtils.getBaseTmpDir() + "/backup-" + new Random().nextInt(Integer.MAX_VALUE);
  }

  public static String runYbBackupCreate(String... args) throws Exception {
    return runYbBackupCreate(Arrays.asList(args));
  }

  public static String runYbBackupCreate(List<String> args) throws Exception {
    List<String> processCommand = new ArrayList<String>(Arrays.asList("create"));
    processCommand.addAll(args);
    final String output = runYbBackup(processCommand);
    JSONObject json = new JSONObject(output);
    final String url = json.getString("snapshot_url");
    LOG.info("SUCCESS. Backup-create operation result - snapshot url: " + url);
    return output;
  }

  public static void runYbBackupRestore(String backupDir, String... args) throws Exception {
    runYbBackupRestore(backupDir, Arrays.asList(args));
  }

  public static void runYbBackupCommand(String command, String backupDir, List<String> args)
      throws Exception {
    List<String> processCommand = new ArrayList<String>(Arrays.asList(
        "--backup_location", backupDir,
        command));
    processCommand.addAll(args);
    final String output = runYbBackup(processCommand);
    JSONObject json = new JSONObject(output);
    final boolean resultOk = json.getBoolean("success");
    LOG.info("SUCCESS. Backup-" + command + " operation result: " + resultOk);

    if (!resultOk) {
      throw new YBBackupException("Backup-" + command + " operation result: " + resultOk);
    }
  }

  public static void runYbBackupRestore(String backupDir, List<String> args) throws Exception {
    runYbBackupCommand("restore", backupDir, args);
  }

  public static void runYbBackupDelete(String backupDir) throws Exception {
    runYbBackupCommand("delete", backupDir, new ArrayList<String>());
  }

  public static String runYbAdmin(String... args) throws Exception {
    final String ybAdminPath = TestUtils.findBinary("yb-admin");
    List<String> processCommand = new ArrayList<String>(Arrays.asList(
        ybAdminPath,
        "--master_addresses", masterAddresses
    ));

    processCommand.addAll(Arrays.asList(args));
    final String output = runProcess(processCommand, defaultYbBackupTimeoutInSeconds);
    LOG.info("yb-admin output: " + output);
    return output;
  }

  // Returns list of tablet uuids for a given table.
  public static List<String> getTabletsForTable(String namespace, String tableName)
      throws Exception {
    String output = runYbAdmin("list_tablets", namespace, tableName);
    return Arrays.stream(output.split(System.lineSeparator()))
                 .filter(line -> !line.startsWith("Tablet-UUID"))
                 .map(line -> line.split(" ")[0])
                 .collect(Collectors.toList());
  }
}
