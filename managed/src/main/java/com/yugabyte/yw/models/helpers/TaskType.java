package com.yugabyte.yw.models.helpers;


/**
 * These are the various types of user tasks and internal tasks.
 */
public enum TaskType {

  // Tasks that are CustomerTasks
  CloudBootstrap("CloudBootstrap"),

  CloudCleanup("CloudCleanup"),

  CreateCassandraTable("CreateCassandraTable"),

  CreateUniverse("CreateUniverse"),

  DestroyUniverse("DestroyUniverse"),

  DeleteTable("DeleteTable"),

  EditUniverse("EditUniverse"),

  ImportIntoTable("ImportIntoTable"),

  UpgradeUniverse("UpgradeUniverse"),

  DeleteNodeFromUniverse("DeleteNodeFromUniverse"),

  StopNodeInUniverse("StopNodeInUniverse"),

  StartNodeInUniverse("StartNodeInUniverse"),

  // Tasks belonging to subtasks classpath
  AnsibleClusterServerCtl("subtasks.AnsibleClusterServerCtl"),

  AnsibleConfigureServers("subtasks.AnsibleConfigureServers"),

  AnsibleDestroyServer("subtasks.AnsibleDestroyServer"),

  AnsibleSetupServer("subtasks.AnsibleSetupServer"),

  AnsibleUpdateNodeInfo("subtasks.AnsibleUpdateNodeInfo"),

  BulkImport("subtasks.BulkImport"),

  ChangeMasterConfig("subtasks.ChangeMasterConfig"),

  CreateTable("subtasks.CreateTable"),

  DeleteNode("subtasks.DeleteNode"),

  UpdateNodeProcess("subtasks.nodes.UpdateNodeProcess"),

  DeleteTableFromUniverse("subtasks.DeleteTableFromUniverse"),

  LoadBalancerStateChange("subtasks.LoadBalancerStateChange"),

  ModifyBlackList("subtasks.ModifyBlackList"),

  RemoveUniverseEntry("subtasks.RemoveUniverseEntry"),

  SetNodeState("subtasks.SetNodeState"),

  SwamperTargetsFileUpdate("subtasks.SwamperTargetsFileUpdate"),

  UniverseUpdateSucceeded("subtasks.UniverseUpdateSucceeded"),

  UpdateAndPersistGFlags("subtasks.UpdateAndPersistGFlags"),

  UpdatePlacementInfo("subtasks.UpdatePlacementInfo"),

  UpdateSoftwareVersion("subtasks.UpdateSoftwareVersion"),

  WaitForDataMove("subtasks.WaitForDataMove"),

  WaitForLoadBalance("subtasks.WaitForLoadBalance"),

  WaitForMasterLeader("subtasks.WaitForMasterLeader"),

  WaitForServer("subtasks.WaitForServer"),

  WaitForTServerHeartBeats("subtasks.WaitForTServerHeartBeats"),

  // Tasks belonging to subtasks.cloud classpath
  CloudAccessKeyCleanup("subtasks.cloud.CloudAccessKeyCleanup"),

  CloudAccessKeySetup("subtasks.cloud.CloudAccessKeySetup"),

  CloudInitializer("subtasks.cloud.CloudInitializer"),

  CloudProviderCleanup("subtasks.cloud.CloudProviderCleanup"),

  CloudRegionCleanup("subtasks.cloud.CloudRegionCleanup"),

  CloudRegionSetup("subtasks.cloud.CloudRegionSetup"),

  CloudSetup("subtasks.cloud.CloudSetup"),

  BackupTable("subtasks.BackupTable");

  private String relativeClassPath;

  TaskType(String relativeClassPath) {
    this.relativeClassPath = relativeClassPath;
  }

  @Override
  public String toString() {
    return this.relativeClassPath;
  }
}
