/**
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * <p>
 * http://www.apache.org/licenses/LICENSE-2.0
 * <p>
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License. See accompanying LICENSE file.
 *
 * Portions Copyright (c) YugaByte, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License
 * is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied.  See the License for the specific language governing permissions and limitations
 * under the License.
 *
 */
package org.yb.minicluster;

import com.google.common.base.Joiner;
import com.google.common.base.Preconditions;
import com.google.common.base.Stopwatch;
import com.google.common.collect.Lists;
import com.google.common.net.HostAndPort;
import org.apache.commons.io.FileUtils;
import org.yb.client.BaseYBClientTest;
import org.yb.client.TestUtils;
import org.yb.client.YBClient;
import org.yb.util.NetUtil;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.*;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.nio.file.FileSystems;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.*;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicInteger;

import static java.util.concurrent.TimeUnit.MILLISECONDS;

/**
 * Utility class to start and manipulate YB clusters. Relies on being IN the source code with
 * both the yb-master and yb-tserver binaries already compiled. {@link BaseYBClientTest} should be
 * extended instead of directly using this class in almost all cases.
 */
public class MiniYBCluster implements AutoCloseable {

  private static final Logger LOG = LoggerFactory.getLogger(MiniYBCluster.class);

  // CQL port needs to be same for all nodes, since CQL clients use a configured port to generate
  // a host:port pair for each node given just the host.
  private static final int CQL_PORT = 9042;

  // How often to push node list refresh events to CQL clients (in seconds)
  public static int CQL_NODE_LIST_REFRESH_SECS = 5;

  public static final int TSERVER_HEARTBEAT_TIMEOUT_MS = 5 * 1000;

  public static final int TSERVER_HEARTBEAT_INTERVAL_MS = 500;

  public static final int CATALOG_MANAGER_BG_TASK_WAIT_MS = 500;

  // We support 127.0.0.1 - 127.0.0.16 on MAC as loopback IPs.
  private static final int NUM_LOCALHOSTS_ON_MAC_OS_X = 16;

  private static final String TSERVER_MASTER_ADDRESSES_FLAG = "--tserver_master_addrs";

  private static final String TSERVER_MASTER_ADDRESSES_FLAG_REGEX =
      TSERVER_MASTER_ADDRESSES_FLAG + ".*";

  // High threshold to avoid excessive slow query log.
  private static final int RPC_SLOW_QUERY_THRESHOLD = 10000000;

  // List of threads that print
  private final List<Thread> processInputPrinters = new ArrayList<>();

  // Map of host/port pairs to master servers.
  private final Map<HostAndPort, MiniYBDaemon> masterProcesses = new ConcurrentHashMap<>();

  // Map of host/port pairs to tablet servers.
  private final Map<HostAndPort, MiniYBDaemon> tserverProcesses = new ConcurrentHashMap<>();

  private final List<String> pathsToDelete = new ArrayList<>();
  private final List<HostAndPort> masterHostPorts = new ArrayList<>();
  private final List<InetSocketAddress> cqlContactPoints = new ArrayList<>();

  // Client we can use for common operations.
  private final YBClient syncClient;
  private final int defaultTimeoutMs;

  private String masterAddresses;

  // This is used as part of the seed when mapping server indexes to unique loopback IPs.
  private final long miniClusterInitTimeMillis = System.currentTimeMillis();

  // We pass the Java test class name as a command line option to YB daemons so that we know what
  // test invoked them if they get stuck.
  private final String testClassName;

  /**
   * This is used to prevent trying to launch two tservers on the same loopback IP. However, this is
   * not static, so if we launch multiple mini-clusters in the same test, they could clash on IPs.
   */
  private final Set<String> tserverUsedBindIPs = new HashSet<>();

  // These are used to assign master/tserver indexes used in the logs (the "m1", "ts2", etc.
  // prefixes).
  private AtomicInteger nextMasterIndex = new AtomicInteger(0);
  private AtomicInteger nextTServerIndex = new AtomicInteger(0);

  private static final int DEFAULT_NUM_SHARDS_PER_TSERVER = 3;
  private static int numShardsPerTserver = DEFAULT_NUM_SHARDS_PER_TSERVER;

  public static void setNumShardsPerTserver(int numShards) {
    numShardsPerTserver = numShards;
  }

