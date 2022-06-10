package org.yb.client;

import java.util.List;
import org.yb.cdc.CdcService;

public class IsBootstrapRequiredResponse extends YRpcResponse {
  private final CdcService.CDCErrorPB cdcError;
  private final boolean bootstrapRequired;

  public IsBootstrapRequiredResponse(
    long elapsedMillis, String tsUUID, CdcService.CDCErrorPB cdcError, boolean bootstrapRequired) {
    super(elapsedMillis, tsUUID);
    this.cdcError = cdcError;
    this.bootstrapRequired = bootstrapRequired;
  }

  public boolean hasError() {
    return cdcError != null;
  }

  public String errorMessage() {
    if (cdcError == null) {
      return "";
    }

    return cdcError.getStatus().getMessage();
  }

  public boolean bootstrapRequired() {
    return bootstrapRequired;
  }
}
