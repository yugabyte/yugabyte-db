// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.UpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleConfigureServers;
import com.yugabyte.yw.commissioner.tasks.subtasks.CertReloadTaskCreator;
import com.yugabyte.yw.commissioner.tasks.subtasks.UniverseSetTlsParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.UniverseUpdateRootCert.UpdateRootCertAction;
import com.yugabyte.yw.commissioner.tasks.subtasks.UpdateUniverseConfig;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.NodeManager.CertRotateAction;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.forms.CertsRotateParams;
import com.yugabyte.yw.forms.CertsRotateParams.CertRotationType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeOption;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.ConfigType;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.io.File;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.Objects;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Abortable
@Retryable
public class CertsRotate extends UpgradeTaskBase {

  @Inject
  protected CertsRotate(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected CertsRotateParams taskParams() {
    return (CertsRotateParams) taskParams;
  }

  @Override
  public SubTaskGroupType getTaskSubGroupType() {
    return SubTaskGroupType.RotatingCert;
  }

  @Override
  public NodeState getNodeState() {
    return NodeState.UpdateCert;
  }

  @Override
  public void validateParams(boolean isFirstTry) {
    super.validateParams(isFirstTry);
    Universe universe = getUniverse();
    taskParams().verifyParams(universe, isFirstTry);
  }

  @Override
  protected void createPrecheckTasks(Universe universe) {
    super.createPrecheckTasks(universe);
    // Skip running prechecks if Node2Node certs has expired
    if (!CertificateHelper.checkNode2NodeCertsExpiry(universe)) {
      addBasicPrecheckTasks();
    }
    createCheckCertificateConfigTask(universe);
  }

  @Override
  protected MastersAndTservers calculateNodesToBeRestarted() {
    return fetchNodes(taskParams().upgradeOption);
  }

  @Override
  public void run() {
    runUpgrade(
        () -> {
          Universe universe = getUniverse();
          MastersAndTservers nodes = getNodesToBeRestarted();
          Set<NodeDetails> allNodes = toOrderedSet(nodes.asPair());
          // For rootCA root certificate rotation, we would need to do it in three rounds
          // so that node to node communications are not disturbed during the upgrade.
          // For other cases we can do it in one round by updating respective certs.
          // This 3-round process works for both ROLLING_UPGRADE and NON_RESTART_UPGRADE.
          if ((taskParams().upgradeOption == UpgradeOption.ROLLING_UPGRADE
                  || taskParams().upgradeOption == UpgradeOption.NON_RESTART_UPGRADE)
              && taskParams().rootCARotationType == CertRotationType.RootCert) {
            // Update the rootCA in platform to generate a multi-cert containing both old cert and
            // new cert. If it is already a multi-cert or the root CA of the universe is already
            // pointing to the new root CA, it does not do anything.
            createUniverseUpdateRootCertTask(UpdateRootCertAction.MultiCert);
            // Append new root cert to the existing ca.crt. If the cert file already contains
            // multiple certs on the DB node, it does not do anything.
            createCertUpdateTasks(allNodes, CertRotateAction.APPEND_NEW_ROOT_CERT);

            // Add task to use the updated certs
            createActivateCertsTask(universe, nodes, taskParams().upgradeOption, false);

            // Copy new server certs to all nodes. This deletes the old certs and uploads the new
            // files to the same location.
            createCertUpdateTasks(allNodes, CertRotateAction.ROTATE_CERTS);

            // Add task to use the updated certs.
            createActivateCertsTask(getUniverse(), nodes, taskParams().upgradeOption, false);

            // Remove old root cert from the ca.crt by replacing (moving) the old cert with the new
            // one.
            createCertUpdateTasks(allNodes, CertRotateAction.REMOVE_OLD_ROOT_CERT);
            // Update gflags of cert directories.
            createUpdateCertDirsTask(nodes.mastersList, ServerType.MASTER);
            createUpdateCertDirsTask(nodes.tserversList, ServerType.TSERVER);

            // Reset the old rootCA content in platform. This sets the rootCA back to the old cert.
            // Upon failure, there can be time window where universe still points to old cert, but
            // retry should work.
            createUniverseUpdateRootCertTask(UpdateRootCertAction.Reset);
            // Update universe details with new cert values. This sets the universe root cert to the
            // new one.
            createUniverseSetTlsParamsTask(getTaskSubGroupType());

            // Add task to use the updated certs.
            createActivateCertsTask(
                getUniverse(), nodes, taskParams().upgradeOption, taskParams().isYbcInstalled());

          } else {
            // Update the rootCA in platform to have both old cert and new cert.
            if (taskParams().rootCARotationType == CertRotationType.RootCert) {
              createUniverseUpdateRootCertTask(UpdateRootCertAction.MultiCert);
            }
            createCertUpdateTasks(
                nodes.mastersList,
                nodes.tserversList,
                getTaskSubGroupType(),
                taskParams().rootCARotationType,
                taskParams().clientRootCARotationType);

            // Add task to use the updated certs.
            createActivateCertsTask(
                getUniverse(), nodes, taskParams().upgradeOption, taskParams().isYbcInstalled());

            // Reset the old rootCA content in platform.
            if (taskParams().rootCARotationType == CertRotationType.RootCert) {
              createUniverseUpdateRootCertTask(UpdateRootCertAction.Reset);
            }
            // Update universe details with new cert values.
            createUniverseSetTlsParamsTask(getTaskSubGroupType());
          }

          if (taskParams().rootCARotationType == CertRotationType.RootCert) {
            // Transfer the new cert to the target universes for the xCluster configs where this
            // universe is the source.
            createUpdateCertsOnXClusterUniversesTask(universe);
          }

          // Restart is scheduled to happen, so 'client cert dir' gflag will be added
          // So configure cert reloading on universe, if not already.
          if (!isCertReloadConfigured(universe)) {
            createCertReloadConfigTask(universe);
            log.info("cert reload configuration task scheduled for this universe");
          }
        },
        // Save the params set in setAdditionalTaskParams in verify params.
        // TODO move setting the params from verifyParams to freeze callback.
        u -> updateTaskDetailsInDB(taskParams()));
  }

  private void createCertUpdateTasks(
      Collection<NodeDetails> nodes, CertRotateAction certRotateAction) {
    createCertUpdateTasks(
        nodes,
        certRotateAction,
        getTaskSubGroupType(),
        taskParams().rootCARotationType,
        taskParams().clientRootCARotationType);
  }

  private void createUpdateCertDirsTask(Collection<NodeDetails> nodes, ServerType serverType) {
    createUpdateCertDirsTask(nodes, serverType, getTaskSubGroupType());
  }

  // TODO: sort out the mess with rootAndClientRootCASame silently shadowing its namesake
  // in UniverseDefinitionTaskParams.
  // (referencing them through taskParams() may cause subtle bugs).
  @Override
  protected UniverseSetTlsParams.Params createSetTlsParams(SubTaskGroupType subTaskGroupType) {
    UniverseSetTlsParams.Params params = super.createSetTlsParams(subTaskGroupType);
    params.rootAndClientRootCASame = taskParams().rootAndClientRootCASame;
    return params;
  }

  @Override
  protected AnsibleConfigureServers.Params createCertUpdateParams(
      UserIntent userIntent,
      NodeDetails node,
      NodeManager.CertRotateAction certRotateAction,
      CertsRotateParams.CertRotationType rootCARotationType,
      CertsRotateParams.CertRotationType clientRootCARotationType) {
    AnsibleConfigureServers.Params params =
        super.createCertUpdateParams(
            userIntent, node, certRotateAction, rootCARotationType, clientRootCARotationType);
    params.rootAndClientRootCASame = taskParams().rootAndClientRootCASame;
    return params;
  }

  @Override
  protected AnsibleConfigureServers.Params createUpdateCertDirParams(
      UserIntent userIntent, NodeDetails node, ServerType serverType) {
    AnsibleConfigureServers.Params params =
        super.createUpdateCertDirParams(userIntent, node, serverType);
    params.rootAndClientRootCASame = taskParams().rootAndClientRootCASame;
    return params;
  }

  /**
   * compare the universe version against the versions where cert rotate is supported and
   * appropriately call 'cert rotate' for newer universes or 'restart nodes' for older universes.
   *
   * @param universe
   * @param nodes nodes which are to be activated with new certs
   * @param upgradeOption
   * @param ybcInstalled
   */
  private void createActivateCertsTask(
      Universe universe,
      MastersAndTservers nodes,
      UpgradeOption upgradeOption,
      boolean ybcInstalled) {

    boolean canReloadCert =
        isCertReloadable(universe) && !CertificateHelper.checkNode2NodeCertsExpiry(universe);
    if (taskParams().upgradeOption == UpgradeOption.NON_RESTART_UPGRADE) {
      if (!canReloadCert) {
        throw new RuntimeException(
            "Node-to-node certificates are expired, only non-rolling upgrade is supported");
      }
      // cert reload can be performed.
      log.info("adding cert rotate via reload task ...");
      createCertReloadTask(nodes, universe.getUniverseUUID(), getUserTaskUUID());
    } else {
      // Do a restart to rotate certificate.
      log.info("adding a cert rotate via restart task ...");
      createRestartTasks(nodes, upgradeOption, ybcInstalled);
    }
  }

  private boolean isCertReloadable(Universe universe) {
    boolean featureFlagEnabled = isCertReloadFeatureEnabled();
    boolean universeConfigured = isCertReloadConfigured(universe);
    log.debug(
        "cert reloadable => feature flag [{}], universe configured [{}]",
        featureFlagEnabled,
        universeConfigured);
    if (!featureFlagEnabled || !universeConfigured) {
      log.info(
          "hot cert reload cannot be performed. Feature flag [{}], universe configured [{}]",
          featureFlagEnabled,
          universeConfigured);
      return false;
    }
    return Util.compareYbVersions(
            universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion,
            "2.14.0.0-b1",
            true /* suppressFormatError */)
        >= 0;
  }

  private boolean isCertReloadFeatureEnabled() {
    return this.confGetter.getGlobalConf(GlobalConfKeys.enableCertReload);
  }

  private boolean isCertReloadConfigured(Universe universe) {
    // universe should have been configured for performing 'hot cert reload'.
    return Boolean.parseBoolean(
        universe
            .getConfig()
            .getOrDefault(Universe.KEY_CERT_HOT_RELOADABLE, Boolean.FALSE.toString()));
  }

  protected void createCertReloadTask(
      MastersAndTservers nodes, UUID universeUuid, UUID userTaskUuid) {

    if (nodes == null) {
      return; // nothing to do if node details are missing.
    }
    log.debug("creating certReloadTaskCreator ...");

    CertReloadTaskCreator taskCreator =
        new CertReloadTaskCreator(
            universeUuid, userTaskUuid, getRunnableTask(), getTaskExecutor(), nodes.mastersList);

    createNonRestartUpgradeTaskFlow(taskCreator, nodes, DEFAULT_CONTEXT);

    Universe universe = Universe.getOrBadRequest(universeUuid);
    if (universe.isYbcEnabled()) {

      createStopYbControllerTasks(nodes.getAllNodes())
          .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);

      createStartYbcTasks(nodes.getAllNodes())
          .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);

      // Wait for yb-controller to be responsive on each node.
      createWaitForYbcServerTask(nodes.getAllNodes())
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
    }
  }

  protected void createCertReloadConfigTask(Universe universe) {
    if (isCertReloadConfigured(universe)) {
      log.debug("Cert reload is already configured");
      return;
    }

    UpdateUniverseConfig task = createTask(UpdateUniverseConfig.class);
    UpdateUniverseConfig.Params params = new UpdateUniverseConfig.Params();
    params.setUniverseUUID(taskParams().getUniverseUUID());
    params.configs =
        Collections.singletonMap(Universe.KEY_CERT_HOT_RELOADABLE, Boolean.TRUE.toString());
    task.initialize(params);

    SubTaskGroup subTaskGroup = createSubTaskGroup("UpdateUniverseConfig");
    subTaskGroup.setSubTaskGroupType(getTaskSubGroupType());
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
  }

  public void createCheckCertificateConfigTask(Universe universe) {
    MastersAndTservers nodes = getNodesToBeRestarted();
    Set<NodeDetails> allNodes = toOrderedSet(nodes.asPair());
    createCheckCertificateConfigTask(
        taskParams().clusters,
        allNodes,
        taskParams().rootCA,
        taskParams().getClientRootCA(),
        universe.getUniverseDetails().getPrimaryCluster().userIntent.enableClientToNodeEncrypt,
        NodeManager.YUGABYTE_USER);
  }

  private void createUpdateCertsOnXClusterUniversesTask(Universe universe) {
    if (Objects.isNull(taskParams().rootCA)) {
      log.warn("No rootCA provided, but createUpdateCertsOnXClusterUniversesTask was called");
      return;
    }
    CertificateInfo newCertificate = CertificateInfo.getOrBadRequest(taskParams().rootCA);
    String newCertificatePath = newCertificate.getCertificate();
    File newCertificateFile = new File(newCertificatePath);
    if (!newCertificateFile.exists()) {
      throw new IllegalStateException(
          String.format(
              "newCertificateFile file \"%s\" for universe \"%s\" does not exist",
              newCertificateFile, universe.getUniverseUUID()));
    }
    log.debug(
        "Will Transfer new universe root certificate {} to all xCluster universes",
        newCertificatePath);

    List<XClusterConfig> xClusterConfigsAsSource =
        XClusterConfig.getBySourceUniverseUUID(universe.getUniverseUUID());

    // Copy the source cert to the corresponding directory on the target universe.
    Map<UUID, List<XClusterConfig>> targetUniverseUuidToXClusterConfigsMap =
        xClusterConfigsAsSource.stream()
            .collect(Collectors.groupingBy(XClusterConfig::getTargetUniverseUUID));

    // We only gather the db-scoped configs where the task universe is the target universe.
    List<XClusterConfig> xClusterConfigsAsTarget =
        XClusterConfig.getByTargetUniverseUUID(universe.getUniverseUUID()).stream()
            .filter(xClusterConfig -> xClusterConfig.getType().equals(ConfigType.Db))
            .toList();

    // Copy the target cert to the corresponding directory on the source universes.
    Map<UUID, List<XClusterConfig>> sourceUniverseUuidToXClusterConfigsMap =
        xClusterConfigsAsTarget.stream()
            .collect(Collectors.groupingBy(XClusterConfig::getSourceUniverseUUID));

    Map<UUID, List<XClusterConfig>> UniverseUuidToXClusterConfigsMap =
        Stream.concat(
                targetUniverseUuidToXClusterConfigsMap.entrySet().stream(),
                sourceUniverseUuidToXClusterConfigsMap.entrySet().stream())
            .collect(
                Collectors.toMap(
                    Entry::getKey,
                    e -> new ArrayList<>(e.getValue()),
                    (list1, list2) -> {
                      list1.addAll(list2);
                      return list1;
                    }));

    // Put all the universes in the locked list. The unlock operation is a no-op if the universe
    // does not get locked by this task.
    lockedXClusterUniversesUuidSet = UniverseUuidToXClusterConfigsMap.keySet();

    UniverseUuidToXClusterConfigsMap.forEach(
        (universeUuid, xClusterConfigs) -> {
          UUID taskParamsUniverseUuid = taskParams().getUniverseUUID();
          try {
            if (!isCertReloadable(Universe.getOrBadRequest(universeUuid))) {
              log.warn(
                  "Cert reload is not available for universe; skipping cert "
                      + "transfer; The user has to restart the replication manually for configs {}",
                  xClusterConfigs);
              return;
            }

            // Lock the other universe.
            Universe otherUniverse =
                lockUniverseIfExist(universeUuid, -1 /* expectedUniverseVersion */);
            if (otherUniverse == null) {
              log.info("Other universe is deleted; No further action is needed");
              return;
            }

            taskParams().setUniverseUUID(otherUniverse.getUniverseUUID());

            // Create the subtasks to transfer the new source universe root certificates to the
            // target universe if required. It will overwrite the previous certs it they exist.
            xClusterConfigs.forEach(
                xClusterConfig ->
                    createTransferXClusterCertsCopyTasks(
                        otherUniverse.getNodes(),
                        xClusterConfig.getReplicationGroupName(),
                        newCertificateFile,
                        otherUniverse));

            List<NodeDetails> mastersList = otherUniverse.getMasters();
            List<NodeDetails> tserversList = otherUniverse.getTServersInPrimaryCluster();
            MastersAndTservers nodes = new MastersAndTservers(mastersList, tserversList);

            // The following tasks will reload the certificates on the target universe.
            CertReloadTaskCreator taskCreator =
                new CertReloadTaskCreator(
                    otherUniverse.getUniverseUUID(),
                    getUserTaskUUID(),
                    getRunnableTask(),
                    getTaskExecutor(),
                    mastersList);
            createNonRestartUpgradeTaskFlow(taskCreator, nodes, DEFAULT_CONTEXT);

            // We don't need to restart the ybc because the universe certificate itself has not
            // changed.

            log.debug(
                "Subtasks created to transfer the new universe root certificate to "
                    + "the other universes for these xCluster configs: {}",
                xClusterConfigs);
          } catch (Exception e) {
            log.error(
                "{} hit error while creating subtasks for transferring universe TLS "
                    + "certificates : {}",
                getName(),
                e.getMessage());
            throw new RuntimeException(e);
          } finally {
            taskParams().setUniverseUUID(taskParamsUniverseUuid);
          }
        });
  }
}
