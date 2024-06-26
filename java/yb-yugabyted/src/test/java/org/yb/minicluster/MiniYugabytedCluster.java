package org.yb.minicluster;

import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.io.PrintWriter;
import java.net.InetAddress;
import java.nio.file.FileSystems;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Random;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ConcurrentSkipListSet;
import java.util.concurrent.atomic.AtomicInteger;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.TestUtils;
import org.yb.util.BindIpUtil;
import org.yb.util.BuildTypeUtil;
import org.yb.util.CommandUtil;
import org.yb.util.NetUtil;
import org.yb.util.ProcessUtil;
import org.yb.util.RandomUtil;
import org.yb.util.SystemUtil;

import com.google.common.base.Joiner;
import com.google.common.base.Preconditions;
import com.google.common.collect.Lists;
import com.google.common.net.HostAndPort;
import com.google.gson.Gson;
import com.google.gson.GsonBuilder;

public class MiniYugabytedCluster implements AutoCloseable {

    private static final Logger LOG = LoggerFactory.getLogger(MiniYugabytedCluster.class);
    private final int MIN_LAST_IP_BYTE = 2;
    private final int MAX_LAST_IP_BYTE = 254;

    private final MiniYugabytedClusterParameters clusterParameters;
    private final List<HostAndPort> yugabytedMasterHostAndPorts = new ArrayList<>();

    // Map of host/port pairs to master servers.
    private final Map<HostAndPort, MiniYBDaemon> yugabytedProcesses = new ConcurrentHashMap<>();

    /**
     * We pass the Java test class name as a command line option to YB daemons so
     * that we know what
     * test invoked them if they get stuck.
     */
    private final String testClassName;

    // The client cert files for mTLS.
    private String certFile = null;
    private String clientCertFile = null;
    private String clientKeyFile = null;

    // This is used as the default bind address (Used only for mTLS verification).
    private String clientHost = null;
    private int clientPort = 0;

    /**
     * This is used to prevent trying to launch two daemons on the same loopback IP.
     * However, this
     * only works with the same test process.
     */
    private static final ConcurrentSkipListSet<String> usedBindIPs = new ConcurrentSkipListSet<>();

    /**
     * CQL port needs to be same for all nodes, since CQL clients use a configured
     * port to generate
     * a host:port pair for each node given just the host.
     */
    private static final int CQL_PORT = 9042;
    private static final int REDIS_PORT = 6379;

    /**
     * When picking an IP address for a tablet server to use, we check that we can
     * bind to these
     * ports.
     */
    private static final int[] TSERVER_CLIENT_FIXED_API_PORTS = new int[] { CQL_PORT,
            REDIS_PORT };

    /**
     * Hard memory limit for YB daemons. This should be consistent with the memory
     * limit set for C++
     * based mini clusters in external_mini_cluster.cc.
     */
    private static final long DAEMON_MEMORY_LIMIT_HARD_BYTES_NON_TSAN = 1024 * 1024 * 1024;
    private static final long DAEMON_MEMORY_LIMIT_HARD_BYTES_TSAN = 512 * 1024 * 1024;

    private static final String YSQL_SNAPSHOTS_DIR = "/opt/yb-build/ysql-sys-catalog-snapshots";

    private static final String YUGABYTED_ADVERTISE_ADDR_FLAG = "--advertise_address";
    private static final String YUGABYTED_MASTER_GFLAGS = "--master_flags=";
    private static final String YUGABYTED_TSERVER_GFLAGS = "--tserver_flags=";
    private static final String YUGABYTED_MASTER_RPC_PORT = "master_rpc_port";
    private static final String YUGABYTED_MASTER_WEB_PORT = "master_webserver_port";
    private static final String YUGABYTED_TSERVER_RPC_PORT = "tserver_rpc_port";
    private static final String YUGABYTED_TSERVER_WEB_PORT = "tserver_webserver_port";
    private static final String YUGABYTED_YCQL_PORT = "ycql_port";
    private static final String YUGABYTED_YSQL_PORT = "ysql_port";
    private static final String YUGABYTED_CALLHOME = "callhome";

    // These are used to assign master/tserver indexes used in the logs (the "m1",
    // "ts2", etc.
    // prefixes).
    private AtomicInteger nextYugabytedIndex = new AtomicInteger(0);

    // List of threads that print log messages.
    private final List<LogPrinter> logPrinters = new ArrayList<>();

