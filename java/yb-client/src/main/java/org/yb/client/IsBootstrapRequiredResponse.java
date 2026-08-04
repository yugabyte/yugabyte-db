package org.yb.client;


import java.util.Map;
import org.yb.master.MasterTypes.MasterErrorPB;

public class IsBootstrapRequiredResponse extends YRpcResponse {
  private final MasterErrorPB error;
  private final Map<String, Boolean> results;

  public IsBootstrapRequiredResponse(
    long elapsedMillis, String tsUUID, MasterErrorPB error, Map<String, Boolean> results) {
    super(elapsedMillis, tsUUID);
    this.error = error;
    this.results = results;
  }

  public boolean hasError() {
    return error != null;
  }

  public String errorMessage() {
    if (error == null) {
      return "";
    }

    return error.getStatus().getMessage();
  }

  public Map<String, Boolean> getResults() {
    return results;
  }
}
