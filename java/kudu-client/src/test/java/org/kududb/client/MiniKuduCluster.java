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
package org.kududb.client;

import com.google.common.base.Joiner;
import com.google.common.base.Preconditions;
import com.google.common.base.Stopwatch;
import com.google.common.collect.Lists;
import com.google.common.net.HostAndPort;
import org.apache.commons.io.FileUtils;
import org.kududb.util.NetUtil;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.BufferedReader;
import java.io.File;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.lang.management.ManagementFactory;
import java.lang.management.RuntimeMXBean;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

import sun.management.VMManagement;

/**
 * Utility class to start and manipulate Kudu clusters. Relies on being IN the Kudu source code with
 * both the kudu-master and kudu-tserver binaries already compiled. {@link BaseKuduTest} should be
 * extended instead of directly using this class in almost all cases.
 */
public class MiniKuduCluster implements AutoCloseable {

  private static final Logger LOG = LoggerFactory.getLogger(MiniKuduCluster.class);

  // TS and Master ports will be assigned starting with this one.
  private static final int PORT_START = 64030;

  // List of threads that print
  private final List<Thread> PROCESS_INPUT_PRINTERS = new ArrayList<>();

  // Map of ports to master servers.
  private final Map<Integer, Process> masterProcesses = new ConcurrentHashMap<>();

  // Map of ports to tablet servers.
  private final Map<Integer, Process> tserverProcesses = new ConcurrentHashMap<>();

  private final List<String> pathsToDelete = new ArrayList<>();
  private final List<HostAndPort> masterHostPorts = new ArrayList<>();

  // Client we can use for common operations.
  private final KuduClient syncClient;
  private final int defaultTimeoutMs;

  private String masterAddresses;

  private MiniKuduCluster(int numMasters, int numTservers, int defaultTimeoutMs) throws Exception {
    this.defaultTimeoutMs = defaultTimeoutMs;

    startCluster(numMasters, numTservers);

    syncClient = new KuduClient.KuduClientBuilder(getMasterAddresses())
        .defaultAdminOperationTimeoutMs(defaultTimeoutMs)
        .defaultOperationTimeoutMs(defaultTimeoutMs)
        .build();
  }

  /**
   * Wait up to this instance's "default timeout" for an expected count of TS to
   * connect to the master.
   * @param expected How many TS are expected
   * @return true if there are at least as many TS as expected, otherwise false
   */
  public boolean waitForTabletServers(int expected) throws Exception {
    int count = 0;
    Stopwatch stopwatch = new Stopwatch().start();
    while (count < expected && stopwatch.elapsedMillis() < defaultTimeoutMs) {
      Thread.sleep(200);
      count = syncClient.listTabletServers().getTabletServersCount();
    }
    return count >= expected;
  }

  /**
   * @return the local PID of this process.
   * This is used to generate unique loopback IPs for parallel test running.
   */
  private static int getPid() {
    try {
      RuntimeMXBean runtime = ManagementFactory.getRuntimeMXBean();
      java.lang.reflect.Field jvm = runtime.getClass().getDeclaredField("jvm");
      jvm.setAccessible(true);
      VMManagement mgmt = (VMManagement)jvm.get(runtime);
      Method pid_method = mgmt.getClass().getDeclaredMethod("getProcessId");
      pid_method.setAccessible(true);

      return (Integer)pid_method.invoke(mgmt);
    } catch (Exception e) {
      LOG.warn("Cannot get PID", e);
      return 1;
    }
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

    int pid = getPid();
    return "127." + ((pid & 0xff00) >> 8) + "." + (pid & 0xff) + ".1";
  }

  /**
   * Starts a Kudu cluster composed of the provided masters and tablet servers.
   * @param numMasters how many masters to start
   * @param numTservers how many tablet servers to start
   * @throws Exception
   */
  private void startCluster(int numMasters, int numTservers) throws Exception {
    Preconditions.checkArgument(numMasters > 0, "Need at least one master");
    Preconditions.checkArgument(numTservers > 0, "Need at least one tablet server");
    // The following props are set via kudu-client's pom.
    String baseDirPath = TestUtils.getBaseDir();
    String localhost = getUniqueLocalhost();

    long now = System.currentTimeMillis();
    LOG.info("Starting {} masters...", numMasters);
    int port = startMasters(PORT_START, numMasters, baseDirPath);
    LOG.info("Starting {} tablet servers...", numTservers);
    for (int i = 0; i < numTservers; i++) {
      port = TestUtils.findFreePort(port);
      String dataDirPath = baseDirPath + "/ts-" + i + "-" + now;
      String flagsPath = TestUtils.getFlagsPath();
      String[] tsCmdLine = {
          TestUtils.findBinary("kudu-tserver"),
          "--flagfile=" + flagsPath,
          "--fs_wal_dir=" + dataDirPath,
          "--fs_data_dirs=" + dataDirPath,
          "--tserver_master_addrs=" + masterAddresses,
          "--webserver_interface=" + localhost,
          "--local_ip_for_outbound_sockets=" + localhost,
          "--rpc_bind_addresses=" + localhost + ":" + port};
      tserverProcesses.put(port, configureAndStartProcess(tsCmdLine));
      port++;

      if (flagsPath.startsWith(baseDirPath)) {
        // We made a temporary copy of the flags; delete them later.
        pathsToDelete.add(flagsPath);
      }
      pathsToDelete.add(dataDirPath);
    }
  }

