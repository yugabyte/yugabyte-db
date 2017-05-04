/**
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License. See accompanying LICENSE file.
 */
package org.yb.client;

import com.google.common.base.Joiner;
import com.google.common.base.Preconditions;
import com.google.common.base.Stopwatch;
import com.google.common.collect.Lists;
import com.google.common.net.HostAndPort;
import org.apache.commons.io.FileUtils;
import org.yb.util.NetUtil;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.BufferedReader;
import java.io.File;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.net.InetSocketAddress;
import java.nio.file.FileSystems;
import java.util.*;
import java.util.concurrent.ConcurrentHashMap;
import static java.util.concurrent.TimeUnit.MILLISECONDS;

/**
 * Utility class to start and manipulate YB clusters. Relies on being IN the Kudu source code with
 * both the yb-master and yb-tserver binaries already compiled. {@link BaseYBTest} should be
 * extended instead of directly using this class in almost all cases.
 */
public class MiniYBCluster implements AutoCloseable {

  enum YBDaemonType {
    MASTER,
    TSERVER
  }

  private static class YBDaemon {
    public YBDaemon(YBDaemonType type, String[] commandLine, Process process) {
      this.type = type;
      this.commandLine = commandLine;
      this.process = process;
    }

    public YBDaemonType getType() {
      return type;
    }

    public String[] getCommandLine() {
      return commandLine;
    }

    public Process getProcess() {
      return process;
    }

    int getPid() throws NoSuchFieldException, IllegalAccessException {
      return TestUtils.pidOfProcess(process);
    }

    String getPidStr() {
      try {
        return String.valueOf(getPid());
      } catch (NoSuchFieldException | IllegalAccessException ex) {
        return "<error_getting_pid>";
      }
    }

    public int getRpcPort() {
      return rpcPort;
    }

    public void setRpcPort(int rpcPort) {
      this.rpcPort = rpcPort;
    }

    @Override
    public String toString() {
      return type.toString().toLowerCase() + " process on port " + rpcPort +
          " with pid " + getPidStr();
    }

    private final YBDaemonType type;
    private final String[] commandLine;
    private final Process process;
    private int rpcPort;
  }

  private static final Logger LOG = LoggerFactory.getLogger(MiniYBCluster.class);

  // TS and Master ports will be assigned starting with this one.
  private static final int PORT_START = 64030;

  // CQL port needs to be same for all nodes, since CQL clients use a configured port to generate
  // a host:port pair for each node given just the host.
  private static final int CQL_PORT = 9042;

  // How often to push node list refresh events to CQL clients (in seconds)
  public static final int CQL_NODE_LIST_REFRESH = 5;

  public static final int TSERVER_HEARTBEAT_TIMEOUT_MS = 5 * 1000;

  public static final int TSERVER_HEARTBEAT_INTERVAL_MS = 500;

  // We support 127.0.0.1 - 127.0.0.16 on MAC as loopback IPs.
  private static final int NUM_MAX_LOCALHOSTS = 16;

  // List of threads that print
  private final List<Thread> PROCESS_INPUT_PRINTERS = new ArrayList<>();

  // Map of ports to master servers.
  private final Map<Integer, YBDaemon> masterProcesses = new ConcurrentHashMap<>();

  // Map of ports to tablet servers.
  private final Map<Integer, YBDaemon> tserverProcesses = new ConcurrentHashMap<>();

  private final List<String> pathsToDelete = new ArrayList<>();
  private final List<HostAndPort> masterHostPorts = new ArrayList<>();
  private final List<InetSocketAddress> cqlContactPoints = new ArrayList<>();

  // Client we can use for common operations.
  private final YBClient syncClient;
  private final int defaultTimeoutMs;

  private String masterAddresses;

  private MiniYBCluster(int numMasters,
                        int numTservers,
                        int defaultTimeoutMs,
                        List<String> masterArgs,
                        List<String> tserverArgs) throws Exception {
    this.defaultTimeoutMs = defaultTimeoutMs;

    startCluster(numMasters, numTservers, masterArgs, tserverArgs);

    syncClient = new YBClient.YBClientBuilder(getMasterAddresses())
        .defaultAdminOperationTimeoutMs(defaultTimeoutMs)
        .defaultOperationTimeoutMs(defaultTimeoutMs)
        .build();
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
    Stopwatch stopwatch = new Stopwatch().start();
    while (count < expected && stopwatch.elapsed(MILLISECONDS) < defaultTimeoutMs) {
      Thread.sleep(200);
      count = syncClient.listTabletServers().getTabletServersCount();
    }
    return count >= expected;
  }

