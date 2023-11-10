package com.yugabyte.yw.models.helpers;

import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.ITask;
import java.lang.reflect.Field;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.stream.Collectors;

/** These are the various types of user tasks and internal tasks. */
public enum TaskType {

  // Tasks that are CustomerTasks
  CloudBootstrap(com.yugabyte.yw.commissioner.tasks.CloudBootstrap.class),

  CloudCleanup(com.yugabyte.yw.commissioner.tasks.CloudCleanup.class),

  CreateCassandraTable(com.yugabyte.yw.commissioner.tasks.CreateCassandraTable.class),

  CreateUniverse(com.yugabyte.yw.commissioner.tasks.CreateUniverse.class),

  ReadOnlyClusterCreate(com.yugabyte.yw.commissioner.tasks.ReadOnlyClusterCreate.class),

  ReadOnlyKubernetesClusterDelete(
      com.yugabyte.yw.commissioner.tasks.ReadOnlyKubernetesClusterDelete.class),

  ReadOnlyClusterDelete(com.yugabyte.yw.commissioner.tasks.ReadOnlyClusterDelete.class),

  CreateKubernetesUniverse(com.yugabyte.yw.commissioner.tasks.CreateKubernetesUniverse.class),

  ReadOnlyKubernetesClusterCreate(
      com.yugabyte.yw.commissioner.tasks.ReadOnlyKubernetesClusterCreate.class),

  DestroyUniverse(com.yugabyte.yw.commissioner.tasks.DestroyUniverse.class),

  PauseUniverse(com.yugabyte.yw.commissioner.tasks.PauseUniverse.class),

  ResumeUniverse(com.yugabyte.yw.commissioner.tasks.ResumeUniverse.class),

  DestroyKubernetesUniverse(com.yugabyte.yw.commissioner.tasks.DestroyKubernetesUniverse.class),

  DeleteTable(com.yugabyte.yw.commissioner.tasks.DeleteTable.class),

  BackupUniverse(com.yugabyte.yw.commissioner.tasks.BackupUniverse.class),

  RestoreBackup(com.yugabyte.yw.commissioner.tasks.RestoreBackup.class),

  MultiTableBackup(com.yugabyte.yw.commissioner.tasks.MultiTableBackup.class),

  CreateBackup(com.yugabyte.yw.commissioner.tasks.CreateBackup.class),

  ConfigureDBApis(com.yugabyte.yw.commissioner.tasks.upgrade.ConfigureDBApis.class),

  ConfigureDBApisKubernetes(
      com.yugabyte.yw.commissioner.tasks.upgrade.ConfigureDBApisKubernetes.class),

  CreatePitrConfig(com.yugabyte.yw.commissioner.tasks.CreatePitrConfig.class),

  RestorePitrConfig(com.yugabyte.yw.commissioner.tasks.RestorePitrConfig.class),

  DeletePitrConfig(com.yugabyte.yw.commissioner.tasks.DeletePitrConfig.class),

  RestoreSnapshotSchedule(com.yugabyte.yw.commissioner.tasks.RestoreSnapshotSchedule.class),

  EditUniverse(com.yugabyte.yw.commissioner.tasks.EditUniverse.class),

  EditKubernetesUniverse(com.yugabyte.yw.commissioner.tasks.EditKubernetesUniverse.class),

  ExternalScript(com.yugabyte.yw.commissioner.tasks.ExternalScript.class),

  @Deprecated
  KubernetesProvision(com.yugabyte.yw.commissioner.tasks.KubernetesProvision.class),

  ImportIntoTable(com.yugabyte.yw.commissioner.tasks.ImportIntoTable.class),

  RunApiTriggeredHooks(com.yugabyte.yw.commissioner.tasks.RunApiTriggeredHooks.class),

  AddOnClusterCreate(com.yugabyte.yw.commissioner.tasks.AddOnClusterCreate.class),

  AddOnClusterDelete(com.yugabyte.yw.commissioner.tasks.AddOnClusterDelete.class),

  // TODO: Mark it as deprecated once UpgradeUniverse related APIs are removed
  UpgradeUniverse(com.yugabyte.yw.commissioner.tasks.UpgradeUniverse.class),

  UpdateLoadBalancerConfig(com.yugabyte.yw.commissioner.tasks.UpdateLoadBalancerConfig.class),

