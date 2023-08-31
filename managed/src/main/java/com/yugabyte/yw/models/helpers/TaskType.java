package com.yugabyte.yw.models.helpers;

import com.google.common.collect.ImmutableMap;
import java.lang.reflect.Field;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.stream.Collectors;

/** These are the various types of user tasks and internal tasks. */
public enum TaskType {

  // Tasks that are CustomerTasks
  CloudBootstrap("CloudBootstrap"),

  CloudCleanup("CloudCleanup"),

  CreateCassandraTable("CreateCassandraTable"),

  CreateUniverse("CreateUniverse"),

  ReadOnlyClusterCreate("ReadOnlyClusterCreate"),

  ReadOnlyKubernetesClusterDelete("ReadOnlyKubernetesClusterDelete"),

  ReadOnlyClusterDelete("ReadOnlyClusterDelete"),

  CreateKubernetesUniverse("CreateKubernetesUniverse"),

  ReadOnlyKubernetesClusterCreate("ReadOnlyKubernetesClusterCreate"),

  DestroyUniverse("DestroyUniverse"),

  PauseUniverse("PauseUniverse"),

  ResumeUniverse("ResumeUniverse"),

  DestroyKubernetesUniverse("DestroyKubernetesUniverse"),

  DeleteTable("DeleteTable"),

  BackupUniverse("BackupUniverse"),

  RestoreBackup("RestoreBackup"),

  MultiTableBackup("MultiTableBackup"),

  CreateBackup("CreateBackup"),

  CreatePitrConfig("CreatePitrConfig"),

  RestoreSnapshotSchedule("RestoreSnapshotSchedule"),

  EditUniverse("EditUniverse"),

  EditKubernetesUniverse("EditKubernetesUniverse"),

  ExternalScript("ExternalScript"),

  @Deprecated
  KubernetesProvision("KubernetesProvision"),

  ImportIntoTable("ImportIntoTable"),

  RunApiTriggeredHooks("RunApiTriggeredHooks"),

  // TODO: Mark it as deprecated once UpgradeUniverse related APIs are removed
  UpgradeUniverse("UpgradeUniverse"),

  RestartUniverse("upgrade.RestartUniverse"),

  RestartUniverseKubernetesUpgrade("upgrade.RestartUniverseKubernetesUpgrade"),

  SoftwareUpgrade("upgrade.SoftwareUpgrade"),

  SoftwareKubernetesUpgrade("upgrade.SoftwareKubernetesUpgrade"),

  KubernetesOverridesUpgrade("upgrade.KubernetesOverridesUpgrade"),

  GFlagsUpgrade("upgrade.GFlagsUpgrade"),

  GFlagsKubernetesUpgrade("upgrade.GFlagsKubernetesUpgrade"),

  CertsRotate("upgrade.CertsRotate"),

  CertsRotateKubernetesUpgrade("upgrade.CertsRotateKubernetesUpgrade"),

  TlsToggle("upgrade.TlsToggle"),

  NodeCertReloadTask("subtasks.NodeCertReloadTask"),

  VMImageUpgrade("upgrade.VMImageUpgrade"),

  SystemdUpgrade("upgrade.SystemdUpgrade"),

  RebootUniverse("upgrade.RebootUniverse"),

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

  RebootNodeInUniverse("RebootNodeInUniverse"),

  ReleaseInstanceFromUniverse("ReleaseInstanceFromUniverse"),

  RotateAccessKey("RotateAccessKey"),

  SetUniverseKey("SetUniverseKey"),

  CreateAndRotateAccessKey("CreateAndRotateAccessKey"),

  @Deprecated
  SetKubernetesUniverseKey("SetKubernetesUniverseKey"),

  CreateKMSConfig("CreateKMSConfig"),

  EditKMSConfig("EditKMSConfig"),

  DeleteKMSConfig("DeleteKMSConfig"),

  UpdateDiskSize("UpdateDiskSize"),

  UpdateKubernetesDiskSize("UpdateKubernetesDiskSize"),

  KubernetesCheckStorageClass("subtasks.KubernetesCheckStorageClass"),

  StartMasterOnNode("StartMasterOnNode"),

  CreateXClusterConfig("CreateXClusterConfig"),

  EditXClusterConfig("EditXClusterConfig"),

  RestartXClusterConfig("RestartXClusterConfig"),

  DeleteXClusterConfig("DeleteXClusterConfig"),

  SyncXClusterConfig("SyncXClusterConfig"),

  CreateSupportBundle("CreateSupportBundle"),

  // Tasks belonging to subtasks classpath
  AddAuthorizedKey("subtasks.AddAuthorizedKey"),

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

  DeleteBackupYb("subtasks.DeleteBackupYb"),

  DeleteCustomerConfig("DeleteCustomerConfig"),

  DeleteCustomerStorageConfig("DeleteCustomerStorageConfig"),

  UpdateNodeProcess("subtasks.nodes.UpdateNodeProcess"),

