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

    private MiniYugabytedClusterParameters(Builder builder) {
        this.numNodes = builder.numNodes;
        this.numShardsPerTServer = builder.numShardsPerTServer;
        this.useIpWithCertificate = builder.useIpWithCertificate;
        this.defaultTimeoutMs = builder.defaultTimeoutMs;
        this.defaultAdminOperationTimeoutMs = builder.defaultAdminOperationTimeoutMs;
        this.replicationFactor = builder.replicationFactor;
        this.startYsqlProxy = builder.startYsqlProxy;
        this.pgTransactionsEnabled = builder.pgTransactionsEnabled;
        this.enableYugabytedUI = builder.enableYugabytedUI;
        this.ysqlSnapshotVersion = builder.ysqlSnapshotVersion;
        this.tserverHeartbeatTimeoutMsOpt = builder.tserverHeartbeatTimeoutMsOpt;
        this.yqlSystemPartitionsVtableRefreshSecsOpt =
                                builder.yqlSystemPartitionsVtableRefreshSecsOpt;
    }

    public static class Builder {
        private int numNodes = DEFUALT_NUM_NODES;
        private int numShardsPerTServer = DEFAULT_NUM_SHARDS_PER_TSERVER;
        private boolean useIpWithCertificate = DEFAULT_USE_IP_WITH_CERTIFICATE;
        private int defaultTimeoutMs = 50000;
        private int defaultAdminOperationTimeoutMs = DEFAULT_ADMIN_OPERATION_TIMEOUT_MS;
        private int replicationFactor = -1;
        private boolean startYsqlProxy = false;
        private boolean pgTransactionsEnabled = false;
        private boolean enableYugabytedUI = false;
        private YsqlSnapshotVersion ysqlSnapshotVersion = YsqlSnapshotVersion.LATEST;
        private Optional<Integer> tserverHeartbeatTimeoutMsOpt = Optional.empty();
        private Optional<Integer> yqlSystemPartitionsVtableRefreshSecsOpt = Optional.empty();

        public Builder numNodes(int numNodes) {
            this.numNodes = numNodes;
            return this;
        }

        public Builder numShardsPerTServer(int numShardsPerTServer) {
            this.numShardsPerTServer = numShardsPerTServer;
            return this;
        }

        public Builder useIpWithCertificate(boolean useIpWithCertificate) {
            this.useIpWithCertificate = useIpWithCertificate;
            return this;
        }

        public Builder defaultTimeoutMs(int defaultTimeoutMs) {
            this.defaultTimeoutMs = defaultTimeoutMs;
            return this;
        }

        public Builder defaultAdminOperationTimeoutMs(int defaultAdminOperationTimeoutMs) {
            this.defaultAdminOperationTimeoutMs = defaultAdminOperationTimeoutMs;
            return this;
        }

        public Builder replicationFactor(int replicationFactor) {
            this.replicationFactor = replicationFactor;
            return this;
        }

        public Builder startYsqlProxy(boolean startYsqlProxy) {
            this.startYsqlProxy = startYsqlProxy;
            return this;
        }

        public Builder pgTransactionsEnabled(boolean pgTransactionsEnabled) {
            this.pgTransactionsEnabled = pgTransactionsEnabled;
            return this;
        }

        public Builder enableYugabytedUI(boolean enableYugabytedUI) {
            this.enableYugabytedUI = enableYugabytedUI;
            return this;
        }

        public Builder ysqlSnapshotVersion(YsqlSnapshotVersion ysqlSnapshotVersion) {
            this.ysqlSnapshotVersion = ysqlSnapshotVersion;
            return this;
        }

        public Builder tserverHeartbeatTimeoutMsOpt(Optional<Integer>
                                            tserverHeartbeatTimeoutMsOpt) {
            this.tserverHeartbeatTimeoutMsOpt = tserverHeartbeatTimeoutMsOpt;
            return this;
        }

        public Builder yqlSystemPartitionsVtableRefreshSecsOpt(
                        Optional<Integer> yqlSystemPartitionsVtableRefreshSecsOpt) {
            this.yqlSystemPartitionsVtableRefreshSecsOpt = yqlSystemPartitionsVtableRefreshSecsOpt;
            return this;
        }

        public MiniYugabytedClusterParameters build() {
            return new MiniYugabytedClusterParameters(this);
        }
    }

    // Getter Methods
    public int getTServerHeartbeatTimeoutMs() {
        return tserverHeartbeatTimeoutMsOpt.get();
    }

    public int getYQLSystemPartitionsVtableRefreshSecs() {
        return yqlSystemPartitionsVtableRefreshSecsOpt.get();
    }

    public boolean getEnableYugabytedUI() {
        return enableYugabytedUI;
    }

    // Setter Methods
    public void setEnableYugabytedUI(boolean enableYugabytedUI) {
        this.enableYugabytedUI = enableYugabytedUI;
    }
}
