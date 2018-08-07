// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import java.util.UUID;

import com.yugabyte.yw.models.helpers.DeviceInfo;

public class UniverseTaskParams extends AbstractTaskParams {
  // The primary device info.
  public DeviceInfo deviceInfo;

  // The universe against which this operation is being executed.
  public UUID universeUUID;

  // Expected version of the universe for operation execution. Set to -1 if an operation should
  // not verify expected version of the universe.
  public int expectedUniverseVersion;
}
