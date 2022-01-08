package com.yugabyte.yw.models.helpers;

import java.lang.reflect.Field;
import java.util.Arrays;
import java.util.List;
import java.util.stream.Collectors;

/** These are the various types of user tasks and internal tasks. */
public enum TaskType {

  // Tasks that are CustomerTasks
  CloudBootstrap("CloudBootstrap"),

  CloudCleanup("CloudCleanup"),

  CreateCassandraTable("CreateCassandraTable"),

  CreateUniverse("CreateUniverse"),

  ReadOnlyClusterCreate("ReadOnlyClusterCreate"),

  ReadOnlyClusterDelete("ReadOnlyClusterDelete"),

  CreateKubernetesUniverse("CreateKubernetesUniverse"),

  DestroyUniverse("DestroyUniverse"),

  PauseUniverse("PauseUniverse"),

  ResumeUniverse("ResumeUniverse"),

  DestroyKubernetesUniverse("DestroyKubernetesUniverse"),

  DeleteTable("DeleteTable"),

  BackupUniverse("BackupUniverse"),

  MultiTableBackup("MultiTableBackup"),

  CreateBackup("CreateBackup"),

  EditUniverse("EditUniverse"),

  EditKubernetesUniverse("EditKubernetesUniverse"),

  ExternalScript("ExternalScript"),

  @Deprecated
  KubernetesProvision("KubernetesProvision"),

  ImportIntoTable("ImportIntoTable"),

  // TODO: Mark it as deprecated once UpgradeUniverse related APIs are removed
  UpgradeUniverse("UpgradeUniverse"),

  RestartUniverse("upgrade.RestartUniverse"),

  SoftwareUpgrade("upgrade.SoftwareUpgrade"),

  SoftwareKubernetesUpgrade("upgrade.SoftwareKubernetesUpgrade"),

  GFlagsUpgrade("upgrade.GFlagsUpgrade"),

  GFlagsKubernetesUpgrade("upgrade.GFlagsKubernetesUpgrade"),

  CertsRotate("upgrade.CertsRotate"),

  TlsToggle("upgrade.TlsToggle"),

  VMImageUpgrade("upgrade.VMImageUpgrade"),

  SystemdUpgrade("upgrade.SystemdUpgrade"),

  CreateRootVolumes("subtasks.CreateRootVolumes"),

  ReplaceRootVolume("subtasks.ReplaceRootVolume"),

  ChangeInstanceType("subtasks.ChangeInstanceType"),

  PersistResizeNode("subtasks.PersistResizeNode"),

  PersistSystemdUpgrade("subtasks.PersistSystemdUpgrade"),

  UpdateNodeDetails("subtasks.UpdateNodeDetails"),

  UpgradeKubernetesUniverse("UpgradeKubernetesUniverse"),

  DeleteNodeFromUniverse("DeleteNodeFromUniverse"),

  StopNodeInUniverse("StopNodeInUniverse"),

  StartNodeInUniverse("StartNodeInUniverse"),

  AddNodeToUniverse("AddNodeToUniverse"),

  RemoveNodeFromUniverse("RemoveNodeFromUniverse"),

  ReleaseInstanceFromUniverse("ReleaseInstanceFromUniverse"),

  SetUniverseKey("SetUniverseKey"),

  @Deprecated
  SetKubernetesUniverseKey("SetKubernetesUniverseKey"),

  CreateKMSConfig("CreateKMSConfig"),

  EditKMSConfig("EditKMSConfig"),

  DeleteKMSConfig("DeleteKMSConfig"),

  UpdateDiskSize("UpdateDiskSize"),

  StartMasterOnNode("StartMasterOnNode"),

  CreateXClusterConfig("CreateXClusterConfig"),

  EditXClusterConfig("EditXClusterConfig"),

  DeleteXClusterConfig("DeleteXClusterConfig"),

  SyncXClusterConfig("SyncXClusterConfig"),

  CreateSupportBundle("CreateSupportBundle"),

  // Tasks belonging to subtasks classpath
  AnsibleClusterServerCtl("subtasks.AnsibleClusterServerCtl"),

  AnsibleConfigureServers("subtasks.AnsibleConfigureServers"),

  AnsibleDestroyServer("subtasks.AnsibleDestroyServer"),

  PauseServer("subtasks.PauseServer"),

  ResumeServer("subtasks.ResumeServer"),

  AnsibleSetupServer("subtasks.AnsibleSetupServer"),

  AnsibleCreateServer("subtasks.AnsibleCreateServer"),

  PrecheckNode("subtasks.PrecheckNode"),

  PrecheckNodeDetached("subtasks.PrecheckNodeDetached"),

  AnsibleUpdateNodeInfo("subtasks.AnsibleUpdateNodeInfo"),

  BulkImport("subtasks.BulkImport"),

  ChangeMasterConfig("subtasks.ChangeMasterConfig"),

  ChangeAdminPassword("subtasks.ChangeAdminPassword"),

  CreateTable("subtasks.CreateTable"),

  DeleteNode("subtasks.DeleteNode"),

  DeleteBackup("subtasks.DeleteBackup"),

  DeleteCustomerConfig("DeleteCustomerConfig"),

  UpdateNodeProcess("subtasks.nodes.UpdateNodeProcess"),

  DeleteTableFromUniverse("subtasks.DeleteTableFromUniverse"),

  LoadBalancerStateChange("subtasks.LoadBalancerStateChange"),

  ModifyBlackList("subtasks.ModifyBlackList"),