  RestartUniverse(com.yugabyte.yw.commissioner.tasks.upgrade.RestartUniverse.class),

  RestartUniverseKubernetesUpgrade(
      com.yugabyte.yw.commissioner.tasks.upgrade.RestartUniverseKubernetesUpgrade.class),

  SoftwareUpgrade(com.yugabyte.yw.commissioner.tasks.upgrade.SoftwareUpgrade.class),

  SoftwareKubernetesUpgrade(
      com.yugabyte.yw.commissioner.tasks.upgrade.SoftwareKubernetesUpgrade.class),

  KubernetesOverridesUpgrade(
      com.yugabyte.yw.commissioner.tasks.upgrade.KubernetesOverridesUpgrade.class),

  GFlagsUpgrade(com.yugabyte.yw.commissioner.tasks.upgrade.GFlagsUpgrade.class),

  GFlagsKubernetesUpgrade(com.yugabyte.yw.commissioner.tasks.upgrade.GFlagsKubernetesUpgrade.class),

  CertsRotate(com.yugabyte.yw.commissioner.tasks.upgrade.CertsRotate.class),

  CertsRotateKubernetesUpgrade(
      com.yugabyte.yw.commissioner.tasks.upgrade.CertsRotateKubernetesUpgrade.class),

  TlsToggle(com.yugabyte.yw.commissioner.tasks.upgrade.TlsToggle.class),

  NodeCertReloadTask(com.yugabyte.yw.commissioner.tasks.subtasks.NodeCertReloadTask.class),

  UpdateUniverseConfig(com.yugabyte.yw.commissioner.tasks.subtasks.UpdateUniverseConfig.class),

  VMImageUpgrade(com.yugabyte.yw.commissioner.tasks.upgrade.VMImageUpgrade.class),

  SystemdUpgrade(com.yugabyte.yw.commissioner.tasks.upgrade.SystemdUpgrade.class),

  RebootUniverse(com.yugabyte.yw.commissioner.tasks.upgrade.RebootUniverse.class),

  CreateRootVolumes(com.yugabyte.yw.commissioner.tasks.subtasks.CreateRootVolumes.class),

  ReplaceRootVolume(com.yugabyte.yw.commissioner.tasks.subtasks.ReplaceRootVolume.class),

  ChangeInstanceType(com.yugabyte.yw.commissioner.tasks.subtasks.ChangeInstanceType.class),

  PersistResizeNode(com.yugabyte.yw.commissioner.tasks.subtasks.PersistResizeNode.class),

  PersistSystemdUpgrade(com.yugabyte.yw.commissioner.tasks.subtasks.PersistSystemdUpgrade.class),

  UpdateNodeDetails(com.yugabyte.yw.commissioner.tasks.subtasks.UpdateNodeDetails.class),

  UpgradeKubernetesUniverse(com.yugabyte.yw.commissioner.tasks.UpgradeKubernetesUniverse.class),

  DeleteNodeFromUniverse(com.yugabyte.yw.commissioner.tasks.DeleteNodeFromUniverse.class),

  StopNodeInUniverse(com.yugabyte.yw.commissioner.tasks.StopNodeInUniverse.class),

  StartNodeInUniverse(com.yugabyte.yw.commissioner.tasks.StartNodeInUniverse.class),

  AddNodeToUniverse(com.yugabyte.yw.commissioner.tasks.AddNodeToUniverse.class),

  RemoveNodeFromUniverse(com.yugabyte.yw.commissioner.tasks.RemoveNodeFromUniverse.class),

  RebootNodeInUniverse(com.yugabyte.yw.commissioner.tasks.RebootNodeInUniverse.class),

  ReleaseInstanceFromUniverse(com.yugabyte.yw.commissioner.tasks.ReleaseInstanceFromUniverse.class),

  RotateAccessKey(com.yugabyte.yw.commissioner.tasks.RotateAccessKey.class),

  SetUniverseKey(com.yugabyte.yw.commissioner.tasks.SetUniverseKey.class),

  CreateAndRotateAccessKey(com.yugabyte.yw.commissioner.tasks.CreateAndRotateAccessKey.class),

  @Deprecated
  SetKubernetesUniverseKey(null),

  CreateKMSConfig(com.yugabyte.yw.commissioner.tasks.CreateKMSConfig.class),

