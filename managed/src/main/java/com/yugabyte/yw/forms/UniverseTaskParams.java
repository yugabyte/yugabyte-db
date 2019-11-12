// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import java.util.UUID;
import java.util.Map;
import java.util.Set;

import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.DeviceInfo;

public class UniverseTaskParams extends AbstractTaskParams {
  // The primary device info.
  public DeviceInfo deviceInfo;

  // The universe against which this operation is being executed.
  public UUID universeUUID;

  // Expected version of the universe for operation execution. Set to -1 if an operation should
  // not verify expected version of the universe.
  public int expectedUniverseVersion;

  // Flag for enabling encryption at rest
  public boolean enableEncryptionAtRest = false;

  // Flag for disabling encryption at rest
  public boolean disableEncryptionAtRest = false;

  public String cmkArn;

  // Store encryption key provider specific configuration/authorization values
  public Map<String, String> encryptionAtRestConfig;

  // The set of nodes that are part of this universe. Should contain nodes in both primary and
  // readOnly clusters.
  public Set<NodeDetails> nodeDetailsSet = null;
}