  /**
   * Hard memory limit for YB daemons. This should be consistent with the memory limit set for C++
   * based mini clusters in external_mini_cluster.cc.
   */
  private static final long DAEMON_MEMORY_LIMIT_HARD_BYTES = 1024 * 1024 * 1024;

  /**
   * Not to be invoked directly, but through a {@link MiniYBClusterBuilder}.
   */
  MiniYBCluster(int numMasters,
                int numTservers,
                int defaultTimeoutMs,
                List<String> masterArgs,
                List<List<String>> tserverArgs,
                String testClassName) throws Exception {
    this.defaultTimeoutMs = defaultTimeoutMs;
    this.testClassName = testClassName;

    startCluster(numMasters, numTservers, masterArgs, tserverArgs);

    syncClient = new YBClient.YBClientBuilder(getMasterAddresses())
        .defaultAdminOperationTimeoutMs(defaultTimeoutMs)
        .defaultOperationTimeoutMs(defaultTimeoutMs)
        .build();

    syncClient.waitForMasterLeader(defaultTimeoutMs);
  }

  private List<String> getCommonDaemonFlags() {
    final List<String> commonFlags = Lists.newArrayList(
        "--logtostderr",
        "--memory_limit_hard_bytes=1073741824",  // 1 GB.
        "--webserver_doc_root=" + TestUtils.getWebserverDocRoot());
    final String extraFlagsFromEnv = System.getenv("YB_EXTRA_DAEMON_FLAGS");
    if (extraFlagsFromEnv != null) {
      // This has an issue with handling quoted arguments with embedded spaces.
      for (String flag : extraFlagsFromEnv.split(" ")) {
        commonFlags.add(flag);
      }
    }
    if (testClassName != null) {
      commonFlags.add("--yb_test_name=" + testClassName);
    }
    commonFlags.add("--memory_limit_hard_bytes=" + DAEMON_MEMORY_LIMIT_HARD_BYTES);
    return commonFlags;
  }

  /**
   * Wait up to this instance's "default timeout" for an expected count of TS to
   * connect to the master.
   *
   * @param expected How many TS are expected
   * @return true if there are at least as many TS as expected, otherwise false
   */
  public boolean waitForTabletServers(int expected) throws Exception {
    int count = 0;
    Stopwatch stopwatch = Stopwatch.createStarted();
    while (count < expected && stopwatch.elapsed(MILLISECONDS) < defaultTimeoutMs) {
      Thread.sleep(200);
      count = syncClient.listTabletServers().getTabletServersCount();
    }
    return count >= expected;
  }

  /**
   * @return string representation of localhost IP.
   */
  private String getRandomBindAddressOnLinux() throws IllegalArgumentException {
    assert(TestUtils.IS_LINUX);
    // On Linux we can use 127.x.y.z, so let's pick deterministic random-ish values based on the
    // current process's pid and server index.
    //
    // In case serverIndex is -1, pick random addresses.
    final StringBuilder randomLoopbackIp = new StringBuilder("127");
    final Random rng = TestUtils.getRandomGenerator();
    for (int i = 0; i < 3; ++i) {
      // Do not use 0 or 255 for IP components.
      randomLoopbackIp.append("." + (1 + rng.nextInt(254)));
    }
    return randomLoopbackIp.toString();
  }

  private boolean canUseForTServer(String bindAddress, boolean logException)
      throws IOException {
    if (tserverUsedBindIPs.contains(bindAddress)) {
      return false;
    }
    final InetAddress bindIp = InetAddress.getByName(bindAddress);
    if (TestUtils.isPortFree(bindIp, CQL_PORT, logException)) {
      tserverUsedBindIPs.add(bindAddress);
      return true;
    }
    return false;
  }

