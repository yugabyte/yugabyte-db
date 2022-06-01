package org.yb.client;

import java.util.List;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.stream.Collectors;
import org.yb.WireProtocol;
import org.yb.annotations.InterfaceAudience;
import org.yb.master.MasterTypes;

@InterfaceAudience.Public
public class DeleteUniverseReplicationResponse extends YRpcResponse {
  private final MasterTypes.MasterErrorPB serverError;
  private final List<WireProtocol.AppStatusPB> warnings;

  public DeleteUniverseReplicationResponse(
      long elapsedMillis,
      String tsUUID,
      MasterTypes.MasterErrorPB serverError,
      List<WireProtocol.AppStatusPB> warnings) {
    super(elapsedMillis, tsUUID);
    this.serverError = serverError;
    this.warnings = warnings;
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

  public List<WireProtocol.AppStatusPB> getWarnings() {
    return warnings;
  }

  public String getWarningsString() {
    if (warnings != null && !warnings.isEmpty()) {
      return "[" + warnings.stream().map(
          warning -> String.format(
            "<<<%s (code=%s)>>>", warning.getMessage(), warning.getCode()))
          .collect(Collectors.joining(", ")) + "]";
    }
    return null;
  }
}
