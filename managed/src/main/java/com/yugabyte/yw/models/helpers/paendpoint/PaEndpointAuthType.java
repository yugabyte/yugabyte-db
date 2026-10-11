// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models.helpers.paendpoint;

/** How a Perf Advisor Endpoint authenticates against one of its two endpoints. */
public enum PaEndpointAuthType {
  NONE,
  BASIC
}
