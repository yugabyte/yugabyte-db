package com.yugabyte.yw.forms;

import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import org.yb.Common;

public class XClusterReplicationTaskParams extends UniverseDefinitionTaskParams {

  public UUID sourceUniverseUUID;
  public UUID targetUniverseUUID;

  public List<String> sourceTableIDs;
  public List<String> targetTableIDs;

  public boolean active;

  public List<String> bootstrapIDs;

  public List<String> sourceTableIdsToAdd;
  public List<String> sourceTableIdsToRemove;
  public List<Common.HostPortPB> sourceMasterAddresses;

  public XClusterReplicationTaskParams() {
    this.sourceTableIDs = new ArrayList<>();
    this.targetTableIDs = new ArrayList<>();
    this.bootstrapIDs = new ArrayList<>();
    this.sourceTableIdsToAdd = new ArrayList<>();
    this.sourceTableIdsToRemove = new ArrayList<>();
    this.sourceMasterAddresses = new ArrayList<>();
  }

  public XClusterReplicationTaskParams(XClusterReplicationTaskParams params) {
    this.sourceUniverseUUID = params.sourceUniverseUUID;
    this.targetUniverseUUID = params.targetUniverseUUID;
    this.sourceTableIDs = new ArrayList<>(params.sourceTableIDs);
    this.targetTableIDs = new ArrayList<>(params.targetTableIDs);
    this.active = params.active;
    this.bootstrapIDs = new ArrayList<>(params.bootstrapIDs);
    this.sourceTableIdsToAdd = new ArrayList<>(params.sourceTableIdsToAdd);
    this.sourceTableIdsToRemove = new ArrayList<>(params.sourceTableIdsToRemove);
    this.sourceMasterAddresses = new ArrayList<>(params.sourceMasterAddresses);
  }
}