  /**
   * @return a unique loopback IP address for this PID. This allows running
   * tests in parallel, since 127.0.0.0/8 all act as loopbacks on Linux.
   *
   * The generated IP is based on pid, so this requires that the parallel tests
   * run in separate VMs.
   *
   * On OSX, the above trick doesn't work, so we can't run parallel tests on OSX.
   * Given that, we just return the normal localhost IP.
   */
  private static String getUniqueLocalhost() {
    if ("Mac OS X".equals(System.getProperty("os.name"))) {
      return "127.0.0.1";
    }

    final int pid = TestUtils.getPid();
    return "127." + ((pid & 0xff00) >> 8) + "." + (pid & 0xff) + ".1";
  }

  /**
   * Retrieves the string representation of the localhost IP to use for the provided server_index.
   * @param server_index the server index for which we need the localhost
   * @return string representation of localhost IP.
   */
  private static String getLocalHost(int server_index) throws IllegalArgumentException {
    if (server_index >= NUM_MAX_LOCALHOSTS) {
      throw new IllegalArgumentException(String.format(
        "Server index: %d is too high, max supported: %d", server_index, NUM_MAX_LOCALHOSTS - 1));
    }
    return "127.0.0." + (server_index + 1);
  }

  /**
   * Take into account any started processes and return the next possibly free port number.
   * The multiply by 2 is to account for one port each for rpc and web services per server.
   * The multiply by 3 is to account for tserver, yql and redis ports in tserver process.
   *
   * @return Port number for a potentially free port.
   */
  private int getNextPotentiallyFreePort() {
    return PORT_START + 2 * (masterProcesses.size() + 3 * tserverProcesses.size());
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
                            List<String> tserverArgs) throws Exception {
    Preconditions.checkArgument(numMasters > 0, "Need at least one master");
    Preconditions.checkArgument(numTservers > 0, "Need at least one tablet server");
    // The following props are set via yb-client's pom.
    String baseDirPath = TestUtils.getBaseDir();

    long now = System.currentTimeMillis();
    LOG.info("Starting {} masters...", numMasters);
    int free_port = startMasters(getNextPotentiallyFreePort(), numMasters, baseDirPath, masterArgs);
    LOG.info("Starting {} tablet servers...", numTservers);
    for (int i = 0; i < numTservers; i++) {
      free_port = startTServer(i, tserverArgs, free_port);
    }
  }

  /**
   * Starts a new tserver for the cluster.
   * @param tserverArgs optional args to the tserver (can be null).
   */
  public void startTServer(List<String> tserverArgs) throws Exception {
    startTServer(tserverProcesses.size(), tserverArgs, getNextPotentiallyFreePort());
  }

  public void startTServer(int tserver_index,
                           List<String> tserverArgs) throws Exception {
    startTServer(tserver_index, tserverArgs, getNextPotentiallyFreePort());
  }

  private int startTServer(int tserver_index,
                           List<String> tserverArgs,
                           int free_port) throws Exception {
    String baseDirPath = TestUtils.getBaseDir();
    long now = System.currentTimeMillis();
    String localhost = getLocalHost(tserver_index);

    int rpc_port = TestUtils.findFreePort(free_port);
    int web_port = TestUtils.findFreePort(rpc_port + 1);
    int redis_port = TestUtils.findFreePort(web_port + 1);
    int redis_web_port = TestUtils.findFreePort(redis_port + 1);
    int cql_web_port = TestUtils.findFreePort(redis_web_port + 1);
    String dataDirPath = baseDirPath + "/ts-" + tserver_index + "-" + now;
    String flagsPath = TestUtils.getFlagsPath();
    List<String> tsCmdLine = Lists.newArrayList(
      TestUtils.findBinary("yb-tserver"),
      "--flagfile=" + flagsPath,
      "--fs_wal_dirs=" + dataDirPath,
      "--fs_data_dirs=" + dataDirPath,
      "--tserver_master_addrs=" + masterAddresses,
      "--webserver_interface=" + localhost,
      "--local_ip_for_outbound_sockets=" + localhost,
      "--rpc_bind_addresses=" + localhost + ":" + rpc_port,
      "--webserver_port=" + web_port,
      "--redis_proxy_bind_address=" + localhost + ":" + redis_port,
      "--redis_proxy_webserver_port=" + redis_web_port,
      "--cql_proxy_bind_address=" + localhost + ":" + CQL_PORT,
      "--yb_num_shards_per_tserver=3",
      "--logtostderr",
      "--cql_nodelist_refresh_interval_secs=" + CQL_NODE_LIST_REFRESH,
      "--heartbeat_interval_ms=" + TSERVER_HEARTBEAT_INTERVAL_MS,
      "--cql_proxy_webserver_port=" + cql_web_port);
    if (tserverArgs != null) {
      for (String arg : tserverArgs) {
        tsCmdLine.add(arg);
      }
    }

    final YBDaemon daemon =
      configureAndStartProcess(YBDaemonType.TSERVER,
        tsCmdLine.toArray(new String[tsCmdLine.size()]),
        "yb-tserver:" + localhost + ":" + rpc_port);
    tserverProcesses.put(rpc_port, daemon);
    daemon.setRpcPort(rpc_port);
    cqlContactPoints.add(new InetSocketAddress(localhost, CQL_PORT));
    free_port = cql_web_port + 1;

    if (flagsPath.startsWith(baseDirPath)) {
      // We made a temporary copy of the flags; delete them later.
      pathsToDelete.add(flagsPath);
    }
    pathsToDelete.add(dataDirPath);
    return free_port;
  }

