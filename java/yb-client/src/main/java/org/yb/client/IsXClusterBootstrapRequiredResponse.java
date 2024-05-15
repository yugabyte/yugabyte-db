// Copyright (c) YugaByte, Inc.

package org.yb.client;

import org.yb.annotations.InterfaceAudience;
import org.yb.master.MasterTypes.MasterErrorPB;

@InterfaceAudience.Public
public class IsXClusterBootstrapRequiredResponse extends YRpcResponse {
  private final MasterErrorPB error;
  private final boolean notReady;
  private final boolean initialBootstrapRequired;

  IsXClusterBootstrapRequiredResponse(long elapsedMillis, String tsUUID,
                                      boolean notReady, boolean initialBootstrapRequired,
                                      MasterErrorPB error) {
    super(elapsedMillis, tsUUID);
    this.notReady = notReady;
    this.initialBootstrapRequired = initialBootstrapRequired;
    this.error = error;
  }

  public boolean isNotReady() {
    return notReady;
  }

  public boolean itInitialBootstrapRequired() {
    return initialBootstrapRequired;
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
