/*
 * Copyright 2022 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common.cdc;

public final class CdcStreamCreateResponse {
  private final String streamId;

  public CdcStreamCreateResponse(String streamId) {
    this.streamId = streamId;
  }

  public String getStreamId() {
    return streamId;
  }
}