  EditKMSConfig(com.yugabyte.yw.commissioner.tasks.EditKMSConfig.class),

  DeleteKMSConfig(com.yugabyte.yw.commissioner.tasks.DeleteKMSConfig.class),

  UpdateDiskSize(com.yugabyte.yw.commissioner.tasks.UpdateDiskSize.class),

  UpdateKubernetesDiskSize(com.yugabyte.yw.commissioner.tasks.UpdateKubernetesDiskSize.class),

  KubernetesCheckVolumeExpansion(
      com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCheckVolumeExpansion.class),

  StartMasterOnNode(com.yugabyte.yw.commissioner.tasks.StartMasterOnNode.class),

  DeleteXClusterConfig(com.yugabyte.yw.commissioner.tasks.DeleteXClusterConfig.class),

  SyncXClusterConfig(com.yugabyte.yw.commissioner.tasks.SyncXClusterConfig.class),

  CreateSupportBundle(com.yugabyte.yw.commissioner.tasks.CreateSupportBundle.class),

  CreateXClusterConfig(com.yugabyte.yw.commissioner.tasks.CreateXClusterConfig.class),

  EditXClusterConfig(com.yugabyte.yw.commissioner.tasks.EditXClusterConfig.class),

  RestartXClusterConfig(com.yugabyte.yw.commissioner.tasks.RestartXClusterConfig.class),

  RestartDrConfig(com.yugabyte.yw.commissioner.tasks.RestartXClusterConfig.class),

  SyncDrConfig(com.yugabyte.yw.commissioner.tasks.SyncXClusterConfig.class),

  SetTablesDrConfig(com.yugabyte.yw.commissioner.tasks.EditXClusterConfig.class),

  CreateDrConfig(com.yugabyte.yw.commissioner.tasks.CreateDrConfig.class),

  DeleteDrConfig(com.yugabyte.yw.commissioner.tasks.DeleteDrConfig.class),

  FailoverDrConfig(com.yugabyte.yw.commissioner.tasks.FailoverDrConfig.class),

  SwitchoverDrConfig(com.yugabyte.yw.commissioner.tasks.SwitchoverDrConfig.class),

  EditDrConfig(com.yugabyte.yw.commissioner.tasks.EditDrConfig.class),

  ReinstallNodeAgent(com.yugabyte.yw.commissioner.tasks.ReinstallNodeAgent.class),

  // Tasks belonging to subtasks classpath
  AddAuthorizedKey(com.yugabyte.yw.commissioner.tasks.subtasks.AddAuthorizedKey.class),

  AnsibleClusterServerCtl(
      com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleClusterServerCtl.class),

  AnsibleConfigureServers(
      com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleConfigureServers.class),

  AnsibleDestroyServer(com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleDestroyServer.class),

  PauseServer(com.yugabyte.yw.commissioner.tasks.subtasks.PauseServer.class),

  ResumeServer(com.yugabyte.yw.commissioner.tasks.subtasks.ResumeServer.class),

  AnsibleSetupServer(com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleSetupServer.class),

  AnsibleCreateServer(com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleCreateServer.class),

  PrecheckNode(com.yugabyte.yw.commissioner.tasks.subtasks.PrecheckNode.class),

  PrecheckNodeDetached(com.yugabyte.yw.commissioner.tasks.subtasks.PrecheckNodeDetached.class),

  AnsibleUpdateNodeInfo(com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleUpdateNodeInfo.class),

  BulkImport(com.yugabyte.yw.commissioner.tasks.subtasks.BulkImport.class),

  ChangeMasterConfig(com.yugabyte.yw.commissioner.tasks.subtasks.ChangeMasterConfig.class),

  ChangeAdminPassword(com.yugabyte.yw.commissioner.tasks.subtasks.ChangeAdminPassword.class),

  CreateTable(com.yugabyte.yw.commissioner.tasks.subtasks.CreateTable.class),

  DeleteNode(com.yugabyte.yw.commissioner.tasks.subtasks.DeleteNode.class),

  DeleteBackup(com.yugabyte.yw.commissioner.tasks.subtasks.DeleteBackup.class),

  DeleteBackupYb(com.yugabyte.yw.commissioner.tasks.subtasks.DeleteBackupYb.class),