    // state variables
    private String ybdAdvertiseAddresses;
    private String masterAddresses;

    private static class YugabytedMasterHostPortAllocation {

        final String bindAddress;
        final int rpcPort;
        final int webPort;

        public YugabytedMasterHostPortAllocation(String bindAddress, int rpcPort, int webPort) {
            this.bindAddress = bindAddress;
            this.rpcPort = rpcPort;
            this.webPort = webPort;
        }
    }

    public MiniYugabytedCluster(MiniYugabytedClusterParameters clusterParameters,
            Map<String, String> yugabytedFlags,
            Map<String, String> yugabytedAdvancedFlags,
            Map<String, String> masterFlags,
            Map<String, String> commonTserverFlags,
            List<Map<String, String>> perTserverFlags,
            Map<String, String> tserverEnvVars,
            String testClassName,
            String certFile,
            String clientCertFile,
            String clientKeyFile) throws Exception {

        this.clusterParameters = clusterParameters;
        this.testClassName = testClassName;
        this.certFile = certFile;
        this.clientCertFile = clientCertFile;
        this.clientKeyFile = clientKeyFile;
        if (clusterParameters.pgTransactionsEnabled && !clusterParameters.startYsqlProxy) {
            throw new AssertionError(
                    "Attempting to enable YSQL transactions without enabling YSQL API");
        }
        this.clientHost = clientHost;
        this.clientPort = clientPort;

        startCluster(
                clusterParameters.numNodes, yugabytedFlags, yugabytedAdvancedFlags,
                masterFlags, commonTserverFlags, perTserverFlags, tserverEnvVars);

    }

    @Override
    public void close() throws Exception {
        throw new UnsupportedOperationException("Unimplemented method 'close'");
    }

    public void restart() throws Exception {
        // TO-DO: Restart
        // restart(true /* waitForMasterLeader */);
    }

    public void shutdown() throws Exception {
    }

    public Map<HostAndPort, MiniYBDaemon> getYugabytedNodes() {
        return yugabytedProcesses;
    }

    /**
     * Returns the comma-separated list of master addresses.
     *
     * @return master addresses
     */
    public String getMasterAddresses() {
        return masterAddresses;
    }

    /**
     * Returns a list of master addresses.
     *
     * @return master addresses
     */
    public List<HostAndPort> getMasterHostPorts() {
        return yugabytedMasterHostAndPorts;
    }

    /**
     * Kills the TS listening on the provided port. Doesn't do anything if the TS
     * was already killed.
     *
     * @param hostPort
     *            host and port on which the tablet server is listening on
     * @throws InterruptedException
     */
    public void killTabletServerOnHostPort(HostAndPort hostPort) throws Exception {
    }

    /**
     * Kills the master listening on the provided host/port combination. Doesn't do
     * anything if the
     * master has already been killed.
     *
     * @param hostAndPort
     *            host/port the master is listening on
     * @throws InterruptedException
     */
    public void killMasterOnHostPort(HostAndPort hostAndPort) throws Exception {

    }

    /**
     * Read the environment variables used for configuring gflags.
     *
     * @param dest
     * @param envVarName
     * @return
     */
    private static void addFlagsFromEnv(List<String> dest, String envVarName) {
        final String extraFlagsFromEnv = System.getenv(envVarName);
        if (extraFlagsFromEnv != null) {
            // TODO: this has an issue with handling quoted arguments with embedded spaces.
            for (String flag : extraFlagsFromEnv.split("\\s+")) {
                dest.add(flag);
            }
        }
    }

    private void startCluster(int numNodes,
            Map<String, String> yugabytedFlags,
            Map<String, String> yugabyatedAdvancedFlags,
            Map<String, String> masterFlags,
            Map<String, String> commonTserverFlags,
            List<Map<String, String>> perTserverFlags,
            Map<String, String> tserverEnvVars) throws Exception {

        Preconditions.checkArgument(numNodes > 0, "Need at least one yugabyted node.");
        // Preconditions.checkNotNull(yugabytedFlags);
        if (perTserverFlags != null &&
                !perTserverFlags.isEmpty() && perTserverFlags.size() != numNodes) {
            throw new AssertionError("numTservers=" + numNodes + " but (perTServerArgs has " +
                    perTserverFlags.size() + " elements");
        }

        for (String envVarName : new String[] { "ASAN_OPTIONS", "TSAN_OPTIONS",
                "LSAN_OPTIONS", "UBSAN_OPTIONS", "ASAN_SYMBOLIZER_PATH" }) {
            String envVarValue = System.getenv(envVarName);
            LOG.info("Environment variable " + envVarName + ": " +
                    (envVarValue == null ? "not set" : envVarValue));
        }

        if (clusterParameters.startYsqlProxy) {
            applyYsqlSnapshot(clusterParameters.ysqlSnapshotVersion, masterFlags);
        }

        LOG.info("Using yugabyted to start YugabyteDB cluster with {} nodes...", numNodes);

        startYugabytedNode(numNodes, "~/node1", yugabytedFlags, yugabyatedAdvancedFlags,
                masterFlags, commonTserverFlags, perTserverFlags, tserverEnvVars);

    }

