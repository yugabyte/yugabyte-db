// Copyright (c) YugaByte, Inc.

package org.yb.client;

import org.yb.master.MasterTypes;

public class AreNodesSafeToTakeDownResponse {

  private final MasterTypes.MasterErrorPB error;

  public AreNodesSafeToTakeDownResponse(MasterTypes.MasterErrorPB error) {
    this.error = error;
  }

  public String getErrorMessage() {
    return error.getStatus().getMessage();
  }

  public boolean isSucessful() {
    return error == null;
  }
}