  DeleteCustomerConfig(com.yugabyte.yw.commissioner.tasks.DeleteCustomerConfig.class),

  DeleteCustomerStorageConfig(com.yugabyte.yw.commissioner.tasks.DeleteCustomerStorageConfig.class),

  UpdateNodeProcess(com.yugabyte.yw.commissioner.tasks.subtasks.nodes.UpdateNodeProcess.class),

  DeleteTableFromUniverse(
      com.yugabyte.yw.commissioner.tasks.subtasks.DeleteTableFromUniverse.class),

  DeleteTablesFromUniverse(
      com.yugabyte.yw.commissioner.tasks.subtasks.DeleteTablesFromUniverse.class),

  DeleteKeyspace(com.yugabyte.yw.commissioner.tasks.subtasks.DeleteKeyspace.class),

  LoadBalancerStateChange(
      com.yugabyte.yw.commissioner.tasks.subtasks.LoadBalancerStateChange.class),

  ModifyBlackList(com.yugabyte.yw.commissioner.tasks.subtasks.ModifyBlackList.class),

  CheckUnderReplicatedTablets(
      com.yugabyte.yw.commissioner.tasks.subtasks.CheckUnderReplicatedTablets.class),

  CheckFollowerLag(com.yugabyte.yw.commissioner.tasks.subtasks.CheckFollowerLag.class),

  ManipulateDnsRecordTask(
      com.yugabyte.yw.commissioner.tasks.subtasks.ManipulateDnsRecordTask.class),

  RemoveUniverseEntry(com.yugabyte.yw.commissioner.tasks.subtasks.RemoveUniverseEntry.class),

  SetFlagInMemory(com.yugabyte.yw.commissioner.tasks.subtasks.SetFlagInMemory.class),

  SetNodeState(com.yugabyte.yw.commissioner.tasks.subtasks.SetNodeState.class),

  SwamperTargetsFileUpdate(
      com.yugabyte.yw.commissioner.tasks.subtasks.SwamperTargetsFileUpdate.class),

  UniverseUpdateSucceeded(
      com.yugabyte.yw.commissioner.tasks.subtasks.UniverseUpdateSucceeded.class),

  UpdateAndPersistGFlags(com.yugabyte.yw.commissioner.tasks.subtasks.UpdateAndPersistGFlags.class),

  UpdateAndPersistKubernetesOverrides(
      com.yugabyte.yw.commissioner.tasks.subtasks.UpdateAndPersistKubernetesOverrides.class),

  UpdatePlacementInfo(com.yugabyte.yw.commissioner.tasks.subtasks.UpdatePlacementInfo.class),

  UpdateSoftwareVersion(com.yugabyte.yw.commissioner.tasks.subtasks.UpdateSoftwareVersion.class),

  UpdateUniverseYbcDetails(
      com.yugabyte.yw.commissioner.tasks.subtasks.UpdateUniverseYbcDetails.class),

  VerifyNodeSSHAccess(com.yugabyte.yw.commissioner.tasks.subtasks.VerifyNodeSSHAccess.class),

  WaitForDataMove(com.yugabyte.yw.commissioner.tasks.subtasks.WaitForDataMove.class),

  WaitForLeaderBlacklistCompletion(
      com.yugabyte.yw.commissioner.tasks.subtasks.WaitForLeaderBlacklistCompletion.class),

  WaitForFollowerLag(com.yugabyte.yw.commissioner.tasks.subtasks.WaitForFollowerLag.class),

  WaitForLoadBalance(com.yugabyte.yw.commissioner.tasks.subtasks.WaitForLoadBalance.class),

  WaitForMasterLeader(com.yugabyte.yw.commissioner.tasks.subtasks.WaitForMasterLeader.class),

  WaitForServer(com.yugabyte.yw.commissioner.tasks.subtasks.WaitForServer.class),

  WaitForYbcServer(com.yugabyte.yw.commissioner.tasks.subtasks.WaitForYbcServer.class),

  WaitForTServerHeartBeats(
      com.yugabyte.yw.commissioner.tasks.subtasks.WaitForTServerHeartBeats.class),

  DeleteClusterFromUniverse(
      com.yugabyte.yw.commissioner.tasks.subtasks.DeleteClusterFromUniverse.class),

  InstanceActions(com.yugabyte.yw.commissioner.tasks.subtasks.InstanceActions.class),

