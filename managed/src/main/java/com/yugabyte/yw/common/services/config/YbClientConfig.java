// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.services.config;

import com.yugabyte.yw.models.Universe;
import java.time.Duration;
import lombok.Getter;
import lombok.NonNull;
import lombok.Setter;

@Getter
@Setter
public class YbClientConfig {

  private @NonNull String masterHostPorts;
  private String certFile;
  private Duration adminOperationTimeout;
  private Duration socketReadTimeout;
  private Duration operationTimeout;
  // Presents this universe's client cert, if it requires one.
  private Universe clientCertUniverse;

  public YbClientConfig(
      String masterHostPorts,
      String certFile,
      long adminOperationTimeoutMs,
      long socketReadTimeoutMs,
      long operationTimeoutMs) {
    this.masterHostPorts = masterHostPorts;
    this.certFile = certFile;
    this.adminOperationTimeout = Duration.ofMillis(adminOperationTimeoutMs);
    this.socketReadTimeout = Duration.ofMillis(socketReadTimeoutMs);
    this.operationTimeout = Duration.ofMillis(operationTimeoutMs);
  }

  public YbClientConfig withClientCertOf(Universe universe) {
    clientCertUniverse = universe;
    return this;
  }
}
