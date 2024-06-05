// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.services.config;

import com.google.inject.Inject;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import javax.inject.Singleton;

@Singleton
public class YbClientConfigFactory {
  @Inject RuntimeConfGetter confGetter;

  public YbClientConfig create(String masterHostPorts) {
    return create(masterHostPorts, null);
  }

  public YbClientConfig create(String masterHostPorts, String certFile) {
    return create(
        masterHostPorts,
        certFile,
        confGetter.getGlobalConf(GlobalConfKeys.ybcAdminOperationTimeoutMs),
        confGetter.getGlobalConf(GlobalConfKeys.ybcSocketReadTimeoutMs),
        confGetter.getGlobalConf(GlobalConfKeys.ybcOperationTimeoutMs));
  }

  public YbClientConfig create(
      String masterHostPorts, String certFile, long adminOperationTimeoutMs) {
    return create(
        masterHostPorts,
        certFile,
        adminOperationTimeoutMs,
        confGetter.getGlobalConf(GlobalConfKeys.ybcSocketReadTimeoutMs),
        confGetter.getGlobalConf(GlobalConfKeys.ybcOperationTimeoutMs));
  }

  public YbClientConfig create(
      String masterHostPorts,
      String certFile,
      long adminOperationTimeoutMs,
      long socketReadTimeoutMs) {
    return create(
        masterHostPorts,
        certFile,
        adminOperationTimeoutMs,
        socketReadTimeoutMs,
        confGetter.getGlobalConf(GlobalConfKeys.ybcOperationTimeoutMs));
  }

  public YbClientConfig create(
      String masterHostPorts,
      String certFile,
      long adminOperationTimeoutMs,
      long socketReadTimeoutMs,
      long operationTimeoutMs) {
    return new YbClientConfig(
        masterHostPorts,
        certFile,
        adminOperationTimeoutMs,
        socketReadTimeoutMs,
        operationTimeoutMs);
  }
}
