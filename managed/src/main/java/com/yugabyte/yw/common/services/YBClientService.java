// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.services;

import java.time.Duration;

import org.yb.client.YBClient;

import lombok.Getter;
import lombok.NonNull;
import lombok.Setter;

public interface YBClientService {

  @Getter
  @Setter
  public static class Config {
    private @NonNull String masterHostPorts;
    private String certFile;
    private Duration adminOperationTimeout = Duration.ofMillis(30000);

    public Config(String masterHostPorts) {
      this(masterHostPorts, null);
    }

    public Config(String masterHostPorts, String certFile) {
      this.masterHostPorts = masterHostPorts;
      this.certFile = certFile;
    }
  }

  YBClient getClient(String masterHostPorts);

  YBClient getClient(String masterHostPorts, String certFile);

  YBClient getClientWithConfig(Config config);

  void closeClient(YBClient client, String masterHostPorts);
}