  /**
   * Start a new master server in 'shell' mode. Finds free web and RPC ports and then
   * starts the master on those ports, finally populates the 'masters' map.
   *
   * @return the host and port for a newly created master.
   * @throws Exception if we are unable to start the master.
   */
  public HostAndPort startShellMaster() throws Exception {
    String baseDirPath = TestUtils.getBaseDir();
    String localhost = getLocalHost(masterProcesses.size());
    int startPort = getNextPotentiallyFreePort();
    int rpcPort = TestUtils.findFreePort(startPort + 1);
    int webPort = TestUtils.findFreePort(startPort + 2);
    LOG.info("Starting shell master at rpc port {}.", rpcPort);
    long now = System.currentTimeMillis();
    String dataDirPath = baseDirPath + "/master-" + masterProcesses.size() + "-" + now;
    String flagsPath = TestUtils.getFlagsPath();
    List<String> masterCmdLine = Lists.newArrayList(
      TestUtils.findBinary("yb-master"),
      "--flagfile=" + flagsPath,
      "--fs_wal_dirs=" + dataDirPath,
      "--fs_data_dirs=" + dataDirPath,
      "--webserver_interface=" + localhost,
      "--local_ip_for_outbound_sockets=" + localhost,
      "--rpc_bind_addresses=" + localhost + ":" + rpcPort,
      "--logtostderr",
      "--webserver_port=" + webPort);
    final YBDaemon daemon = configureAndStartProcess(
        YBDaemonType.MASTER, masterCmdLine.toArray(new String[masterCmdLine.size()]),
        "yb-master:" + localhost + ":" + rpcPort);
    masterProcesses.put(rpcPort, daemon);
    daemon.setRpcPort(rpcPort);

    HostAndPort newHp = HostAndPort.fromParts(localhost, rpcPort);
    masterHostPorts.add(newHp);

    if (flagsPath.startsWith(baseDirPath)) {
      // We made a temporary copy of the flags; delete them later.
      pathsToDelete.add(flagsPath);
    }
    pathsToDelete.add(dataDirPath);

    // Sleep for some time to let the shell master to get initialized and running.
    Thread.sleep(5000);

    return newHp;
  }