  /**
   * Start the specified number of master servers with ports starting from a specified
   * number. Finds free web and RPC ports up front for all of the masters first, then
   * starts them on those ports, populating 'masters' map.
   * @param masterStartPort the starting point of the port range for the masters
   * @param numMasters number of masters to start
   * @param baseDirPath the base directory where the mini cluster stores its data
   * @return the next free port
   * @throws Exception if we are unable to start the masters
   */
  private int startMasters(int masterStartPort, int numMasters,
                          String baseDirPath) throws Exception {
    LOG.info("Starting {} masters...", numMasters);
    // Get the list of web and RPC ports to use for the master consensus configuration:
    // request NUM_MASTERS * 2 free ports as we want to also reserve the web
    // ports for the consensus configuration.
    String localhost = getUniqueLocalhost();
    List<Integer> ports = TestUtils.findFreePorts(masterStartPort, numMasters * 2);
    int lastFreePort = ports.get(ports.size() - 1);
    List<Integer> masterRpcPorts = Lists.newArrayListWithCapacity(numMasters);
    List<Integer> masterWebPorts = Lists.newArrayListWithCapacity(numMasters);
    for (int i = 0; i < numMasters * 2; i++) {
      if (i % 2 == 0) {
        masterRpcPorts.add(ports.get(i));
        masterHostPorts.add(HostAndPort.fromParts(localhost, ports.get(i)));
      } else {
        masterWebPorts.add(ports.get(i));
      }
    }
    masterAddresses = NetUtil.hostsAndPortsToString(masterHostPorts);
    for (int i = 0; i < numMasters; i++) {
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
          TestUtils.findBinary("kudu-master"),
          "--flagfile=" + flagsPath,
          "--fs_wal_dir=" + dataDirPath,
          "--fs_data_dirs=" + dataDirPath,
          "--webserver_interface=" + localhost,
          "--local_ip_for_outbound_sockets=" + localhost,
          "--rpc_bind_addresses=" + localhost + ":" + masterRpcPorts.get(i),
          "--webserver_port=" + masterWebPorts.get(i));
      if (numMasters > 1) {
        masterCmdLine.add("--master_addresses=" + masterAddresses);
      }
      masterProcesses.put(masterRpcPorts.get(i),
          configureAndStartProcess(masterCmdLine.toArray(new String[masterCmdLine.size()])));

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
   * @param command Process and options
   * @return The started process
   * @throws Exception Exception if an error prevents us from starting the process,
   * or if we were able to start the process but noticed that it was then killed (in which case
   * we'll log the exit value).
   */
  private Process configureAndStartProcess(String[] command) throws Exception {
    LOG.info("Starting process: {}", Joiner.on(" ").join(command));
    ProcessBuilder processBuilder = new ProcessBuilder(command);
    processBuilder.redirectErrorStream(true);
    Process proc = processBuilder.start();
    ProcessInputStreamLogPrinterRunnable printer =
        new ProcessInputStreamLogPrinterRunnable(proc.getInputStream());
    Thread thread = new Thread(printer);
    thread.setDaemon(true);
    thread.setName(command[0]);
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
    return proc;
  }

  /**
   * Kills the TS listening on the provided port. Doesn't do anything if the TS was already killed.
   * @param port port on which the tablet server is listening on
   * @throws InterruptedException
   */
  public void killTabletServerOnPort(int port) throws InterruptedException {
    Process ts = tserverProcesses.remove(port);
    if (ts == null) {
      // The TS is already dead, good.
      return;
    }
    LOG.info("Killing server at port " + port);
    ts.destroy();
    ts.waitFor();
  }

  /**
   * Kills the master listening on the provided port. Doesn't do anything if the master was
   * already killed.
   * @param port port on which the master is listening on
   * @throws InterruptedException
   */
  public void killMasterOnPort(int port) throws InterruptedException {
    Process master = masterProcesses.remove(port);
    if (master == null) {
      // The master is already dead, good.
      return;
    }
    LOG.info("Killing master at port " + port);
    master.destroy();
    master.waitFor();
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
   * Stops all the processes and deletes the folders used to store data and the flagfile.
   */
  public void shutdown() {
    for (Iterator<Process> masterIter = masterProcesses.values().iterator(); masterIter.hasNext(); ) {
      masterIter.next().destroy();
      masterIter.remove();
    }
    for (Iterator<Process> tsIter = tserverProcesses.values().iterator(); tsIter.hasNext(); ) {
      tsIter.next().destroy();
      tsIter.remove();
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

  public static class MiniKuduClusterBuilder {

    private int numMasters = 1;
    private int numTservers = 3;
    private int defaultTimeoutMs = 50000;

    public MiniKuduClusterBuilder numMasters(int numMasters) {
      this.numMasters = numMasters;
      return this;
    }

    public MiniKuduClusterBuilder numTservers(int numTservers) {
      this.numTservers = numTservers;
      return this;
    }

    /**
     * Configures the internal client to use the given timeout for all operations. Also uses the
     * timeout for tasks like waiting for tablet servers to check in with the master.
     * @param defaultTimeoutMs timeout in milliseconds
     * @return this instance
     */
    public MiniKuduClusterBuilder defaultTimeoutMs(int defaultTimeoutMs) {
      this.defaultTimeoutMs = defaultTimeoutMs;
      return this;
    }

    public MiniKuduCluster build() throws Exception {
      return new MiniKuduCluster(numMasters, numTservers, defaultTimeoutMs);
    }
  }

}
