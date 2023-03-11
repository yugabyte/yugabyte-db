// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.services.config;

import com.google.inject.Inject;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;

import javax.inject.Singleton;

@Singleton
public class YbClientConfigFactory {
  public static final String YBC_ADMIN_OPERATION_TIMEOUT_MS =
      "ybc.timeout.admin_operation_timeout_ms";
  public static final String YBC_SOCKET_READ_TIMEOUT_MS = "ybc.timeout.socket_read_timeout_ms";
  public static final String YBC_OPERATION_TIMEOUT_MS = "ybc.timeout.operation_timeout_ms";

  @Inject RuntimeConfigFactory runtimeConfigFactory;

  public YbClientConfig create(String masterHostPorts) {
    return create(masterHostPorts, null);
  }

  public YbClientConfig create(String masterHostPorts, String certFile) {
    return create(
        masterHostPorts,
        certFile,
        runtimeConfigFactory.globalRuntimeConf().getInt(YBC_ADMIN_OPERATION_TIMEOUT_MS),
        runtimeConfigFactory.globalRuntimeConf().getInt(YBC_SOCKET_READ_TIMEOUT_MS),
        runtimeConfigFactory.globalRuntimeConf().getInt(YBC_OPERATION_TIMEOUT_MS));
  }

  public YbClientConfig create(
      String masterHostPorts, String certFile, long adminOperationTimeoutMs) {
    return create(
        masterHostPorts,
        certFile,
        adminOperationTimeoutMs,
        runtimeConfigFactory.globalRuntimeConf().getInt(YBC_SOCKET_READ_TIMEOUT_MS),
        runtimeConfigFactory.globalRuntimeConf().getInt(YBC_OPERATION_TIMEOUT_MS));
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
        runtimeConfigFactory.globalRuntimeConf().getInt(YBC_OPERATION_TIMEOUT_MS));
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
