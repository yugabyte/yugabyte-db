package com.yugabyte.yw.common.cdc;

import java.util.Map;

public final class CdcStream {
  private final String streamId;
  private final Map<String, String> options;
  private final String namespaceId;

  public CdcStream(String streamId, Map<String, String> options, String namespaceId) {
    this.streamId = streamId;
    this.options = options;
    this.namespaceId = namespaceId;
  }

  public String getStreamId() {
    return streamId;
  }

  public Map<String, String> getOptions() {
    return options;
  }

  public String getNamespaceId() {
    return namespaceId;
  }
}
