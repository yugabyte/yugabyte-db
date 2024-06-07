// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.params;

import com.yugabyte.yw.forms.AbstractTaskParams;
import java.util.UUID;

public class CloudTaskParams extends AbstractTaskParams implements IProviderTaskParams {
  // The Provider for which to bootstrap the network.
  public UUID providerUUID;

  @Override
  public UUID getProviderUUID() {
    return providerUUID;
  }
}
