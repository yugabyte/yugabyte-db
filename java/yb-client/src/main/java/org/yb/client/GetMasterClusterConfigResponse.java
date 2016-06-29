// Copyright (c) YugaByte, Inc.

package org.yb.client;

import org.yb.annotations.InterfaceAudience;
import org.yb.master.Master;

@InterfaceAudience.Public
public class GetMasterClusterConfigResponse extends YRpcResponse {
  private Master.SysClusterConfigEntryPB clusterConfig;
  private Master.MasterErrorPB serverError;

  GetMasterClusterConfigResponse(
      long ellapsedMillis, String masterUUID, Master.SysClusterConfigEntryPB config,
      Master.MasterErrorPB error) {
    super(ellapsedMillis, masterUUID);
    serverError = error;
    clusterConfig = config;
  }

  public Master.SysClusterConfigEntryPB getConfig() {
    return clusterConfig;
  }

  public boolean hasError() {
    return serverError != null;
  }

  public String errorMessage() {
    if (serverError == null) {
      return "";
    }

    return serverError.getStatus().getMessage();
  }
}
