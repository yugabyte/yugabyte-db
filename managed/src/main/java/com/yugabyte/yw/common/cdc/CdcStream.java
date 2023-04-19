/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
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
