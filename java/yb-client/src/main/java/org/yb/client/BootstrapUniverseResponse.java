package org.yb.client;

import java.util.List;
import org.yb.annotations.InterfaceAudience;
import org.yb.cdc.CdcService;

@InterfaceAudience.Public
public class BootstrapUniverseResponse extends YRpcResponse {
  private final CdcService.CDCErrorPB cdcError;
  private final List<String> bootstrapIds;

  public BootstrapUniverseResponse(
    long elapsedMillis, String tsUUID, CdcService.CDCErrorPB cdcError, List<String> bootstrapIds) {
    super(elapsedMillis, tsUUID);
    this.cdcError = cdcError;
    this.bootstrapIds = bootstrapIds;
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

  public List<String> bootstrapIds() {
    return bootstrapIds;
  }
}
