package org.yb.minicluster;
import java.util.Map;

public class MiniYugabytedNodeConfigurations {

    public String baseDir;
    public String ybdAdvertiseAddress;
    public final Map<String, String> yugabytedFlags;
    public final Map<String, String> yugabytedAdvancedFlags;
    public final Map<String, String> masterFlags;
    public final Map<String, String> tserverFlags;
    public final Map<String, String> tserverEnvVars;

    private MiniYugabytedNodeConfigurations(Builder builder) {
        this.yugabytedFlags = builder.yugabytedFlags;
        this.yugabytedAdvancedFlags = builder.yugabytedAdvancedFlags;
        this.masterFlags = builder.masterFlags;
        this.tserverFlags = builder.tserverFlags;
        this.tserverEnvVars = builder.tserverEnvVars;
        this.baseDir = builder.baseDir;
        this.ybdAdvertiseAddress = builder.ybdAdvertiseAddress;
    }

    public static class Builder {
        private Map<String, String> yugabytedFlags = null;
        private Map<String, String> yugabytedAdvancedFlags = null;
        private Map<String, String> masterFlags = null;
        private Map<String, String> tserverFlags = null;
        private Map<String, String> tserverEnvVars = null;
        private String baseDir = null;
        private String ybdAdvertiseAddress = null;

        public Builder yugabytedFlags(Map<String, String> yugabytedFlags) {
            this.yugabytedFlags = yugabytedFlags;
            return this;
        }

        public Builder yugabytedAdvancedFlags(Map<String, String> yugabytedAdvancedFlags) {
            this.yugabytedAdvancedFlags = yugabytedAdvancedFlags;
            return this;
        }

        public Builder masterFlags(Map<String, String> masterFlags) {
            this.masterFlags = masterFlags;
            return this;
        }

        public Builder tserverFlags(Map<String, String> tserverFlags) {
            this.tserverFlags = tserverFlags;
            return this;
        }

        public Builder tserverEnvVars(Map<String, String> tserverEnvVars) {
            this.tserverEnvVars = tserverEnvVars;
            return this;
        }

        public Builder baseDir(String baseDir) {
            this.baseDir = baseDir;
            return this;
        }

        public Builder ybdAdvertiseAddresses(String ybdAdvertiseAddress) {
            this.ybdAdvertiseAddress = ybdAdvertiseAddress;
            return this;
        }

        public MiniYugabytedNodeConfigurations build() {
            return new MiniYugabytedNodeConfigurations(this);
        }
    }

    // Getter Methods
    public Map<String, String> getYugabytedFlags() {
        return yugabytedFlags;
    }

    public String getBaseDir() {
        return baseDir;
    }

    public String getYbdAdvertiseAddresses() {
        return ybdAdvertiseAddress;
    }

    // Setter Methods
    public void setBaseDir(String baseDir) {
        this.baseDir = baseDir;
    }

    public void setYbdAdvertiseAddress(String ybdAdvertiseAddress) {
        this.ybdAdvertiseAddress = ybdAdvertiseAddress;
    }
}
