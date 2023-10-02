// Copyright (c) YugaByte, Inc.

package org.yb.client;

import java.util.List;
import org.yb.annotations.InterfaceAudience;
import org.yb.tserver.TserverService.ListTabletsForTabletServerResponsePB.Entry;

@InterfaceAudience.Public
public class ListTabletsForTabletServerResponse extends YRpcResponse {

  // The tablets that exist on this tserver.
  List<String> tabletIds;

  public ListTabletsForTabletServerResponse(
            long ellapsedMillis, String uuid, List<String> tabletIds) {
    super(ellapsedMillis, uuid);
    this.tabletIds = tabletIds;
  }

  public List<String> getTabletIds() {
    return tabletIds;
  }
}
