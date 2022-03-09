// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.services;

import java.time.Duration;

import org.yb.client.YBClient;

public interface YBClientService {

  public static class Config {
    private String masterHostPorts;
    private String certFile;
    private Duration adminOperationTimeout = Duration.ofSeconds(120);

    public Config(String masterHostPorts) {
      this(masterHostPorts, null);
    }

    public Config(String masterHostPorts, String certFile) {
      this.setMasterHostPorts(masterHostPorts);
      this.setCertFile(certFile);
    }

    public String getMasterHostPorts() {
      return masterHostPorts;
    }

    public void setMasterHostPorts(String masterHostPorts) {
      this.masterHostPorts = masterHostPorts;
    }

    public String getCertFile() {
      return certFile;
    }

    public void setCertFile(String certFile) {
      this.certFile = certFile;
    }

    public Duration getAdminOperationTimeout() {
      return adminOperationTimeout;
    }

    public void setAdminOperationTimeout(Duration adminOperationTimeout) {
      this.adminOperationTimeout = adminOperationTimeout;
    }
  }

  YBClient getClient(String masterHostPorts);

  YBClient getClient(String masterHostPorts, String certFile);

  YBClient getClientWithConfig(Config config);

  void closeClient(YBClient client, String masterHostPorts);
}