  private String getTabletServerBindAddress() throws IllegalArgumentException, IOException {
    if (TestUtils.IS_LINUX) {
      final int NUM_ATTEMPTS = 1000;
      for (int i = 1; i <= NUM_ATTEMPTS; ++i) {
        String randomBindAddress = getRandomBindAddressOnLinux();
        if (canUseForTServer(randomBindAddress, i == NUM_ATTEMPTS)) {
          return randomBindAddress;
        }
      }
      throw new IOException("Could not find a loopback IP where port " + CQL_PORT + " is free " +
          "in " + NUM_ATTEMPTS + " attempts");
    }

    for (int i = 1; i <= NUM_LOCALHOSTS_ON_MAC_OS_X; ++i) {
      String bindAddress = "127.0.0." + i;
      if (canUseForTServer(bindAddress, i == NUM_LOCALHOSTS_ON_MAC_OS_X)) {
        return bindAddress;
      }
    }

    throw new IOException("Cannot find a loopback IP to launch a tablet server on");
  }

  private String getMasterBindAddress() {
    if (TestUtils.IS_LINUX) {
      return getRandomBindAddressOnLinux();
    }
    final Random rng = TestUtils.getRandomGenerator();
    return "127.0.0." + (1 + rng.nextInt(NUM_LOCALHOSTS_ON_MAC_OS_X - 1));
  }

  /**
   * Starts a YB cluster composed of the provided masters and tablet servers.
   *
   * @param numMasters how many masters to start
   * @param numTservers how many tablet servers to start
   * @throws Exception
   */
  private void startCluster(int numMasters,
                            int numTservers,
                            List<String> masterArgs,
                            List<List<String>> tserverArgs) throws Exception {
    Preconditions.checkArgument(numMasters > 0, "Need at least one master");
    Preconditions.checkArgument(numTservers > 0, "Need at least one tablet server");
    // The following props are set via yb-client's pom.
    String baseDirPath = TestUtils.getBaseDir();

    LOG.info("Starting {} masters...", numMasters);
    startMasters(numMasters, baseDirPath, masterArgs);
    LOG.info("Starting {} tablet servers...", numTservers);
    for (int i = 0; i < numTservers; i++) {
      startTServer(tserverArgs.get(i));
    }
  }

  /**
   * Update the master addresses for MiniYBCluster and also for the flagsfile so that tservers
   * pick it up.
   */
  private void updateMasterAddresses() throws IOException {
    masterAddresses = NetUtil.hostsAndPortsToString(masterHostPorts);
    Path flagsFile = Paths.get (TestUtils.getFlagsPath());
    String content = new String(Files.readAllBytes(flagsFile));
    LOG.info("Retrieved flags file content: " + content);
    String tserverMasterAddressesFlag = String.format("%s=%s", TSERVER_MASTER_ADDRESSES_FLAG,
      masterAddresses);
    if (content.contains(TSERVER_MASTER_ADDRESSES_FLAG)) {
      content = content.replaceAll(TSERVER_MASTER_ADDRESSES_FLAG_REGEX, tserverMasterAddressesFlag);
    } else {
      content += tserverMasterAddressesFlag + "\n";
    }
    Files.write(flagsFile, content.getBytes());
    LOG.info("Wrote flags file content: " + content);
  }

