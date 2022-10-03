package com.yugabyte.yw.commissioner.tasks.params;

import java.util.UUID;

import com.yugabyte.yw.models.AccessKey;

public class NodeAccessTaskParams extends NodeTaskParams {

  public UUID customerUUID;

  public UUID providerUUID;

  public UUID regionUUID;

  public UUID universeUUID;

  public AccessKey accessKey;

  // Key to be used for add/remove authorized key tasks
  public AccessKey taskAccessKey;

  public String sshUser;

  public NodeAccessTaskParams(
      UUID customerUUID,
      UUID providerUUID,
      UUID azUuid,
      UUID universeUUID,
      AccessKey accessKey,
      String sshUser) {
    this.customerUUID = customerUUID;
    this.azUuid = azUuid;
    this.providerUUID = providerUUID;
    this.universeUUID = universeUUID;
    this.accessKey = accessKey;
    this.sshUser = sshUser;
  }
}
