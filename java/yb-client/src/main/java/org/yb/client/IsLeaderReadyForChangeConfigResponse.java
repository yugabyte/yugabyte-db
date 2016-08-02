// Copyright (c) YugaByte, Inc.

package org.yb.client;

import org.yb.tserver.Tserver.TabletServerErrorPB;

public class IsLeaderReadyForChangeConfigResponse extends YRpcResponse {
  private TabletServerErrorPB serverError;
  private boolean isReady;

  IsLeaderReadyForChangeConfigResponse(long ellapsedMillis,
                                       String masterUUID,
                                       TabletServerErrorPB error,
                                       boolean isReady) {
    super(ellapsedMillis, masterUUID);
    serverError = error;
    this.isReady = isReady;
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

  public boolean isReady() {
    return isReady;
  }
}