  DeleteTableFromUniverse("subtasks.DeleteTableFromUniverse"),

  DeleteTablesFromUniverse("subtasks.DeleteTablesFromUniverse"),

  LoadBalancerStateChange("subtasks.LoadBalancerStateChange"),

  ModifyBlackList("subtasks.ModifyBlackList"),

  ManipulateDnsRecordTask("subtasks.ManipulateDnsRecordTask"),

  RemoveUniverseEntry("subtasks.RemoveUniverseEntry"),

  SetFlagInMemory("subtasks.SetFlagInMemory"),

  SetNodeState("subtasks.SetNodeState"),

  SwamperTargetsFileUpdate("subtasks.SwamperTargetsFileUpdate"),

  UniverseUpdateSucceeded("subtasks.UniverseUpdateSucceeded"),

  UpdateAndPersistGFlags("subtasks.UpdateAndPersistGFlags"),

  UpdateAndPersistKubernetesOverrides("subtasks.UpdateAndPersistKubernetesOverrides"),

  UpdatePlacementInfo("subtasks.UpdatePlacementInfo"),

  UpdateSoftwareVersion("subtasks.UpdateSoftwareVersion"),

  UpdateUniverseYbcDetails("subtasks.UpdateUniverseYbcDetails"),

  VerifyNodeSSHAccess("subtasks.VerifyNodeSSHAccess"),

  WaitForDataMove("subtasks.WaitForDataMove"),

  WaitForLeaderBlacklistCompletion("subtasks.WaitForLeaderBlacklistCompletion"),

  WaitForFollowerLag("subtasks.WaitForFollowerLag"),

  WaitForLoadBalance("subtasks.WaitForLoadBalance"),

  WaitForMasterLeader("subtasks.WaitForMasterLeader"),

  WaitForServer("subtasks.WaitForServer"),

  WaitForYbcServer("subtasks.WaitForYbcServer"),

  WaitForTServerHeartBeats("subtasks.WaitForTServerHeartBeats"),

  DeleteClusterFromUniverse("subtasks.DeleteClusterFromUniverse"),

  InstanceActions("subtasks.InstanceActions"),

  WaitForServerReady("subtasks.WaitForServerReady"),

  RunExternalScript("subtasks.RunExternalScript"),

  RemoveAuthorizedKey("subtasks.RemoveAuthorizedKey"),

  UpdateUniverseAccessKey("subtasks.UpdateUniverseAccessKey"),

  // Tasks belonging to subtasks.xcluster classpath
  BootstrapProducer("subtasks.xcluster.BootstrapProducer"),

  CheckBootstrapRequired("subtasks.xcluster.CheckBootstrapRequired"),

  DeleteBootstrapIds("subtasks.xcluster.DeleteBootstrapIds"),

  DeleteReplication("subtasks.xcluster.DeleteReplication"),

  DeleteXClusterConfigEntry("subtasks.xcluster.DeleteXClusterConfigEntry"),

  ResetXClusterConfigEntry("subtasks.xcluster.ResetXClusterConfigEntry"),

  SetReplicationPaused("subtasks.xcluster.SetReplicationPaused"),

  SetRestoreTime("subtasks.xcluster.SetRestoreTime"),

  XClusterConfigSetup("subtasks.xcluster.XClusterConfigSetup"),

  XClusterConfigSetStatus("subtasks.xcluster.XClusterConfigSetStatus"),

  XClusterConfigSetStatusForTables("subtasks.xcluster.XClusterConfigSetStatusForTables"),

  XClusterConfigModifyTables("subtasks.xcluster.XClusterConfigModifyTables"),

  XClusterConfigRename("subtasks.xcluster.XClusterConfigRename"),

  XClusterConfigSync("subtasks.xcluster.XClusterConfigSync"),

  XClusterConfigUpdateMasterAddresses("subtasks.xcluster.XClusterConfigUpdateMasterAddresses"),

  XClusterInfoPersist("subtasks.xcluster.XClusterInfoPersist"),

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

  BackupTableYbc("subtasks.BackupTableYbc"),

  BackupUniverseKeys("subtasks.BackupUniverseKeys"),

  RestoreBackupYb("subtasks.RestoreBackupYb"),

  RestoreBackupYbc("subtasks.RestoreBackupYbc"),

  RestoreUniverseKeys("subtasks.RestoreUniverseKeys"),

  RestoreUniverseKeysYb("subtasks.RestoreUniverseKeysYb"),

  RestoreUniverseKeysYbc("subtasks.RestoreUniverseKeysYbc"),

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

  ManageAlertDefinitions("subtasks.ManageAlertDefinitions"),

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

  PreflightNodeCheck("subtasks.PreflightNodeCheck"),

  RunYsqlUpgrade("subtasks.RunYsqlUpgrade"),

  ResizeNode("upgrade.ResizeNode"),

  CheckMemory("subtasks.check.CheckMemory"),

  UpdateMountedDisks("subtasks.UpdateMountedDisks"),

