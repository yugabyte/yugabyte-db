// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.services;

import java.time.Duration;
import lombok.Getter;
import lombok.NonNull;
import lombok.Setter;
import org.yb.client.YBClient;

public interface YBClientService {

  @Getter
  @Setter
  public static class Config {

    public static final long DEFAULT_ADMIN_OPERATION_TIMEOUT_MS = 120000;

    private @NonNull String masterHostPorts;
    private String certFile;
    private Duration adminOperationTimeout = Duration.ofMillis(DEFAULT_ADMIN_OPERATION_TIMEOUT_MS);

    public Config(String masterHostPorts) {
      this(masterHostPorts, null);
    }

    public Config(String masterHostPorts, String certFile) {
      this.masterHostPorts = masterHostPorts;
      this.certFile = certFile;
    }

    public Config(String masterHostPorts, String certFile, long adminOperationTimeoutMs) {
      this.masterHostPorts = masterHostPorts;
      this.certFile = certFile;
      this.adminOperationTimeout = Duration.ofMillis(adminOperationTimeoutMs);
    }
  }

  YBClient getClient(String masterHostPorts);

  YBClient getClient(String masterHostPorts, String certFile);

  YBClient getClientWithConfig(Config config);

  void closeClient(YBClient client, String masterHostPorts);
}