    private void startYugabytedNode(int numNodes,
            String baseDirPath,
            Map<String, String> yugabytedFlags,
            Map<String, String> yugabytedAdvancedFlags,
            Map<String, String> masterFlags,
            Map<String, String> commonTserverFlags,
            List<Map<String, String>> perTserverFlags,
            Map<String, String> tserverEnvVars) throws Exception {

        assert (yugabytedMasterHostAndPorts.isEmpty());

        /**
         * Get the list of web and RPC ports to use for the master consensus
         * configuration:
         * request NUM_MASTERS * 2 free ports as we want to also reserve the web
         * ports for the consensus configuration.
         */
        List<YugabytedMasterHostPortAllocation> yugabytedReservedHostPort = reserveHostAndPort(
                numNodes);

        updateYbdAdvertiseAddress();

        int perTserverFlagsCounter = 0;
        for (YugabytedMasterHostPortAllocation hostAlloc : yugabytedReservedHostPort) {
            final String yugabytedAdvertiseAddress = hostAlloc.bindAddress;
            final int yugabytedMasterRpcPort = hostAlloc.rpcPort;
            final long now = System.currentTimeMillis();
            baseDirPath = baseDirPath + "-" + yugabytedAdvertiseAddress + "-" + now;
            String flagsPath = YugabytedTestUtils.getFlagsPath();

            // Determine the list of all the arguments provided to
            // yugabyted CLI based of the other flags - masterFlags, commonTserverFlags,
            // perTserverFlags etc.

            List<String> yugabytedArgs = getCommonYugabytedCmdLine(flagsPath,
                    baseDirPath, yugabytedAdvertiseAddress);

            if (yugabytedFlags != null && yugabytedFlags.size() > 0) {
                yugabytedArgs.addAll(CommandUtil.flagsToArgs(yugabytedFlags));
            }

            if (masterFlags != null && masterFlags.size() > 0) {
                getMasterFlagsAsYugabytedArg(yugabytedArgs, masterFlags);
            }

            getTserverFlagsAsYugabytedArg(yugabytedArgs,
                    commonTserverFlags, perTserverFlags, perTserverFlagsCounter);

            // if (commonTserverFlags != null && commonTserverFlags.size() > 0) {
            // if (perTserverFlags != null &&
            // perTserverFlags.get(perTserverFlagsCounter).size() > 0) {
            // getTserverFlagsAsYugabytedArg(yugabytedArgs,
            // commonTserverFlags, perTserverFlags.get(perTserverFlagsCounter));
            // } else {
            // getTserverFlagsAsYugabytedArg(yugabytedArgs,
            // commonTserverFlags);
            // }
            // } else if (perTserverFlags != null &&
            // perTserverFlags.get(perTserverFlagsCounter).size() > 0) {
            // getTserverFlagsAsYugabytedArg(yugabytedArgs,
            // perTserverFlags.get(perTserverFlagsCounter));
            // }

            Map<String, Object> yugabytedConfArgs = generateAndPopulateYugabytedConfigFile(
                    yugabytedAdvertiseAddress, yugabytedMasterRpcPort, hostAlloc.webPort,
                    flagsPath, yugabytedAdvancedFlags);

            // generate the webserver and API ports and set the yugabyted config file.

            final HostAndPort yugabytedNodeHostPort = HostAndPort.fromParts(
                    yugabytedAdvertiseAddress, yugabytedMasterRpcPort);

            String[] yugabytedStartCommand = new String[yugabytedArgs.size()];
            yugabytedStartCommand = yugabytedArgs.toArray(yugabytedStartCommand);
            yugabytedProcesses.put(yugabytedNodeHostPort,
                    configureAndStartProcess(MiniYBDaemonType.YUGABYTED,
                            yugabytedStartCommand, flagsPath, baseDirPath,
                            tserverEnvVars, yugabytedConfArgs));

            if (perTserverFlagsCounter < numNodes)
                perTserverFlagsCounter++;

        }
    }