  /**
   * Start the specified number of master servers with ports starting from a specified
   * number. Finds free web and RPC ports up front for all of the masters first, then
   * starts them on those ports, populating 'masters' map.
   *
   * @param masterStartPort the starting point of the port range for the masters
   * @param numMasters number of masters to start
   * @param baseDirPath  the base directory where the mini cluster stores its data
   * @return the next free port
   * @throws Exception if we are unable to start the masters
   */
  private int startMasters(int masterStartPort,
                           int numMasters,
                           String baseDirPath,
                           List<String> masterArgs) throws Exception {
    // Get the list of web and RPC ports to use for the master consensus configuration:
    // request NUM_MASTERS * 2 free ports as we want to also reserve the web
    // ports for the consensus configuration.
    List<Integer> ports = TestUtils.findFreePorts(masterStartPort, numMasters * 2);
    int lastFreePort = ports.get(ports.size() - 1);
    List<Integer> masterRpcPorts = Lists.newArrayListWithCapacity(numMasters);
    List<Integer> masterWebPorts = Lists.newArrayListWithCapacity(numMasters);
    for (int i = 0; i < numMasters * 2; i++) {
      if (i % 2 == 0) {
        String localhost = getLocalHost(i / 2);
        masterRpcPorts.add(ports.get(i));
        masterHostPorts.add(HostAndPort.fromParts(localhost, ports.get(i)));
      } else {
        masterWebPorts.add(ports.get(i));
      }
    }
    masterAddresses = NetUtil.hostsAndPortsToString(masterHostPorts);
    for (int i = 0; i < numMasters; i++) {
      String localhost = getLocalHost(i);
      long now = System.currentTimeMillis();
      String dataDirPath = baseDirPath + "/master-" + i + "-" + now;
      String flagsPath = TestUtils.getFlagsPath();
      // The web port must be reserved in the call to findFreePorts above and specified
      // to avoid the scenario where:
      // 1) findFreePorts finds RPC ports a, b, c for the 3 masters.
      // 2) start master 1 with RPC port and let it bind to any (specified as 0) web port.
      // 3) master 1 happens to bind to port b for the web port, as master 2 hasn't been
      // started yet and findFreePort(s) is "check-time-of-use" (it does not reserve the
      // ports, only checks that when it was last called, these ports could be used).
      List<String> masterCmdLine = Lists.newArrayList(
        TestUtils.findBinary("yb-master"),
        "--create_cluster",
        "--flagfile=" + flagsPath,
        "--fs_wal_dirs=" + dataDirPath,
        "--fs_data_dirs=" + dataDirPath,
        "--webserver_interface=" + localhost,
        "--local_ip_for_outbound_sockets=" + localhost,
        "--rpc_bind_addresses=" + localhost + ":" + masterRpcPorts.get(i),
        "--logtostderr",
        "--tserver_unresponsive_timeout_ms=" + TSERVER_HEARTBEAT_TIMEOUT_MS,
        "--webserver_port=" + masterWebPorts.get(i));
      if (numMasters > 1) {
        masterCmdLine.add("--master_addresses=" + masterAddresses);
      }
      if (masterArgs != null) {
        for (String arg : masterArgs) {
          masterCmdLine.add(arg);
        }
      }
      masterProcesses.put(masterRpcPorts.get(i),
          configureAndStartProcess(YBDaemonType.MASTER,
                                   masterCmdLine.toArray(new String[masterCmdLine.size()]),
                                   "yb-master:" + localhost + ":" + masterRpcPorts.get(i)));

      if (flagsPath.startsWith(baseDirPath)) {
        // We made a temporary copy of the flags; delete them later.
        pathsToDelete.add(flagsPath);
      }
      pathsToDelete.add(dataDirPath);
    }
    return lastFreePort + 1;
  }

  /**
   * Starts a process using the provided command and configures it to be daemon,
   * redirects the stderr to stdout, and starts a thread that will read from the process' input
   * stream and redirect that to LOG.
   *
   * @param type Daemon type
   * @param command Process and options
   * @param threadName Name for the thread that logs process output
   * @return The started process
   * @throws Exception Exception if an error prevents us from starting the process,
   *                   or if we were able to start the process but noticed that it was then killed
   *                   (in which case we'll log the exit value).
   */
  private YBDaemon configureAndStartProcess(YBDaemonType type,
                                            String[] command,
                                            String threadName) throws Exception {
    command[0] = FileSystems.getDefault().getPath(command[0]).normalize().toString();
    LOG.info("Starting process: {}", Joiner.on(" ").join(command));
    ProcessBuilder processBuilder = new ProcessBuilder(command);
    processBuilder.redirectErrorStream(true);
    Process proc = processBuilder.start();
    ProcessInputStreamLogPrinterRunnable printer =
        new ProcessInputStreamLogPrinterRunnable(proc.getInputStream());
    Thread thread = new Thread(printer);
    thread.setDaemon(true);
    thread.setName(threadName);
    PROCESS_INPUT_PRINTERS.add(thread);
    thread.start();

    Thread.sleep(300);
    try {
      int ev = proc.exitValue();
      throw new Exception("We tried starting a process (" + command[0] + ") but it exited with " +
          "value=" + ev);
    } catch (IllegalThreadStateException ex) {
      // This means the process is still alive, it's like reverse psychology.
    }

    LOG.info("Started " + command[0] + " as pid " + TestUtils.pidOfProcess(proc));

    return new YBDaemon(type, command, proc);
  }