  WaitForServerReady(com.yugabyte.yw.commissioner.tasks.subtasks.WaitForServerReady.class),

  WaitForClockSync(com.yugabyte.yw.commissioner.tasks.subtasks.WaitForClockSync.class),

  WaitForDuration(com.yugabyte.yw.commissioner.tasks.subtasks.WaitForDuration.class),

  RunExternalScript(com.yugabyte.yw.commissioner.tasks.subtasks.RunExternalScript.class),

  RemoveAuthorizedKey(com.yugabyte.yw.commissioner.tasks.subtasks.RemoveAuthorizedKey.class),

  UpdateUniverseAccessKey(
      com.yugabyte.yw.commissioner.tasks.subtasks.UpdateUniverseAccessKey.class),

  ManageLoadBalancerGroup(
      com.yugabyte.yw.commissioner.tasks.subtasks.ManageLoadBalancerGroup.class),

  // Tasks belonging to subtasks.xcluster classpath
  BootstrapProducer(com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.BootstrapProducer.class),

  CheckBootstrapRequired(
      com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.CheckBootstrapRequired.class),

  DeleteBootstrapIds(com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.DeleteBootstrapIds.class),

  DeleteReplication(com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.DeleteReplication.class),

  DeleteXClusterConfigEntry(
      com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.DeleteXClusterConfigEntry.class),

  DeleteXClusterTableConfigEntry(
      com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.DeleteXClusterTableConfigEntry.class),

  DeleteDrConfigEntry(com.yugabyte.yw.commissioner.tasks.subtasks.DeleteDrConfigEntry.class),

  WaitForReplicationDrain(
      com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.WaitForReplicationDrain.class),

  ResetXClusterConfigEntry(
      com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.ResetXClusterConfigEntry.class),

  SetReplicationPaused(
      com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.SetReplicationPaused.class),

  ChangeXClusterRole(com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.ChangeXClusterRole.class),

  SetDrStates(com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.SetDrStates.class),

  SetRestoreTime(com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.SetRestoreTime.class),

  XClusterConfigSetup(
      com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.XClusterConfigSetup.class),

  XClusterConfigSetStatus(
      com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.XClusterConfigSetStatus.class),

  XClusterConfigSetStatusForTables(
      com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.XClusterConfigSetStatusForTables.class),

  XClusterConfigModifyTables(
      com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.XClusterConfigModifyTables.class),

  XClusterConfigRename(
      com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.XClusterConfigRename.class),

  XClusterConfigSync(com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.XClusterConfigSync.class),

  XClusterConfigUpdateMasterAddresses(
      com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.XClusterConfigUpdateMasterAddresses
          .class),

  XClusterInfoPersist(
      com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.XClusterInfoPersist.class),

  ReplicateNamespaces(
      com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.ReplicateNamespaces.class),

  CheckXUniverseAutoFlags(
      com.yugabyte.yw.commissioner.tasks.subtasks.check.CheckXUniverseAutoFlags.class),

  PromoteSecondaryConfigToMainConfig(
      com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.PromoteSecondaryConfigToMainConfig
          .class),

  // Tasks belonging to subtasks.cloud classpath
  CloudAccessKeyCleanup(
      com.yugabyte.yw.commissioner.tasks.subtasks.cloud.CloudAccessKeyCleanup.class),

  CloudAccessKeySetup(com.yugabyte.yw.commissioner.tasks.subtasks.cloud.CloudAccessKeySetup.class),

  CloudInitializer(com.yugabyte.yw.commissioner.tasks.subtasks.cloud.CloudInitializer.class),

  CloudProviderCleanup(
      com.yugabyte.yw.commissioner.tasks.subtasks.cloud.CloudProviderCleanup.class),

  CloudRegionCleanup(com.yugabyte.yw.commissioner.tasks.subtasks.cloud.CloudRegionCleanup.class),

  CloudRegionSetup(com.yugabyte.yw.commissioner.tasks.subtasks.cloud.CloudRegionSetup.class),

  CloudSetup(com.yugabyte.yw.commissioner.tasks.subtasks.cloud.CloudSetup.class),

  BackupTable(com.yugabyte.yw.commissioner.tasks.subtasks.BackupTable.class),

  BackupTableYb(com.yugabyte.yw.commissioner.tasks.subtasks.BackupTableYb.class),