    private void updateYbdAdvertiseAddress() throws IOException {

        ybdAdvertiseAddresses = NetUtil.hostsAndPortsToString(yugabytedMasterHostAndPorts);
        masterAddresses = ybdAdvertiseAddresses;
        Path flagsFile = Paths.get(YugabytedTestUtils.getFlagsPath());
        String content = new String(Files.readAllBytes(flagsFile));
        LOG.info("Retrieved flags file content: " + content);
        String yugabytedConfigFlags = String.format("%s=%s", YUGABYTED_ADVERTISE_ADDR_FLAG,
                ybdAdvertiseAddresses);
        if (content.contains(YUGABYTED_ADVERTISE_ADDR_FLAG)) {
            content = content.replaceAll(YUGABYTED_ADVERTISE_ADDR_FLAG, yugabytedConfigFlags);
        } else {
            content += yugabytedConfigFlags + "\n";
        }
        Files.write(flagsFile, content.getBytes());
        LOG.info("Wrote flags file content: " + content);
    }

    private List<YugabytedMasterHostPortAllocation> reserveHostAndPort(int numNodes)
            throws Exception {

        final List<YugabytedMasterHostPortAllocation> yugabytedHostPortAllocList =
                                                                        new ArrayList<>();
        for (int i = 0; i < numNodes; ++i) {
            final String yugabytedBindAddress = getYugabytedBindAddress();
            final int rpcPort = TestUtils.findFreePort(yugabytedBindAddress);
            // final String yugabytedBindAddress = "127.0.0.1";
            // final int rpcPort = 7100;
            final int webPort = TestUtils.findFreePort(yugabytedBindAddress);
            yugabytedHostPortAllocList.add(
                    new YugabytedMasterHostPortAllocation(yugabytedBindAddress, rpcPort, webPort));
            yugabytedMasterHostAndPorts.add(HostAndPort.fromParts(yugabytedBindAddress, rpcPort));
        }
        return yugabytedHostPortAllocList;
    }

    private MiniYBDaemon configureAndStartProcess(MiniYBDaemonType type,
            String[] command,
            String bindIp,
            String dataDirPath,
            Map<String, String> environment,
            Map<String, Object> yugabytedConfArgs) throws Exception {

        command[0] = FileSystems.getDefault().getPath(command[0]).normalize().toString();
        final int indexForLog = nextYugabytedIndex.incrementAndGet();

        // {
        // List<String> args = new ArrayList<>();
        // args.addAll(Arrays.asList(command));
        // String fatalDetailsPathPrefix =
        // System.getenv("YB_FATAL_DETAILS_PATH_PREFIX");
        // if (fatalDetailsPathPrefix == null) {
        // fatalDetailsPathPrefix = TestUtils.getTestReportFilePrefix() +
        // "fatal_failure_details";
        // }
        // fatalDetailsPathPrefix += "." + type.shortStr() + "-" + indexForLog + "." +
        // bindIp + "-";
        // args.add("--fatal_details_path_prefix=" + fatalDetailsPathPrefix);
        // // to-do: add it back in after collating of all the required gflags
        // // args.addAll(getCommonDaemonFlags());
        // command = args.toArray(command);
        // }

        // {

        ProcessBuilder procBuilder = new ProcessBuilder(command).redirectErrorStream(true);
        String envString = "{}";
        if (environment != null) {
            procBuilder.environment().putAll(environment);
            envString = environment.toString();
        }
        LOG.info("Starting process: {} with environment {}",
                Joiner.on(" ").join(command), envString);
        Process proc = procBuilder.start();
        final MiniYBDaemon daemon = new MiniYBDaemon(type, indexForLog, command,
                proc, bindIp, (int) yugabytedConfArgs.get(YUGABYTED_MASTER_RPC_PORT),
                (int) yugabytedConfArgs.get(YUGABYTED_MASTER_WEB_PORT),
                (int) yugabytedConfArgs.get(YUGABYTED_YCQL_PORT),
                (int) yugabytedConfArgs.get(YUGABYTED_YSQL_PORT),
                0, dataDirPath);
        logPrinters.add(daemon.getLogPrinter());

        Thread.sleep(300);
        try {
            int ev = proc.exitValue();
            throw new Exception(
                    "We tried starting a process (" + command[0] + ") but it exited with " +
                            "value=" + ev + (daemon.getLogPrinter().getError() == null ? ""
                                    : ", error: " + daemon.getLogPrinter().getError()));
        } catch (IllegalThreadStateException ex) {
            // This means the process is still alive, it's like reverse psychology.
        }

        LOG.info("Started " + command[0] + " as pid " + ProcessUtil.pidOfProcess(proc));

        return daemon;
    }

