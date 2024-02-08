// Copyright (c) YugaByte, Inc.

package org.yb.client;

import java.util.List;
import org.yb.annotations.InterfaceAudience;
import org.yb.master.MasterTypes;
import org.yb.util.TabletServerInfo;

@InterfaceAudience.Public
public class ListLiveTabletServersResponse extends YRpcResponse {

  private final List<TabletServerInfo> tabletServers;
  private final MasterTypes.MasterErrorPB serverError;

  ListLiveTabletServersResponse(long elapsedMillis, String tsUUID,
                                List<TabletServerInfo> tabletServers,
                                MasterTypes.MasterErrorPB error) {
    super(elapsedMillis, tsUUID);
    this.tabletServers = tabletServers;
    this.serverError = error;
  }

  public boolean hasError() {
    return serverError != null;
  }

  public List<TabletServerInfo> getTabletServers() {
    return tabletServers;
  }

  public String errorMessage() {
    if (serverError == null) {
      return "";
    }

    return serverError.getStatus().getMessage();
  }
}
