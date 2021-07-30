package org.yb.client;

import org.yb.annotations.InterfaceAudience;
import org.yb.master.Master;

@InterfaceAudience.Public
public class GetXClusterReplicationInfoResponse extends YRpcResponse {
  private final Master.MasterErrorPB serverError;
  private final Master.SysUniverseReplicationEntryPB info;

  GetXClusterReplicationInfoResponse(
    long elapsedMillis,
    String tsUUID,
    Master.MasterErrorPB serverError,
    Master.SysUniverseReplicationEntryPB info) {
    super(elapsedMillis, tsUUID);
    this.serverError = serverError;
    this.info = info;
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

  public Master.SysUniverseReplicationEntryPB info() {
    return info;
  }
}
