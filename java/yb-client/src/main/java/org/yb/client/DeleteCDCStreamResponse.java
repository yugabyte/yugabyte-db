package org.yb.client;

import java.util.Set;
import org.yb.master.MasterTypes;

public class DeleteCDCStreamResponse extends YRpcResponse {
  private final MasterTypes.MasterErrorPB serverError;
  private final Set<String> notFoundStreamIds;

  DeleteCDCStreamResponse (long elapsedMillis,
                           String tsUUID,
                           MasterTypes.MasterErrorPB serverError,
                           Set<String> notFoundStreamIds) {
    super(elapsedMillis, tsUUID);
    this.serverError = serverError;
    this.notFoundStreamIds = notFoundStreamIds;
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

  public Set<String> getNotFoundStreamIds() {
    return notFoundStreamIds;
  }
}
