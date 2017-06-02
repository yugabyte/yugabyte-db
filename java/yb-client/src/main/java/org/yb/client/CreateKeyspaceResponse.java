// Copyright (c) YugaByte, Inc.

package org.yb.client;

import org.yb.annotations.InterfaceAudience;
import org.yb.master.Master;

@InterfaceAudience.Public
public class CreateKeyspaceResponse extends YRpcResponse {
  private Master.MasterErrorPB serverError;

  CreateKeyspaceResponse(long ellapsedMillis, String masterUUID,
      Master.MasterErrorPB error) {
    super(ellapsedMillis, masterUUID);
    serverError = error;
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
