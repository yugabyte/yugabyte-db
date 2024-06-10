// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.params;

import java.util.UUID;

public interface IProviderTaskParams {
  UUID getProviderUUID();
}