    /** Common flags for both master and tserver processes */
    private List<String> getCommonDaemonFlags() {
        final List<String> commonFlags = Lists.newArrayList(
                // Ensure that logging goes to the test output and doesn't get buffered.
                "--logtostderr",
                "--logbuflevel=-1",
                "--webserver_doc_root=" + TestUtils.getWebserverDocRoot());
        addFlagsFromEnv(commonFlags, "YB_EXTRA_DAEMON_FLAGS");
        if (testClassName != null) {
            commonFlags.add("--yb_test_name=" + testClassName);
        }

        final long memoryLimit = BuildTypeUtil.nonTsanVsTsan(
                DAEMON_MEMORY_LIMIT_HARD_BYTES_NON_TSAN,
                DAEMON_MEMORY_LIMIT_HARD_BYTES_TSAN);
        commonFlags.add("--memory_limit_hard_bytes=" + memoryLimit);

        // YB_TEST_INVOCATION_ID is a special environment variable that we use to
        // force-kill all
        // processes even if MiniYBCluster fails to kill them.
        String testInvocationId = System.getenv("YB_TEST_INVOCATION_ID");
        if (testInvocationId != null) {
            // We use --metric_node_name=... to include a unique "test invocation id" into
            // the command
            // line so we can kill any stray processes later. --metric_node_name is normally
            // how we pass
            // the Universe ID to the cluster. We could use any other flag that is present
            // in yb-master
            // and yb-tserver for this.
            commonFlags.add("--metric_node_name=" + testInvocationId);
        }

        commonFlags.add("--yb_num_shards_per_tserver=" + clusterParameters.numShardsPerTServer);
        commonFlags.add("--ysql_num_shards_per_tserver=" + clusterParameters.numShardsPerTServer);
        commonFlags.add("--enable_ysql=" + clusterParameters.startYsqlProxy);

        return commonFlags;
    }

    /**
     * Returns the common options among regular masters and shell masters.
     *
     * @return a list of command line options
     */
    private List<String> getCommonYugabytedCmdLine(String flagsPath, String baseDirPath,
            String yugabytedAdvertiseAddr) throws Exception {
        List<String> yugabytedCmdLine = Lists.newArrayList(
                YugabytedTestUtils.findYugabytedBinary("yugabyted"),
                "start",
                "--ui=" + clusterParameters.getEnableYugabytedUI(),
                "--config=" + flagsPath,
                "--base_dir=" + baseDirPath,
                "--advertise_address=" + yugabytedAdvertiseAddr);

        return yugabytedCmdLine;
    }

    private void getMasterFlagsAsYugabytedArg(List<String> yugabytedFlags,
            Map<String, String> masterFlags) {

        List<String> masterFlagsList = new ArrayList<String>();
        String masterFlagsString = YUGABYTED_MASTER_GFLAGS;

        masterFlagsList.addAll(CommandUtil.flagsToArgs(masterFlags));
        for (String flag : masterFlagsList) {
            masterFlagsString += flag + ",";
        }
        yugabytedFlags.add(masterFlagsString);

    }

    private void getTserverFlagsAsYugabytedArg(List<String> yugabytedFlags,
            Map<String, String> tserverFlags, List<Map<String, String>> perTserverFlags,
            int perTserverFlagsCounter) {

        List<String> tserverFlagsList = new ArrayList<String>();
        String tserverFlagsString = YUGABYTED_TSERVER_GFLAGS;

        if (tserverFlags != null && tserverFlags.size() > 0) {
            tserverFlagsList.addAll(CommandUtil.flagsToArgs(tserverFlags));
        }

        if (perTserverFlags != null && perTserverFlags.size() != 0 &&
                perTserverFlags.size() > perTserverFlagsCounter) {
            tserverFlagsList
                    .addAll(CommandUtil.flagsToArgs(perTserverFlags.get(perTserverFlagsCounter)));
        }

        if (tserverFlagsList.size() > 0) {
            for (String flag : tserverFlagsList) {
                tserverFlagsString += flag + ",";
            }
            yugabytedFlags.add(tserverFlagsString);
        }

    }

