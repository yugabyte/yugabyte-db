package org.yb.client;

import org.yb.annotations.InterfaceAudience;
import org.yb.master.MasterTypes.MasterErrorPB;

@InterfaceAudience.Public
public class XClusterDeleteOutboundReplicationGroupResponse extends YRpcResponse {
  private final MasterErrorPB error;

  public XClusterDeleteOutboundReplicationGroupResponse(
      long elapsedMillis, String tsUUID, MasterErrorPB error) {
    super(elapsedMillis, tsUUID);
    this.error = error;
  }

  public boolean hasError() {
    return error != null;
  }

  public String errorMessage() {
    if (error == null) {
      return "";
    }

    return error.getStatus().getMessage();
  }
}
