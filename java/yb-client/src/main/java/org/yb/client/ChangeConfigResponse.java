// Copyright (c) Yugabyte, Inc.

package org.yb.client;

import org.yb.annotations.InterfaceAudience;
import org.yb.annotations.InterfaceStability;

import java.util.List;

@InterfaceAudience.Public
public class ChangeConfigResponse extends YRpcResponse {
  private boolean hasError;
  
  ChangeConfigResponse(long ellapsedMillis, String masterUUID, boolean hasErr) {
    super(ellapsedMillis, masterUUID);
    hasError = hasErr;
  }

  public boolean hasError() {
    return hasError;
  }
}