  TransferXClusterCerts("subtasks.TransferXClusterCerts"),

  CreateTableSpacesInUniverse("CreateTableSpacesInUniverse"),

  CreateTableSpaces("subtasks.CreateTableSpaces"),

  ThirdpartySoftwareUpgrade("upgrade.ThirdpartySoftwareUpgrade"),

  MarkUniverseForHealthScriptReUpload("subtasks.MarkUniverseForHealthScriptReUpload"),

  RebootServer("subtasks.RebootServer"),

  HardRebootServer("subtasks.HardRebootServer"),

  RunHooks("subtasks.RunHooks"),

  UpdateUniverseTags("subtasks.UpdateUniverseTags"),

  UpgradeYbc("subtasks.UpgradeYbc"),

  InstallYbcSoftware("InstallYbcSoftware"),

  UpgradeUniverseYbc("UpgradeUniverseYbc"),

  DisableYbc("DisableYbc"),

  InstanceExistCheck("subtasks.InstanceExistCheck"),

  AddGFlagMetadata("AddGFlagMetadata"),

  InstallThirdPartySoftwareK8s("subtasks.InstallThirdPartySoftwareK8s");

  private String relativeClassPath;

  TaskType(String relativeClassPath) {
    this.relativeClassPath = relativeClassPath;
  }

  @Override
  public String toString() {
    return this.relativeClassPath;
  }

  /**
   * Used to fill in an active task metric value for universe. For now only universe related tasks
   * are marked with that.
   */
  private static final Map<TaskType, Integer> TASK_CODES_MAP =
      ImmutableMap.<TaskType, Integer>builder()
          // Cluster operations (1-29):
          .put(CreateKubernetesUniverse, 3)
          .put(CreateUniverse, 4)
          .put(DestroyKubernetesUniverse, 5)
          .put(DestroyUniverse, 6)
          .put(EditKubernetesUniverse, 7)
          .put(EditUniverse, 8)
          .put(PauseUniverse, 9)
          .put(ReadOnlyClusterCreate, 10)
          .put(ReadOnlyClusterDelete, 11)
          .put(ReadOnlyKubernetesClusterCreate, 12)
          .put(ReadOnlyKubernetesClusterDelete, 13)
          .put(ResumeUniverse, 14)
          // Upgrade/Maintenance (30-69):
          .put(CertsRotate, 30)
          .put(GFlagsUpgrade, 32)
          .put(RebootUniverse, 33)
          .put(ResizeNode, 34)
          .put(RestartUniverse, 35)
          .put(SoftwareUpgrade, 36)
          .put(SystemdUpgrade, 37)
          .put(ThirdpartySoftwareUpgrade, 38)
          .put(TlsToggle, 39)
          .put(VMImageUpgrade, 40)
          .put(UpdateDiskSize, 41)
          .put(UpgradeUniverse, 42)
          .put(CertsRotateKubernetesUpgrade, 43)
          .put(GFlagsKubernetesUpgrade, 45)
          .put(KubernetesOverridesUpgrade, 46)
          .put(RestartUniverseKubernetesUpgrade, 47)
          .put(SoftwareKubernetesUpgrade, 48)
          .put(UpdateKubernetesDiskSize, 49)
          .put(UpgradeKubernetesUniverse, 50)
          // Node operations (70-89):
          .put(AddNodeToUniverse, 70)
          .put(DeleteNodeFromUniverse, 71)
          .put(RebootNodeInUniverse, 72)
          .put(ReleaseInstanceFromUniverse, 73)
          .put(RemoveNodeFromUniverse, 74)
          .put(StartMasterOnNode, 75)
          .put(StartNodeInUniverse, 76)
          .put(StopNodeInUniverse, 77)
          // Backup/restore (90-109):
          .put(BackupUniverse, 90)
          .put(CreateBackup, 91)
          .put(CreatePitrConfig, 93)
          .put(DeleteCustomerConfig, 94)
          .put(DeleteCustomerStorageConfig, 95)
          .put(MultiTableBackup, 97)
          .put(RestoreBackup, 98)
          .put(RestoreSnapshotSchedule, 99)
          // Table ops (110-119):
          .put(CreateCassandraTable, 110)
          .put(CreateTableSpacesInUniverse, 111)
          .put(DeleteTable, 112)
          .put(ImportIntoTable, 113)
          // XCluster (120-129):
          .put(CreateXClusterConfig, 120)
          .put(DeleteXClusterConfig, 121)
          .put(EditXClusterConfig, 122)
          .put(RestartXClusterConfig, 123)
          .put(SyncXClusterConfig, 124)
          // Other (130+):
          .put(DisableYbc, 130)
          .put(InstallYbcSoftware, 131)
          .put(UpgradeUniverseYbc, 132)
          .put(RotateAccessKey, 133)
          .put(RunApiTriggeredHooks, 134)
          .put(SetUniverseKey, 135)
          .build();

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

  public Integer getCode() {
    return TASK_CODES_MAP.getOrDefault(this, 0);
  }
}
