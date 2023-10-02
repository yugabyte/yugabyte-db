// Copyright (c) YugaByte, Inc.
package org.yb.client;

import org.yb.annotations.InterfaceAudience;
import org.yb.tserver.TserverTypes;

@InterfaceAudience.Public
public class UpgradeYsqlResponse extends YRpcResponse {

  private final TserverTypes.TabletServerErrorPB serverError;

  public UpgradeYsqlResponse(long ellapsedMillis, String uuid,
                            TserverTypes.TabletServerErrorPB serverError) {
    super(ellapsedMillis, uuid);
    this.serverError = serverError;
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
