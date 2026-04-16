package org.yb.client;

import org.yb.annotations.InterfaceAudience;
import org.yb.master.CatalogEntityInfo;
import org.yb.master.MasterTypes;

@InterfaceAudience.Public
public class GetUniverseReplicationResponse extends YRpcResponse {
  private final MasterTypes.MasterErrorPB serverError;
  private final CatalogEntityInfo.SysUniverseReplicationEntryPB info;

  public GetUniverseReplicationResponse(
    long elapsedMillis,
    String tsUUID,
    MasterTypes.MasterErrorPB serverError,
    CatalogEntityInfo.SysUniverseReplicationEntryPB info) {
    super(elapsedMillis, tsUUID);
    this.serverError = serverError;
    this.info = info;
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

  public CatalogEntityInfo.SysUniverseReplicationEntryPB info() {
    return info;
  }
}
