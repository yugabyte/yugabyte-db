// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.params;

import com.yugabyte.yw.forms.ITaskParams;

import java.util.UUID;

public class CloudTaskParams implements ITaskParams {
  public UUID providerUUID;
}
