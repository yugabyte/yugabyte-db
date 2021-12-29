package org.yb.client;

import org.yb.annotations.InterfaceAudience;
import org.yb.cdc.CdcService;

import java.util.List;

@InterfaceAudience.Public
public class BootstrapUniverseResponse extends YRpcResponse {
  private final CdcService.CDCErrorPB cdcError;
  private final List<String> bootstrapIDs;

  public BootstrapUniverseResponse(
    long elapsedMillis, String tsUUID, CdcService.CDCErrorPB cdcError, List<String> bootstrapIDs) {
    super(elapsedMillis, tsUUID);
    this.cdcError = cdcError;
    this.bootstrapIDs = bootstrapIDs;
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

  public List<String> bootstrapIDs() {
    return bootstrapIDs;
  }
}
