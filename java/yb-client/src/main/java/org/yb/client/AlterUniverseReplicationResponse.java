package org.yb.client;

import org.yb.annotations.InterfaceAudience;
import org.yb.master.Master;

@InterfaceAudience.Public
public class AlterUniverseReplicationResponse extends YRpcResponse {
  private final Master.MasterErrorPB serverError;

  public AlterUniverseReplicationResponse(
    long elapsedMillis, String tsUUID, Master.MasterErrorPB serverError) {
    super(elapsedMillis, tsUUID);
    this.serverError = serverError;
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