  BackupTableYbc(com.yugabyte.yw.commissioner.tasks.subtasks.BackupTableYbc.class),

  BackupUniverseKeys(com.yugabyte.yw.commissioner.tasks.subtasks.BackupUniverseKeys.class),

  RestoreBackupYb(com.yugabyte.yw.commissioner.tasks.subtasks.RestoreBackupYb.class),

  RestoreBackupYbc(com.yugabyte.yw.commissioner.tasks.subtasks.RestoreBackupYbc.class),

  RestoreUniverseKeys(com.yugabyte.yw.commissioner.tasks.subtasks.RestoreUniverseKeys.class),

  RestoreUniverseKeysYb(com.yugabyte.yw.commissioner.tasks.subtasks.RestoreUniverseKeysYb.class),

  RestoreUniverseKeysYbc(com.yugabyte.yw.commissioner.tasks.subtasks.RestoreUniverseKeysYbc.class),

  WaitForLeadersOnPreferredOnly(
      com.yugabyte.yw.commissioner.tasks.subtasks.WaitForLeadersOnPreferredOnly.class),

  EnableEncryptionAtRest(com.yugabyte.yw.commissioner.tasks.subtasks.EnableEncryptionAtRest.class),

  DisableEncryptionAtRest(
      com.yugabyte.yw.commissioner.tasks.subtasks.DisableEncryptionAtRest.class),

  DestroyEncryptionAtRest(
      com.yugabyte.yw.commissioner.tasks.subtasks.DestroyEncryptionAtRest.class),

  KubernetesCommandExecutor(
      com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor.class),

  KubernetesWaitForPod(com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesWaitForPod.class),

  KubernetesCheckNumPod(com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCheckNumPod.class),

  SetActiveUniverseKeys(com.yugabyte.yw.commissioner.tasks.subtasks.SetActiveUniverseKeys.class),

  @Deprecated
  CopyEncryptionKeyFile(null),

  WaitForEncryptionKeyInMemory(
      com.yugabyte.yw.commissioner.tasks.subtasks.WaitForEncryptionKeyInMemory.class),

  UnivSetCertificate(com.yugabyte.yw.commissioner.tasks.subtasks.UnivSetCertificate.class),

  CreateAlertDefinitions(com.yugabyte.yw.commissioner.tasks.subtasks.CreateAlertDefinitions.class),

  ManageAlertDefinitions(com.yugabyte.yw.commissioner.tasks.subtasks.ManageAlertDefinitions.class),

  UniverseSetTlsParams(com.yugabyte.yw.commissioner.tasks.subtasks.UniverseSetTlsParams.class),

  UniverseUpdateRootCert(com.yugabyte.yw.commissioner.tasks.subtasks.UniverseUpdateRootCert.class),

  ResetUniverseVersion(com.yugabyte.yw.commissioner.tasks.subtasks.ResetUniverseVersion.class),

  DeleteCertificate(com.yugabyte.yw.commissioner.tasks.subtasks.DeleteCertificate.class),

  SetNodeStatus(com.yugabyte.yw.commissioner.tasks.subtasks.SetNodeStatus.class),

  CheckMasterLeader(com.yugabyte.yw.commissioner.tasks.subtasks.check.CheckMasterLeader.class),

  CheckMasters(com.yugabyte.yw.commissioner.tasks.subtasks.check.CheckMasters.class),

  CheckTServers(com.yugabyte.yw.commissioner.tasks.subtasks.check.CheckTServers.class),

  WaitForTServerHBs(com.yugabyte.yw.commissioner.tasks.subtasks.check.WaitForTServerHBs.class),

  CreatePrometheusSwamperConfig(
      com.yugabyte.yw.commissioner.tasks.subtasks.CreatePrometheusSwamperConfig.class),

  PreflightNodeCheck(com.yugabyte.yw.commissioner.tasks.subtasks.PreflightNodeCheck.class),

  RunYsqlUpgrade(com.yugabyte.yw.commissioner.tasks.subtasks.RunYsqlUpgrade.class),

  PromoteAutoFlags(com.yugabyte.yw.commissioner.tasks.subtasks.PromoteAutoFlags.class),

  RollbackAutoFlags(com.yugabyte.yw.commissioner.tasks.subtasks.RollbackAutoFlags.class),

