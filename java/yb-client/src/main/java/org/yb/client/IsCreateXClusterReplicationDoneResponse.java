// Copyright (c) YugaByte, Inc.

package org.yb.client;

import org.yb.WireProtocol.AppStatusPB;
import org.yb.annotations.InterfaceAudience;
import org.yb.master.MasterTypes.MasterErrorPB;

@InterfaceAudience.Public
public class IsCreateXClusterReplicationDoneResponse extends YRpcResponse {
  private final boolean done;
  private final AppStatusPB replicationError;
  private final MasterErrorPB serverError;

  public IsCreateXClusterReplicationDoneResponse(
      long elapsedMillis, String tsUUID,
      boolean done, AppStatusPB replicationError, MasterErrorPB serverError) {
    super(elapsedMillis, tsUUID);
    this.done = done;
    this.replicationError = replicationError;
    this.serverError = serverError;
  }

  public boolean isDone() {
    return this.done;
  }

  public boolean hasReplicationError() {
    return this.replicationError != null;
  }

  public AppStatusPB getReplicationError() {
    return this.replicationError;
  }

  public boolean hasError() {
    return this.serverError != null;
  }

  public String errorMessage() {
    if (serverError == null) {
      return "";
    }

    return serverError.getStatus().getMessage();
  }
}
