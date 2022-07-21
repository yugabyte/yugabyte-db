package com.yugabyte.yw.common.cdc;

import java.util.List;

public final class CdcStreamDeleteResponse {
  private final List<String> notFoundStreamIds;

  public CdcStreamDeleteResponse(List<String> notFoundStreamIds) {
    this.notFoundStreamIds = notFoundStreamIds;
  }

  public List<String> getNotFoundStreamIds() {
    return notFoundStreamIds;
  }
}