  StoreAutoFlagConfigVersion(
      com.yugabyte.yw.commissioner.tasks.subtasks.StoreAutoFlagConfigVersion.class),

  CheckUpgrade(com.yugabyte.yw.commissioner.tasks.subtasks.check.CheckUpgrade.class),

  ResizeNode(com.yugabyte.yw.commissioner.tasks.upgrade.ResizeNode.class),

  CheckMemory(com.yugabyte.yw.commissioner.tasks.subtasks.check.CheckMemory.class),

  CheckSoftwareVersion(
      com.yugabyte.yw.commissioner.tasks.subtasks.check.CheckSoftwareVersion.class),

  UpdateMountedDisks(com.yugabyte.yw.commissioner.tasks.subtasks.UpdateMountedDisks.class),

  TransferXClusterCerts(com.yugabyte.yw.commissioner.tasks.subtasks.TransferXClusterCerts.class),

  CreateTableSpacesInUniverse(com.yugabyte.yw.commissioner.tasks.CreateTableSpacesInUniverse.class),

  CreateTableSpaces(com.yugabyte.yw.commissioner.tasks.subtasks.CreateTableSpaces.class),

  ThirdpartySoftwareUpgrade(
      com.yugabyte.yw.commissioner.tasks.upgrade.ThirdpartySoftwareUpgrade.class),

  ModifyAuditLoggingConfig(
      com.yugabyte.yw.commissioner.tasks.upgrade.ModifyAuditLoggingConfig.class),

  MarkUniverseForHealthScriptReUpload(
      com.yugabyte.yw.commissioner.tasks.subtasks.MarkUniverseForHealthScriptReUpload.class),

  RebootServer(com.yugabyte.yw.commissioner.tasks.subtasks.RebootServer.class),

  HardRebootServer(com.yugabyte.yw.commissioner.tasks.subtasks.HardRebootServer.class),

  RunHooks(com.yugabyte.yw.commissioner.tasks.subtasks.RunHooks.class),

  UpdateUniverseTags(com.yugabyte.yw.commissioner.tasks.subtasks.UpdateUniverseTags.class),

  UpgradeYbc(com.yugabyte.yw.commissioner.tasks.subtasks.UpgradeYbc.class),

  InstallYbcSoftware(com.yugabyte.yw.commissioner.tasks.InstallYbcSoftware.class),

  InstallYbcSoftwareOnK8s(
      com.yugabyte.yw.commissioner.tasks.subtasks.InstallYbcSoftwareOnK8s.class),

  UpgradeUniverseYbc(com.yugabyte.yw.commissioner.tasks.UpgradeUniverseYbc.class),

  DisableYbc(com.yugabyte.yw.commissioner.tasks.DisableYbc.class),

  InstanceExistCheck(com.yugabyte.yw.commissioner.tasks.subtasks.InstanceExistCheck.class),

  AddGFlagMetadata(com.yugabyte.yw.commissioner.tasks.AddGFlagMetadata.class),

  DeleteRootVolumes(com.yugabyte.yw.commissioner.tasks.subtasks.DeleteRootVolumes.class),

  InstallingThirdPartySoftware(
      com.yugabyte.yw.commissioner.tasks.subtasks.InstallThirdPartySoftwareK8s.class),

  InstallNodeAgent(com.yugabyte.yw.commissioner.tasks.subtasks.InstallNodeAgent.class),

  WaitForNodeAgent(com.yugabyte.yw.commissioner.tasks.subtasks.WaitForNodeAgent.class),

  CloudProviderDelete(com.yugabyte.yw.commissioner.tasks.CloudProviderDelete.class),

  CloudImageBundleSetup(
      com.yugabyte.yw.commissioner.tasks.subtasks.cloud.CloudImageBundleSetup.class),

  UpdateClusterUserIntent(
      com.yugabyte.yw.commissioner.tasks.subtasks.UpdateClusterUserIntent.class),

  UpdateClusterAPIDetails(
      com.yugabyte.yw.commissioner.tasks.subtasks.UpdateClusterAPIDetails.class),

  UpdateUniverseState(
      com.yugabyte.yw.commissioner.tasks.subtasks.UpdateUniverseSoftwareUpgradeState.class),

  UpdateUniverseCommunicationPorts(
      com.yugabyte.yw.commissioner.tasks.subtasks.UpdateUniverseCommunicationPorts.class),