  public void startTServer(List<String> tserverArgs) throws Exception {
    String baseDirPath = TestUtils.getBaseDir();
    long now = System.currentTimeMillis();
    final String tserverBindAddress = getTabletServerBindAddress();

    final int rpcPort = TestUtils.findFreePort(tserverBindAddress);
    final int webPort = TestUtils.findFreePort(tserverBindAddress);
    final int redisPort = TestUtils.findFreePort(tserverBindAddress);
    final int redisWebPort = TestUtils.findFreePort(tserverBindAddress);
    final int cqlWebPort = TestUtils.findFreePort(tserverBindAddress);

    String dataDirPath = baseDirPath + "/ts-" + tserverBindAddress + "-" + rpcPort + "-" + now;
    String flagsPath = TestUtils.getFlagsPath();
    final List<String> tsCmdLine = Lists.newArrayList(
        TestUtils.findBinary("yb-tserver"),
        "--flagfile=" + flagsPath,
        "--fs_data_dirs=" + dataDirPath,
        "--tserver_master_addrs=" + masterAddresses,
        "--webserver_interface=" + tserverBindAddress,
        "--local_ip_for_outbound_sockets=" + tserverBindAddress,
        "--rpc_bind_addresses=" + tserverBindAddress + ":" + rpcPort,
        "--webserver_port=" + webPort,
        "--redis_proxy_bind_address=" + tserverBindAddress + ":" + redisPort,
        "--redis_proxy_webserver_port=" + redisWebPort,
        "--cql_proxy_bind_address=" + tserverBindAddress + ":" + CQL_PORT,
        "--yb_num_shards_per_tserver=" + numShardsPerTserver,
        "--cql_nodelist_refresh_interval_secs=" + CQL_NODE_LIST_REFRESH_SECS,
        "--heartbeat_interval_ms=" + TSERVER_HEARTBEAT_INTERVAL_MS,
        "--rpc_slow_query_threshold_ms=" + RPC_SLOW_QUERY_THRESHOLD,
        "--cql_proxy_webserver_port=" + cqlWebPort);
    tsCmdLine.addAll(getCommonDaemonFlags());
    if (tserverArgs != null) {
      for (String arg : tserverArgs) {
        tsCmdLine.add(arg);
      }
    }

    final MiniYBDaemon daemon = configureAndStartProcess(MiniYBDaemonType.TSERVER,
        tsCmdLine.toArray(new String[tsCmdLine.size()]),
        tserverBindAddress, rpcPort, webPort, cqlWebPort, redisWebPort);
    tserverProcesses.put(HostAndPort.fromParts(tserverBindAddress, rpcPort), daemon);
    cqlContactPoints.add(new InetSocketAddress(tserverBindAddress, CQL_PORT));

    if (flagsPath.startsWith(baseDirPath)) {
      // We made a temporary copy of the flags; delete them later.
      pathsToDelete.add(flagsPath);
    }
    pathsToDelete.add(dataDirPath);
  }

  /**
   * Returns the common options among regular masters and shell masters.
   * @return a list of command line options
   */
  private List<String> getCommonMasterCmdLine(String flagsPath, String dataDirPath,
                                              String masterBindAddress, int masterRpcPort,
                                              int masterWebPort) throws Exception {
    return Lists.newArrayList(
      TestUtils.findBinary("yb-master"),
      "--flagfile=" + flagsPath,
      "--fs_wal_dirs=" + dataDirPath,
      "--fs_data_dirs=" + dataDirPath,
      "--webserver_interface=" + masterBindAddress,
      "--local_ip_for_outbound_sockets=" + masterBindAddress,
      "--rpc_bind_addresses=" + masterBindAddress + ":" + masterRpcPort,
      "--tserver_unresponsive_timeout_ms=" + TSERVER_HEARTBEAT_TIMEOUT_MS,
      "--catalog_manager_bg_task_wait_ms=" + CATALOG_MANAGER_BG_TASK_WAIT_MS,
      "--rpc_slow_query_threshold_ms=" + RPC_SLOW_QUERY_THRESHOLD,
      "--webserver_port=" + masterWebPort);
  }

  /**
   * Start a new master server in 'shell' mode. Finds free web and RPC ports and then
   * starts the master on those ports, finally populates the 'masters' map.
   *
   * @return the host and port for a newly created master.
   * @throws Exception if we are unable to start the master.
   */
  public HostAndPort startShellMaster() throws Exception {
    final String baseDirPath = TestUtils.getBaseDir();
    final String masterBindAddress = getMasterBindAddress();
    final int rpcPort = TestUtils.findFreePort(masterBindAddress);
    final int webPort = TestUtils.findFreePort(masterBindAddress);
    LOG.info("Starting shell master on {}, port {}.", masterBindAddress, rpcPort);
    long now = System.currentTimeMillis();
    final String dataDirPath =
        baseDirPath + "/master-" + masterBindAddress + "-" + rpcPort + "-" + now;
    final String flagsPath = TestUtils.getFlagsPath();
    List<String> masterCmdLine = getCommonMasterCmdLine(flagsPath, dataDirPath,
      masterBindAddress, rpcPort, webPort);
    masterCmdLine.addAll(getCommonDaemonFlags());

    final MiniYBDaemon daemon = configureAndStartProcess(
        MiniYBDaemonType.MASTER, masterCmdLine.toArray(new String[masterCmdLine.size()]),
        masterBindAddress, rpcPort, webPort, -1, -1);

    final HostAndPort masterHostPort = HostAndPort.fromParts(masterBindAddress, rpcPort);
    masterHostPorts.add(masterHostPort);
    updateMasterAddresses();
    masterProcesses.put(masterHostPort, daemon);

    if (flagsPath.startsWith(baseDirPath)) {
      // We made a temporary copy of the flags; delete them later.
      pathsToDelete.add(flagsPath);
    }
    pathsToDelete.add(dataDirPath);

    // Sleep for some time to let the shell master to get initialized and running.
    Thread.sleep(5000);

    return masterHostPort;
  }