  ManipulateDnsRecordTask("subtasks.ManipulateDnsRecordTask"),

  RemoveUniverseEntry("subtasks.RemoveUniverseEntry"),

  SetFlagInMemory("subtasks.SetFlagInMemory"),

  SetNodeState("subtasks.SetNodeState"),

  SwamperTargetsFileUpdate("subtasks.SwamperTargetsFileUpdate"),

  UniverseUpdateSucceeded("subtasks.UniverseUpdateSucceeded"),

  UpdateAndPersistGFlags("subtasks.UpdateAndPersistGFlags"),

  UpdatePlacementInfo("subtasks.UpdatePlacementInfo"),

  UpdateSoftwareVersion("subtasks.UpdateSoftwareVersion"),

  WaitForDataMove("subtasks.WaitForDataMove"),

  WaitForLeaderBlacklistCompletion("subtasks.WaitForLeaderBlacklistCompletion"),

  WaitForFollowerLag("subtasks.WaitForFollowerLag"),

  WaitForLoadBalance("subtasks.WaitForLoadBalance"),

  WaitForMasterLeader("subtasks.WaitForMasterLeader"),

  WaitForServer("subtasks.WaitForServer"),

  WaitForTServerHeartBeats("subtasks.WaitForTServerHeartBeats"),

  DeleteClusterFromUniverse("subtasks.DeleteClusterFromUniverse"),

  InstanceActions("subtasks.InstanceActions"),

  WaitForServerReady("subtasks.WaitForServerReady"),

  RunExternalScript("subtasks.RunExternalScript"),

  // Tasks belonging to subtasks.xcluster classpath
  XClusterConfigSetup("subtasks.xcluster.XClusterConfigSetup"),

  XClusterConfigSetStatus("subtasks.xcluster.XClusterConfigSetStatus"),

  XClusterConfigModifyTables("subtasks.xcluster.XClusterConfigModifyTables"),

  XClusterConfigRename("subtasks.xcluster.XClusterConfigRename"),

  XClusterConfigDelete("subtasks.xcluster.XClusterConfigDelete"),

  XClusterConfigSync("subtasks.xcluster.XClusterConfigSync"),

  // Tasks belonging to subtasks.cloud classpath
  CloudAccessKeyCleanup("subtasks.cloud.CloudAccessKeyCleanup"),

  CloudAccessKeySetup("subtasks.cloud.CloudAccessKeySetup"),

  CloudInitializer("subtasks.cloud.CloudInitializer"),

  CloudProviderCleanup("subtasks.cloud.CloudProviderCleanup"),

  CloudRegionCleanup("subtasks.cloud.CloudRegionCleanup"),

  CloudRegionSetup("subtasks.cloud.CloudRegionSetup"),

  CloudSetup("subtasks.cloud.CloudSetup"),

  BackupTable("subtasks.BackupTable"),

  BackupTableYb("subtasks.BackupTableYb"),

  BackupUniverseKeys("subtasks.BackupUniverseKeys"),

  RestoreUniverseKeys("subtasks.RestoreUniverseKeys"),

  WaitForLeadersOnPreferredOnly("subtasks.WaitForLeadersOnPreferredOnly"),

  EnableEncryptionAtRest("subtasks.EnableEncryptionAtRest"),

  DisableEncryptionAtRest("subtasks.DisableEncryptionAtRest"),

  DestroyEncryptionAtRest("subtasks.DestroyEncryptionAtRest"),

  KubernetesCommandExecutor("subtasks.KubernetesCommandExecutor"),

  KubernetesWaitForPod("subtasks.KubernetesWaitForPod"),

  KubernetesCheckNumPod("subtasks.KubernetesCheckNumPod"),

  SetActiveUniverseKeys("subtasks.SetActiveUniverseKeys"),

  @Deprecated
  CopyEncryptionKeyFile("subtasks.CopyEncryptionKeyFile"),

  WaitForEncryptionKeyInMemory("subtasks.WaitForEncryptionKeyInMemory"),

  UnivSetCertificate("subtasks.UnivSetCertificate"),

  CreateAlertDefinitions("subtasks.CreateAlertDefinitions"),

  UniverseSetTlsParams("subtasks.UniverseSetTlsParams"),

  UniverseUpdateRootCert("subtasks.UniverseUpdateRootCert"),

  ResetUniverseVersion("subtasks.ResetUniverseVersion"),

  DeleteCertificate("subtasks.DeleteCertificate"),

  SetNodeStatus("subtasks.SetNodeStatus"),

  CheckMasterLeader("subtasks.check.CheckMasterLeader"),

  CheckMasters("subtasks.check.CheckMasters"),

  CheckTServers("subtasks.check.CheckTServers"),

  WaitForTServerHBs("subtasks.check.WaitForTServerHBs"),

  CreatePrometheusSwamperConfig("subtasks.CreatePrometheusSwamperConfig"),

  PreflightNodeCheck("subtasks.PreflightNodeCheck");

  private String relativeClassPath;

  TaskType(String relativeClassPath) {
    this.relativeClassPath = relativeClassPath;
  }

  @Override
  public String toString() {
    return this.relativeClassPath;
  }

  public static List<TaskType> filteredValues() {
    return Arrays.stream(TaskType.values())
        .filter(
            value -> {
              try {
                Field field = TaskType.class.getField(value.name());
                return !field.isAnnotationPresent(Deprecated.class);
              } catch (Exception e) {
                return false;
              }
            })
        .collect(Collectors.toList());
  }
}