    private Map<String, Object> generateAndPopulateYugabytedConfigFile(String advertiseAddress,
            int masterRpcPort, int masterWebPort, String flagsFilePath,
            Map<String, String> yugabyatedAdvancedFlags) throws IOException {

        final int tserverRpcPort = TestUtils.findFreePort(advertiseAddress);
        final int tserverWebPort = TestUtils.findFreePort(advertiseAddress);
        final int ycqlPort = TestUtils.findFreePort(advertiseAddress);
        final int ysqlPort = TestUtils.findFreePort(advertiseAddress);

        Map<String, Object> customFlags = new HashMap<String, Object>();
        customFlags.put(YUGABYTED_MASTER_RPC_PORT, masterRpcPort);
        customFlags.put(YUGABYTED_MASTER_WEB_PORT, masterWebPort);
        customFlags.put(YUGABYTED_TSERVER_RPC_PORT, tserverRpcPort);
        customFlags.put(YUGABYTED_TSERVER_WEB_PORT, tserverWebPort);
        customFlags.put(YUGABYTED_YCQL_PORT, ycqlPort);
        customFlags.put(YUGABYTED_YSQL_PORT, ysqlPort);
        customFlags.put(YUGABYTED_CALLHOME, "false");

        if (yugabyatedAdvancedFlags != null && yugabyatedAdvancedFlags.size() > 0) {
            customFlags.putAll(yugabyatedAdvancedFlags);
        }

        PrintWriter writer = new PrintWriter(new FileWriter(flagsFilePath));
        Gson gson = new GsonBuilder()
                .setPrettyPrinting()
                .create();
        String customFlagsString = gson.toJson(customFlags);
        writer.write(customFlagsString);
        writer.flush();
        writer.close();

        return customFlags;
    }

    private String getYugabytedBindAddress() throws IOException {
        return getDaemonBindAddress(MiniYBDaemonType.YUGABYTED);
    }

    private String getDaemonBindAddress(MiniYBDaemonType daemonType) throws IOException {
        if (SystemUtil.IS_LINUX && !clusterParameters.useIpWithCertificate) {
            return pickFreeRandomBindIpOnLinux(daemonType);
        }

        return pickFreeBindIpOnMacOrWithCertificate(daemonType);
    }

    private String pickFreeRandomBindIpOnLinux(MiniYBDaemonType daemonType)
            throws IOException {
        final int MAX_NUM_ATTEMPTS = 1000;
        for (int i = 1; i <= MAX_NUM_ATTEMPTS; ++i) {
            String randomBindAddress = getRandomBindAddressOnLinux();
            if (canUseBindIP(randomBindAddress, i == MAX_NUM_ATTEMPTS)) {
                return randomBindAddress;
            }
        }
        throw new IOException("Could not find a loopback IP of the form 127.x.y.z for a " +
                daemonType.humanReadableName() + " in " + MAX_NUM_ATTEMPTS + " attempts");
    }