  private static class MasterHostPortAllocation {
    final String bindAddress;
    final int rpcPort;
    final int webPort;

    public MasterHostPortAllocation(String bindAddress, int rpcPort, int webPort) {
      this.bindAddress = bindAddress;
      this.rpcPort = rpcPort;
      this.webPort = webPort;
    }
  }

  /**
   * Start the specified number of master servers with ports starting from a specified
   * number. Finds free web and RPC ports up front for all of the masters first, then
   * starts them on those ports, populating 'masters' map.
   *
   * @param numMasters number of masters to start
   * @param baseDirPath  the base directory where the mini cluster stores its data
   * @param extraMasterArgs common command-line arguments to pass to all masters
   * @throws Exception if we are unable to start the masters
   */
  private void startMasters(
      int numMasters,
      String baseDirPath,
      List<String> extraMasterArgs) throws Exception {
    assert(masterHostPorts.isEmpty());

    // Get the list of web and RPC ports to use for the master consensus configuration:
    // request NUM_MASTERS * 2 free ports as we want to also reserve the web
    // ports for the consensus configuration.
    final List<MasterHostPortAllocation> masterHostPortAlloc = new ArrayList<>();
    for (int i = 0; i < numMasters; ++i) {
      final String masterBindAddress = getMasterBindAddress();
      final int rpcPort = TestUtils.findFreePort(masterBindAddress);
      final int webPort = TestUtils.findFreePort(masterBindAddress);
      masterHostPortAlloc.add(
          new MasterHostPortAllocation(masterBindAddress, rpcPort, webPort));
      masterHostPorts.add(HostAndPort.fromParts(masterBindAddress, rpcPort));
    }

    updateMasterAddresses();
    for (MasterHostPortAllocation masterAlloc : masterHostPortAlloc) {
      final String masterBindAddress = masterAlloc.bindAddress;
      final int masterRpcPort = masterAlloc.rpcPort;
      final long now = System.currentTimeMillis();
      String dataDirPath =
          baseDirPath + "/master-" + masterBindAddress + "-" + masterRpcPort + "-" + now;
      String flagsPath = TestUtils.getFlagsPath();
      final int masterWebPort = masterAlloc.webPort;
      List<String> masterCmdLine = getCommonMasterCmdLine(flagsPath, dataDirPath,
        masterBindAddress, masterRpcPort, masterWebPort);
      masterCmdLine.addAll(getCommonDaemonFlags());
      masterCmdLine.add("--master_addresses=" + masterAddresses);
      if (extraMasterArgs != null) {
        masterCmdLine.addAll(extraMasterArgs);
      }
      final HostAndPort masterHostAndPort = HostAndPort.fromParts(masterBindAddress, masterRpcPort);
      masterProcesses.put(masterHostAndPort,
          configureAndStartProcess(
              MiniYBDaemonType.MASTER,
              masterCmdLine.toArray(new String[masterCmdLine.size()]),
              masterBindAddress, masterRpcPort, masterWebPort, -1, -1));

      if (flagsPath.startsWith(baseDirPath)) {
        // We made a temporary copy of the flags; delete them later.
        pathsToDelete.add(flagsPath);
      }
      pathsToDelete.add(dataDirPath);
    }
  }

