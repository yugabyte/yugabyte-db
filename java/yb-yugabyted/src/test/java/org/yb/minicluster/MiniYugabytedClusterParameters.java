package org.yb.minicluster;

import java.util.Optional;

public class MiniYugabytedClusterParameters {

    // public static final int DEFAULT_NUM_MASTERS = 3;
    // public static final int DEFAULT_NUM_TSERVERS = 3;
    public static final int DEFUALT_NUM_NODES = 1;
    public static final int DEFAULT_NUM_SHARDS_PER_TSERVER = 3;
    public static boolean DEFAULT_USE_IP_WITH_CERTIFICATE = false;

    public static final int DEFAULT_ADMIN_OPERATION_TIMEOUT_MS = 90000;

    // public int numMasters = 1;
    // public int numTservers = DEFAULT_NUM_TSERVERS;
    public int numNodes = DEFUALT_NUM_NODES;
    public int numShardsPerTServer = DEFAULT_NUM_SHARDS_PER_TSERVER;
    public boolean useIpWithCertificate = DEFAULT_USE_IP_WITH_CERTIFICATE;
    public int defaultTimeoutMs = 50000;
    public int defaultAdminOperationTimeoutMs = DEFAULT_ADMIN_OPERATION_TIMEOUT_MS;
    public int replicationFactor = -1;
    public boolean startYsqlProxy = false;
    public boolean pgTransactionsEnabled = false;
    private boolean enableYugabytedUI = false;
    public YsqlSnapshotVersion ysqlSnapshotVersion = YsqlSnapshotVersion.LATEST;
    public Optional<Integer> tserverHeartbeatTimeoutMsOpt = Optional.empty();
    public Optional<Integer> yqlSystemPartitionsVtableRefreshSecsOpt = Optional.empty();

    public int getTServerHeartbeatTimeoutMs() {
        return tserverHeartbeatTimeoutMsOpt.get();
    }

    public int getYQLSystemPartitionsVtableRefreshSecs() {
        return yqlSystemPartitionsVtableRefreshSecsOpt.get();
    }

    public boolean getEnableYugabytedUI() {
        return enableYugabytedUI;
    }

    public void setEnableYugabytedUI(boolean enableYugabytedUI) {
        this.enableYugabytedUI = enableYugabytedUI;
    }

}