  private void processCoreFile(YBDaemon daemon) throws Exception {
    final int pid = daemon.getPid();
    final File coreFile = new File("core." + pid);
    if (coreFile.exists()) {
      LOG.warn("Found core file '{}' from {}", coreFile.getAbsolutePath(), daemon.toString());
      Process analyzeCoreFileScript = new ProcessBuilder().command(Arrays.asList(new String[] {
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

  private void destroyDaemon(YBDaemon daemon) throws Exception {
    LOG.warn("Destroying " + daemon.toString());
    daemon.getProcess().destroy();
    processCoreFile(daemon);
  }

  private void destroyDaemonAndWait(YBDaemon daemon) throws Exception {
    destroyDaemon(daemon);
    daemon.getProcess().waitFor();
  }

  /**
   * Kills the TS listening on the provided port. Doesn't do anything if the TS was already killed.
   * @param port port on which the tablet server is listening on
   * @throws InterruptedException
   */
  public void killTabletServerOnPort(int port) throws Exception {
    YBDaemon ts = tserverProcesses.remove(port);
    if (ts == null) {
      // The TS is already dead, good.
      return;
    }
    destroyDaemonAndWait(ts);
  }

  public Set<Integer> getTabletServerPorts() {
    return tserverProcesses.keySet();
  }

  /**
   * Kills the master listening on the provided port. Doesn't do anything if the master was
   * already killed.
   * @param port port on which the master is listening on
   * @throws InterruptedException
   */
  public void killMasterOnPort(int port) throws Exception {
    YBDaemon master = masterProcesses.remove(port);
    if (master == null) {
      // The master is already dead, good.
      return;
    }
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
  private List<Process> destroyDaemons(Collection<YBDaemon> daemons) throws Exception {
    List<Process> processes = new ArrayList<>();
    for (Iterator<YBDaemon> iter = daemons.iterator(); iter.hasNext(); ) {
      final YBDaemon daemon = iter.next();
      LOG.info("Destroying {} process with pid {}",
        daemon.getType().toString().toLowerCase(), daemon.getPid());
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
    List<Process> processes = new ArrayList<>();
    processes.addAll(destroyDaemons(masterProcesses.values()));
    processes.addAll(destroyDaemons(tserverProcesses.values()));
    for (Process process : processes) {
      process.waitFor();
    }
    for (Thread thread : PROCESS_INPUT_PRINTERS) {
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
   * Returns a list of CQL contact points.
   * @return CQL contact points
   */
  public List<InetSocketAddress> getCQLContactPoints() {
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

    public ProcessInputStreamLogPrinterRunnable(InputStream is) {
      this.is = is;
    }

    @Override
    public void run() {
      try {
        String line;
        BufferedReader in = new BufferedReader(new InputStreamReader(is));
        while ((line = in.readLine()) != null) {
          LOG.info(line);
        }
        in.close();
      }
      catch (Exception e) {
        if (!e.getMessage().contains("Stream closed")) {
          LOG.error("Caught error while reading a process' output", e);
        }
      }
    }
  }

  public static class MiniYBClusterBuilder {

    private int numMasters = 1;
    private int numTservers = 3;
    private int defaultTimeoutMs = 50000;
    private List<String> masterArgs = null;
    private List<String> tserverArgs = null;

    public MiniYBClusterBuilder numMasters(int numMasters) {
      this.numMasters = numMasters;
      return this;
    }

    public MiniYBClusterBuilder numTservers(int numTservers) {
      this.numTservers = numTservers;
      return this;
    }

    /**
     * Configures the internal client to use the given timeout for all operations. Also uses the
     * timeout for tasks like waiting for tablet servers to check in with the master.
     * @param defaultTimeoutMs timeout in milliseconds
     * @return this instance
     */
    public MiniYBClusterBuilder defaultTimeoutMs(int defaultTimeoutMs) {
      this.defaultTimeoutMs = defaultTimeoutMs;
      return this;
    }

    /**
     * Configure additional command-line arguments for starting master.
     * @param masterArgs additional command-line arguments
     * @return this instance
     */
    public MiniYBClusterBuilder masterArgs(List<String> masterArgs) {
      this.masterArgs = masterArgs;
      return this;
    }

    /**
     * Configure additional command-line arguments for starting tserver.
     */
    public MiniYBClusterBuilder tserverArgs(List<String> tserverArgs) {
      this.tserverArgs = tserverArgs;
      return this;
    }

    public MiniYBCluster build() throws Exception {
      return new MiniYBCluster(numMasters, numTservers, defaultTimeoutMs, masterArgs, tserverArgs);
    }
  }

}