  /**
   * Starts a process using the provided command and configures it to be daemon,
   * redirects the stderr to stdout, and starts a thread that will read from the process' input
   * stream and redirect that to LOG.
   *
   * @param type Daemon type
   * @param command Process and options
   * @return The started process
   * @throws Exception Exception if an error prevents us from starting the process,
   *                   or if we were able to start the process but noticed that it was then killed
   *                   (in which case we'll log the exit value).
   */
  private MiniYBDaemon configureAndStartProcess(MiniYBDaemonType type,
                                                String[] command,
                                                String bindIp,
                                                int rpcPort,
                                                int webPort,
                                                int cqlWebPort,
                                                int redisWebPort) throws Exception {
    command[0] = FileSystems.getDefault().getPath(command[0]).normalize().toString();
    LOG.info("Starting process: {}", Joiner.on(" ").join(command));
    ProcessBuilder processBuilder = new ProcessBuilder(command);
    processBuilder.redirectErrorStream(true);
    Process proc = processBuilder.start();
    final int indexForLog =
        type == MiniYBDaemonType.MASTER ? nextMasterIndex.incrementAndGet()
                                        : nextTServerIndex.incrementAndGet();
    final MiniYBDaemon daemon =
        new MiniYBDaemon(type, indexForLog, command, proc, bindIp, rpcPort, webPort, cqlWebPort,
            redisWebPort);

    ProcessInputStreamLogPrinterRunnable printer =
        new ProcessInputStreamLogPrinterRunnable(proc.getInputStream(), daemon.getLogPrefix());
    final Thread logPrinterThread = new Thread(printer);
    logPrinterThread.setDaemon(true);
    logPrinterThread.setName("Log printer for " + daemon.getLogPrefix().trim());
    processInputPrinters.add(logPrinterThread);
    logPrinterThread.start();

    Thread.sleep(300);
    try {
      int ev = proc.exitValue();
      throw new Exception("We tried starting a process (" + command[0] + ") but it exited with " +
          "value=" + ev);
    } catch (IllegalThreadStateException ex) {
      // This means the process is still alive, it's like reverse psychology.
    }

    LOG.info("Started " + command[0] + " as pid " + TestUtils.pidOfProcess(proc));

    return daemon;
  }

  private void processCoreFile(MiniYBDaemon daemon) throws Exception {
    final int pid = daemon.getPid();
    final File coreFile = new File("core." + pid);
    if (coreFile.exists()) {
      LOG.warn("Found core file '{}' from {}", coreFile.getAbsolutePath(), daemon.toString());
      Process analyzeCoreFileScript = new ProcessBuilder().command(Arrays.asList(new String[]{
          TestUtils.findYbRootDir() + "/build-support/analyze_core_file.sh",
          "--core",
          coreFile.getAbsolutePath(),
          "--executable",
          daemon.getCommandLine()[0]
      })).inheritIO().start();
      analyzeCoreFileScript.waitFor();
      if (coreFile.delete()) {
        LOG.warn("Deleted core file at '{}'", coreFile.getAbsolutePath());
      } else {
        LOG.warn("Failed to delete core file at '{}'", coreFile.getAbsolutePath());
      }
    }
  }

  private void destroyDaemon(MiniYBDaemon daemon) throws Exception {
    LOG.warn("Destroying " + daemon.toString());
    daemon.getProcess().destroy();
    processCoreFile(daemon);
  }

  private void destroyDaemonAndWait(MiniYBDaemon daemon) throws Exception {
    destroyDaemon(daemon);
    daemon.getProcess().waitFor();
  }

  /**
   * Kills the TS listening on the provided port. Doesn't do anything if the TS was already killed.
   * @param hostPort host and port on which the tablet server is listening on
   * @throws InterruptedException
   */
  public void killTabletServerOnHostPort(HostAndPort hostPort) throws Exception {
    final MiniYBDaemon ts = tserverProcesses.remove(hostPort);
    if (ts == null) {
      // The TS is already dead, good.
      return;
    }
    assert(cqlContactPoints.remove(new InetSocketAddress(hostPort.getHostText(), CQL_PORT)));
    destroyDaemonAndWait(ts);
  }

  public Map<HostAndPort, MiniYBDaemon> getTabletServers() {
    return tserverProcesses;
  }

  public Map<HostAndPort, MiniYBDaemon> getMasters() {
    return masterProcesses;
  }

  /**
   * Kills the master listening on the provided host/port combination. Doesn't do anything if the
   * master has already been killed.
   * @param hostAndPort host/port the master is listening on
   * @throws InterruptedException
   */
  public void killMasterOnHostPort(HostAndPort hostAndPort) throws Exception {
    MiniYBDaemon master = masterProcesses.remove(hostAndPort);
    if (master == null) {
      // The master is already dead, good.
      return;
    }
    assert(masterHostPorts.remove(hostAndPort));
    updateMasterAddresses();
    destroyDaemonAndWait(master);
  }

