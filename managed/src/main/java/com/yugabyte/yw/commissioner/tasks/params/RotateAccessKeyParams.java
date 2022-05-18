package com.yugabyte.yw.commissioner.tasks.params;

import java.util.UUID;

import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.AccessKey;

public class RotateAccessKeyParams extends UniverseTaskParams {
  public UUID customerUUID;
  public UUID providerUUID;
  public UUID universeUUID;
  public AccessKey newAccessKey;

  public RotateAccessKeyParams(
      UUID customerUUID, UUID providerUUID, UUID universeUUID, AccessKey newAccessKey) {
    this.customerUUID = customerUUID;
    this.providerUUID = providerUUID;
    this.universeUUID = universeUUID;
    this.newAccessKey = newAccessKey;
  }
}