    private String pickFreeBindIpOnMacOrWithCertificate(
            MiniYBDaemonType daemonType) throws IOException {
        List<String> bindIps = new ArrayList<>();

        final int nextToLastByteMin = 0;
        // If we need an IP with a certificate, use 127.0.0.*, otherwise use 127.0.x.y
        // with a small
        // range of x.
        final int nextToLastByteMax = clusterParameters.useIpWithCertificate ? 0 : 3;

        if (SystemUtil.IS_LINUX) {
            // We only use even last bytes of the loopback IP in case we are testing TLS
            // encryption.
            final int lastIpByteStep = clusterParameters.useIpWithCertificate ? 2 : 1;
            for (int nextToLastByte = nextToLastByteMin;
                        nextToLastByte <= nextToLastByteMax;
                            ++nextToLastByte) {
                for (int lastIpByte = MIN_LAST_IP_BYTE;
                            lastIpByte <= MAX_LAST_IP_BYTE;
                                lastIpByte += lastIpByteStep) {
                    String bindIp = getLoopbackIpWithLastTwoBytes(nextToLastByte, lastIpByte);
                    if (!usedBindIPs.contains(bindIp)) {
                        bindIps.add(bindIp);
                    }
                }
            }
        } else {
            List<String> loopbackIps = BindIpUtil.getLoopbackIPs();
            if (clusterParameters.useIpWithCertificate) {
                // macOS, but we need a 127.0.0.x, where x is even.
                for (String loopbackIp : loopbackIps) {
                    if (loopbackIp.startsWith("127.0.0.")) {
                        String[] components = loopbackIp.split("[.]");
                        int lastIpByte = Integer.valueOf(components[components.length - 1]);
                        if (lastIpByte >= MIN_LAST_IP_BYTE &&
                                lastIpByte <= MAX_LAST_IP_BYTE &&
                                lastIpByte % 2 == 0) {
                            bindIps.add(loopbackIp);
                        }
                    }
                }
            } else {
                // macOS, no requirement that there is a certificate.
                bindIps = loopbackIps;
            }
        }

        Collections.shuffle(bindIps, RandomUtil.getRandomGenerator());

        for (int i = bindIps.size() - 1; i >= 0; --i) {
            String bindAddress = bindIps.get(i);
            if (canUseBindIP(bindAddress, i == 0)) {
                return bindAddress;
            }
        }

        Collections.sort(bindIps);
        throw new IOException(String.format(
                "Cannot find a loopback IP of the form 127.0.x.y to launch a %s on. " +
                        "Considered options: %s.",
                daemonType.humanReadableName(),
                bindIps));
    }

    /**
     * @return the string representation of a random localhost IP.
     */
    private String getRandomBindAddressOnLinux() throws IllegalArgumentException {
        assert (SystemUtil.IS_LINUX);
        // On Linux we can use 127.x.y.z, so let's just pick a random address.
        final StringBuilder randomLoopbackIp = new StringBuilder("127");
        final Random rng = RandomUtil.getRandomGenerator();
        for (int i = 0; i < 3; ++i) {
            // Do not use 0 or 255 for IP components.
            randomLoopbackIp.append("." + (1 + rng.nextInt(254)));
        }
        return randomLoopbackIp.toString();
    }

    private String getLoopbackIpWithLastTwoBytes(int nextToLastByte, int lastByte) {
        return "127.0." + nextToLastByte + "." + lastByte;
    }

    private boolean canUseBindIP(String bindIP, boolean logException) throws IOException {
        if (usedBindIPs.contains(bindIP)) {
            // We are using this bind IP for a daemon already.
            return false;
        }

        final InetAddress bindIp = InetAddress.getByName(bindIP);
        for (int clientApiPort : TSERVER_CLIENT_FIXED_API_PORTS) {
            if (!TestUtils.isPortFree(bindIp, clientApiPort, logException)) {
                // One of the ports we need to be free is not free, reject this IP address.
                return false;
            }
        }

        // All ports we care about are free, let's try to use this IP address.
        return usedBindIPs.add(bindIP);
    }

    private String getYsqlSnapshotFilePath(YsqlSnapshotVersion ver) {
        String filenamePrefix = "initial_sys_catalog_snapshot_";
        String filename;
        switch (ver) {
            case EARLIEST:
                filename = filenamePrefix + "2.0.9.0";
                break;
            case LATEST:
                throw new IllegalArgumentException("LATEST snapshot does not need a custom path");
            default:
                throw new IllegalArgumentException("Unknown snapshot version: " + ver);
        }
        filename = filename + "_" + (BuildTypeUtil.isRelease() ? "release" : "debug");
        File file = new File(YSQL_SNAPSHOTS_DIR, filename);
        Preconditions.checkState(file.exists(),
                "Snapshot %s is not found in %s, should've been downloaded by the build script!",
                filename, YSQL_SNAPSHOTS_DIR);
        return file.getAbsolutePath();
    }

    private void applyYsqlSnapshot(YsqlSnapshotVersion ver, Map<String, String> masterFlags) {
        // No need to set the flag for LATEST snapshot.
        if (ver != YsqlSnapshotVersion.LATEST) {
            String snapshotPath = getYsqlSnapshotFilePath(ver);
            masterFlags.put("initial_sys_catalog_snapshot_path", snapshotPath);
        }
    }

}