  /**
   * See {@link #shutdown()}.
   * @throws Exception never thrown, exceptions are logged
   */
  @Override
  public void close() throws Exception {
    shutdown();
  }

  /**
   * Destroys the given list of YB daemons. Returns a list of processes in case the caller wants
   * to wait for all of them to shut down.
   */
  private List<Process> destroyDaemons(Collection<MiniYBDaemon> daemons) throws Exception {
    List<Process> processes = new ArrayList<>();
    for (Iterator<MiniYBDaemon> iter = daemons.iterator(); iter.hasNext(); ) {
      final MiniYBDaemon daemon = iter.next();
      destroyDaemon(daemon);
      processes.add(daemon.getProcess());
      iter.remove();
    }
    return processes;
  }

  /**
   * Stops all the processes and deletes the folders used to store data and the flagfile.
   */
  public void shutdown() throws Exception {
    LOG.info("Shutting down mini cluster");
    List<Process> processes = new ArrayList<>();
    processes.addAll(destroyDaemons(masterProcesses.values()));
    processes.addAll(destroyDaemons(tserverProcesses.values()));
    for (Process process : processes) {
      process.waitFor();
    }
    for (Thread thread : processInputPrinters) {
      thread.interrupt();
    }

    for (String path : pathsToDelete) {
      try {
        File f = new File(path);
        if (f.isDirectory()) {
          FileUtils.deleteDirectory(f);
        } else {
          f.delete();
        }
      } catch (Exception e) {
        LOG.warn("Could not delete path {}", path, e);
      }
    }
    if (syncClient != null) {
      syncClient.shutdown();
    }
    LOG.info("Mini cluster shutdown finished");
  }

  /**
   * Returns the comma-separated list of master addresses.
   * @return master addresses
   */
  public String getMasterAddresses() {
    return masterAddresses;
  }

  /**
   * Returns a list of master addresses.
   * @return master addresses
   */
  public List<HostAndPort> getMasterHostPorts() {
    return masterHostPorts;
  }

  /**
   * Returns the master address host and port at the given index.
   * @return the master address host/port, if it exists.
   */
  public HostAndPort getMasterHostPort(int index) throws Exception {
    return masterHostPorts.get(index);
  }

  /**
   * Returns the current number of masters.
   * @return count of masters
   */
  public int getNumMasters() {
    return masterHostPorts.size();
  }

  /**
   * Returns the number of shards per tserver.
   * @return number of shards per tserver.
   */
  public int getNumShardsPerTserver() {
    return numShardsPerTserver;
  }

  /**
   * Returns a list of CQL contact points.
   * @return CQL contact points
   */
  public List<InetSocketAddress> getCQLContactPoints() {
    return cqlContactPoints;
  }

  /**
   * Returns a comma separated list of CQL contact points.
   * @return CQL contact points
   */
  public String getCQLContactPointsAsString() {
    String cqlContactPoints = "";
    for (InetSocketAddress contactPoint : getCQLContactPoints()) {
      if (!cqlContactPoints.isEmpty()) {
        cqlContactPoints += ",";
      }
      cqlContactPoints += contactPoint.getHostName() + ":" + contactPoint.getPort();
    }
    return cqlContactPoints;
  }

  /**
   * Returns a client to this YB cluster.
   * @return YBClient
   */
  public YBClient getClient() {
    return syncClient;
  }

  /**
   * Helper runnable that can log what the processes are sending on their stdout and stderr that
   * we'd otherwise miss.
   */
  static class ProcessInputStreamLogPrinterRunnable implements Runnable {

    private final InputStream is;
    private final String logPrefix;

    public ProcessInputStreamLogPrinterRunnable(InputStream is, String logPrefix) {
      this.is = is;
      this.logPrefix = logPrefix;
    }

    @Override
    public void run() {
      try {
        String line;
        BufferedReader in = new BufferedReader(new InputStreamReader(is));
        while ((line = in.readLine()) != null) {
          System.out.println(logPrefix + line);
        }
        in.close();
      } catch (Exception e) {
        if (!e.getMessage().contains("Stream closed")) {
          LOG.error("Caught error while reading a process' output", e);
        }
      }
    }

  }

}
