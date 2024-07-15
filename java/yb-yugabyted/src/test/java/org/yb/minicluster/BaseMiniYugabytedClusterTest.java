package org.yb.minicluster;

import java.util.List;
import java.util.Map;
import java.util.TreeMap;

import org.junit.Before;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.BaseYBTest;
import org.yb.client.TestUtils;
import org.yb.util.Timeouts;

import com.google.common.net.HostAndPort;

public class BaseMiniYugabytedClusterTest extends BaseYBTest {

    private static final Logger LOG = LoggerFactory.getLogger(BaseMiniYugabytedClusterTest.class);

    /** Default timeout used when calling YB Java client's async API. */
    protected static final int DEFAULT_SLEEP = (int) Timeouts.adjustTimeoutSecForBuildType(50000);

    /**
     * Whether the fresh cluster should be created for the next test, so that custom
     * {@code createMiniCluster} won't affect other tests.
     */
    private static boolean clusterNeedsRecreation = false;

    /**
     * A mini-cluster shared between invocations of multiple test methods.
     */
    protected static MiniYugabytedCluster miniYugabytedCluster;

    protected static MiniYugabytedClusterParameters clusterParameters;

    protected static List<MiniYugabytedNodeConfigurations> clusterConfigurations;

    /**
     * Comma separate describing the master addresses and ports.
     */
    protected static String masterAddresses;

    protected static List<HostAndPort> masterHostPorts;

    protected String certFile = null;

    // The client cert files for mTLS.
    protected String clientCertFile = null;
    protected String clientKeyFile = null;

    /** Default bind address (Used only for mTLS verification). */
    protected String clientHost = null;
    protected int clientPort = 0;

    @Before
    public void setUpBefore() throws Exception {
        resetSettings();
        if (!isMiniClusterEnabled()) {
            return;
        }
        TestUtils.clearReservedPorts();
        if (clusterNeedsRecreation) {
            destroyMiniCluster();
            clusterNeedsRecreation = false;
        }
        if (miniYugabytedCluster == null) {
            createMiniCluster(clusterParameters.numNodes);
        } else if (shouldRestartMiniClusterBetweenTests()) {
            LOG.info("Restarting the MiniCluster");
            miniYugabytedCluster.restart();
        }
    }

    protected final void createMiniCluster(int numNodes) throws Exception {
        createMiniYugabytedCluster(numNodes);
    }

    protected final void createMiniYugabytedCluster(int numNodes) throws Exception {

        if (!isMiniClusterEnabled()) {
            return;
        }

        final int replicationFactor = getReplicationFactor();

        // miniYugabytedCluster = new MiniYugabytedCluster(clusterParameters,
        // getMasterFlags(), getMasterFlags(), getTServerFlags(),
        // null, getMasterFlags(), clientHost, certFile,
        // clientCertFile, clientKeyFile);

        // TO-DO: replace all nulls with the actual fields.
        miniYugabytedCluster = new MiniYugabytedCluster(
            clusterParameters,
            clusterConfigurations
        );

        masterAddresses = miniYugabytedCluster.getMasterAddresses();
        masterHostPorts = miniYugabytedCluster.getMasterHostPorts();

        afterStartingMiniCluster();

    }

    protected int getReplicationFactor() {
        return -1;
    }

    protected void resetSettings() {
    }

    /**
     * This allows subclasses to optionally skip the usage of a mini-cluster in a
     * test.
     */
    protected boolean isMiniClusterEnabled() {
        return true;
    }

    protected static void destroyMiniCluster() throws Exception {
        if (miniYugabytedCluster != null) {
            LOG.info("Destroying mini cluster");
            miniYugabytedCluster.shutdown();
            miniYugabytedCluster = null;
        }
    }

    protected boolean shouldRestartMiniClusterBetweenTests() {
        return false;
    }

    /**
     * This is called every time right after starting a mini cluster.
     */
    protected void afterStartingMiniCluster() throws Exception {
    }

    protected Map<String, String> getYugabytedFlags() {
        return new TreeMap<>();
    }

    protected Map<String, String> getMasterFlags() {
        return new TreeMap<>();
    }

    protected Map<String, String> getTserverFlags() {
        return new TreeMap<>();
    }

}
