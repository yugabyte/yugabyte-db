// Copyright (c) YugaByte, Inc.

package org.yb.client;

import java.util.List;
import org.yb.annotations.InterfaceAudience;
import org.yb.master.MasterTypes;

@InterfaceAudience.Public
public class GetXClusterOutboundReplicationGroupsResponse extends YRpcResponse {
  private final MasterTypes.MasterErrorPB serverError;
  private final List<String> replicationGroupIds;

  public GetXClusterOutboundReplicationGroupsResponse(
    long elapsedMillis,
    String tsUUID,
    MasterTypes.MasterErrorPB serverError,
    List<String> replicationGroupIds) {
      super(elapsedMillis, tsUUID);
      this.serverError = serverError;
      this.replicationGroupIds = replicationGroupIds;
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

  public List<String> getReplicationGroupIds() {
    return this.replicationGroupIds;
  }
}