  CreateBackupSchedule(com.yugabyte.yw.commissioner.tasks.CreateBackupSchedule.class),

  YBCBackupSucceeded(com.yugabyte.yw.commissioner.tasks.subtasks.YBCBackupSucceeded.class),

  CloudProviderEdit(com.yugabyte.yw.commissioner.tasks.CloudProviderEdit.class),

  SoftwareUpgradeYB(com.yugabyte.yw.commissioner.tasks.upgrade.SoftwareUpgradeYB.class),

  SoftwareKubernetesUpgradeYB(
      com.yugabyte.yw.commissioner.tasks.upgrade.SoftwareKubernetesUpgradeYB.class),

  FinalizeUpgrade(com.yugabyte.yw.commissioner.tasks.upgrade.FinalizeUpgrade.class),

  RollbackUpgrade(com.yugabyte.yw.commissioner.tasks.upgrade.RollbackUpgrade.class),

  RollbackKubernetesUpgrade(
      com.yugabyte.yw.commissioner.tasks.upgrade.RollbackKubernetesUpgrade.class),

  ReprovisionNode(com.yugabyte.yw.commissioner.tasks.ReprovisionNode.class),

  UpdateUniverseIntent(com.yugabyte.yw.commissioner.tasks.subtasks.UpdateUniverseIntent.class),

  FreezeUniverse(com.yugabyte.yw.commissioner.tasks.subtasks.FreezeUniverse.class),

  LdapUniverseSync(com.yugabyte.yw.commissioner.tasks.LdapUnivSync.class),

  // Tasks belonging to subtasks.ldap classpath
  QueryLdapServer(com.yugabyte.yw.commissioner.tasks.subtasks.ldapsync.QueryLdapServer.class),

  DbLdapSync(com.yugabyte.yw.commissioner.tasks.subtasks.ldapsync.DbLdapSync.class);

  private final Class<? extends ITask> taskClass;

  /**
   * Used to fill in an active task metric value for universe. For now only universe related tasks
   * are marked with that.
   */
  private static final Map<TaskType, Integer> TASK_CODES_MAP =
      ImmutableMap.<TaskType, Integer>builder()
          // Cluster operations (1-29):
          .put(AddOnClusterCreate, 1)
          .put(AddOnClusterDelete, 2)
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
          .put(ConfigureDBApis, 31)
          .put(GFlagsUpgrade, 32)
          .put(RebootUniverse, 33)
          .put(ResizeNode, 34)
          .put(RestartUniverse, 35)
          .put(SoftwareUpgrade, 36)
          .put(SoftwareUpgradeYB, 36)
          .put(SystemdUpgrade, 37)
          .put(ThirdpartySoftwareUpgrade, 38)
          .put(TlsToggle, 39)
          .put(VMImageUpgrade, 40)
          .put(UpdateDiskSize, 41)
          .put(UpgradeUniverse, 42)
          .put(CertsRotateKubernetesUpgrade, 43)
          .put(ConfigureDBApisKubernetes, 44)
          .put(GFlagsKubernetesUpgrade, 45)
          .put(KubernetesOverridesUpgrade, 46)
          .put(RestartUniverseKubernetesUpgrade, 47)
          .put(SoftwareKubernetesUpgrade, 48)
          .put(UpdateKubernetesDiskSize, 49)
          .put(UpgradeKubernetesUniverse, 50)
          .put(FinalizeUpgrade, 51)
          .put(RollbackUpgrade, 52)
          .put(SoftwareKubernetesUpgradeYB, 53)
          .put(RollbackKubernetesUpgrade, 54)
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
          .put(CreateBackupSchedule, 92)
          .put(CreatePitrConfig, 93)
          .put(DeleteCustomerConfig, 94)
          .put(DeleteCustomerStorageConfig, 95)
          .put(DeletePitrConfig, 96)
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
          .put(UpdateLoadBalancerConfig, 136)
          .put(LdapUniverseSync, 137)
          .build();

  TaskType(Class<? extends ITask> taskClass) {
    this.taskClass = taskClass;
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

  public Class<? extends ITask> getTaskClass() {
    return taskClass;
  }

  public Integer getCode() {
    return TASK_CODES_MAP.getOrDefault(this, 0);
  }
}
