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
