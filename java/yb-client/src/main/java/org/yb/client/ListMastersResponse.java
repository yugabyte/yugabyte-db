// Copyright (c) YugaByte, Inc.

package org.yb.client;

import org.yb.annotations.InterfaceAudience;
import org.yb.util.ServerInfo;

import java.util.List;

@InterfaceAudience.Public
public class ListMastersResponse extends YRpcResponse {
  private boolean hasError;
  private final List<ServerInfo> mastersList;

  ListMastersResponse(
      long ellapsedMillis, String masterUUID, boolean hasErr, List<ServerInfo> mastersList) {
    super(ellapsedMillis, masterUUID);
    hasError = hasErr;
    this.mastersList = mastersList;
  }

  public boolean hasError() {
    return hasError;
  }

  public int getNumMasters() {
    return mastersList.size();
  }

  public List<ServerInfo> getMasters() {
    return mastersList;
  }
}
