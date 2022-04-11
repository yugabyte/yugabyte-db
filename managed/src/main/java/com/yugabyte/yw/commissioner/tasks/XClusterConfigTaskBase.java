// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks;

import com.google.api.client.util.Throwables;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.TransferXClusterCerts;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.XClusterConfigDelete;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.XClusterConfigModifyTables;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.XClusterConfigRename;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.XClusterConfigSetStatus;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.XClusterConfigSetup;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.XClusterConfigSync;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
import com.yugabyte.yw.models.helpers.NodeDetails;
import io.ebean.Ebean;
import java.io.File;
import java.time.Duration;
import java.util.Collection;
import java.util.Collections;
import java.util.Map;
import java.util.Optional;
import lombok.extern.slf4j.Slf4j;
import org.yb.WireProtocol.AppStatusPB.ErrorCode;
import org.yb.client.IsSetupUniverseReplicationDoneResponse;
import play.api.Play;

@Slf4j
public abstract class XClusterConfigTaskBase extends UniverseDefinitionTaskBase {

  public YBClientService ybService;

  private static final int POLL_TIMEOUT_SECONDS = 300;
  private static final String GFLAG_NAME_TO_SUPPORT_MISMATCH_CERTS = "certs_for_cdc_dir";
  private static final String GFLAG_VALUE_TO_SUPPORT_MISMATCH_CERTS =
      "/home/yugabyte/yugabyte-tls-producer/";

  protected XClusterConfigTaskBase(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  public void initialize(ITaskParams params) {
    super.initialize(params);
    ybService = Play.current().injector().instanceOf(YBClientService.class);
  }

  @Override
  public String getName() {
    if (taskParams().xClusterConfig != null) {
      return String.format(
          "%s(uuid=%s, universe=%s)",
          this.getClass().getSimpleName(),
          taskParams().xClusterConfig.uuid,
          taskParams().universeUUID);
    } else {
      return String.format(
          "%s(universe=%s)", this.getClass().getSimpleName(), taskParams().universeUUID);
    }
  }

  @Override
  protected XClusterConfigTaskParams taskParams() {
    return (XClusterConfigTaskParams) taskParams;
  }

  protected XClusterConfig getXClusterConfig() {
    taskParams().xClusterConfig = XClusterConfig.getOrBadRequest(taskParams().xClusterConfig.uuid);
    return taskParams().xClusterConfig;
  }

  protected Optional<XClusterConfig> maybeGetXClusterConfig() {
    return XClusterConfig.maybeGet(taskParams().xClusterConfig.uuid);
  }

  protected XClusterConfig refreshXClusterConfig() {
    taskParams().xClusterConfig.refresh();
    Ebean.refreshMany(taskParams().xClusterConfig, "tables");
    return taskParams().xClusterConfig;
  }

  protected void setXClusterConfigStatus(XClusterConfigStatusType status) {
    taskParams().xClusterConfig.status = status;
    taskParams().xClusterConfig.update();
  }

  protected SubTaskGroup createXClusterConfigSetupTask() {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("XClusterConfigSetup", executor);
    XClusterConfigSetup task = createTask(XClusterConfigSetup.class);
    task.initialize(taskParams());
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  protected SubTaskGroup createXClusterConfigToggleStatusTask() {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("XClusterConfigToggleStatus", executor);
    XClusterConfigSetStatus task = createTask(XClusterConfigSetStatus.class);
    task.initialize(taskParams());
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  protected SubTaskGroup createXClusterConfigModifyTablesTask() {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("XClusterConfigModifyTables", executor);
    XClusterConfigModifyTables task = createTask(XClusterConfigModifyTables.class);
    task.initialize(taskParams());
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  protected SubTaskGroup createXClusterConfigRenameTask() {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("XClusterConfigRename", executor);
    XClusterConfigRename task = createTask(XClusterConfigRename.class);
    task.initialize(taskParams());
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  protected SubTaskGroup createXClusterConfigDeleteTask() {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("XClusterConfigDelete", executor);
    XClusterConfigDelete task = createTask(XClusterConfigDelete.class);
    task.initialize(taskParams());
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  protected SubTaskGroup createXClusterConfigSyncTask() {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("XClusterConfigSync", executor);
    XClusterConfigSync task = createTask(XClusterConfigSync.class);
    task.initialize(taskParams());
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  protected interface IPollForXClusterOperation {
    IsSetupUniverseReplicationDoneResponse poll(String replicationGroupName) throws Exception;
  }

  protected void waitForXClusterOperation(IPollForXClusterOperation p) {
    XClusterConfig xClusterConfig = taskParams().xClusterConfig;

    try {
      IsSetupUniverseReplicationDoneResponse doneResponse = null;
      int numAttempts = 1;
      long startTime = System.currentTimeMillis();
      while (((System.currentTimeMillis() - startTime) / 1000) < POLL_TIMEOUT_SECONDS) {
        if (numAttempts % 10 == 0) {
          log.info(
              "Wait for XClusterConfig({}) operation to complete (attempt {})",
              xClusterConfig.uuid,
              numAttempts);
        }

        doneResponse = p.poll(xClusterConfig.getReplicationGroupName());
        if (doneResponse.isDone()) {
          break;
        }
        if (doneResponse.hasError()) {
          log.warn(
              "Failed to wait for XClusterConfig({}) operation: {}",
              xClusterConfig.uuid,
              doneResponse.getError().toString());
        }
        waitFor(Duration.ofMillis(1000));
        numAttempts++;
      }

      if (doneResponse == null) {
        // TODO: Add unit tests (?) -- May be difficult due to wait
        throw new RuntimeException(
            String.format(
                "Never received response waiting for XClusterConfig(%s) operation to complete",
                xClusterConfig.uuid));
      }
      if (!doneResponse.isDone()) {
        // TODO: Add unit tests (?) -- May be difficult due to wait
        throw new RuntimeException(
            String.format(
                "Timed out waiting for XClusterConfig(%s) operation to complete",
                xClusterConfig.uuid));
      }
      if (doneResponse.hasReplicationError()
          && doneResponse.getReplicationError().getCode() != ErrorCode.OK) {
        throw new RuntimeException(
            String.format(
                "XClusterConfig(%s) operation failed: %s",
                xClusterConfig.uuid, doneResponse.getReplicationError().toString()));
      }

    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      Throwables.propagate(e);
    }
  }

  /**
   * It checks if it is necessary to copy the source universe root certificate to the target
   * universe for the xCluster replication config to work. If it is necessary, an optional
   * containing the path to the source root certificate on the Platform host will be returned.
   * Otherwise, it will be empty.
   *
   * @param sourceUniverse The source Universe in the xCluster replication config.
   * @param targetUniverse The target Universe in the xCluster replication config.
   * @return returns an optional File that is present if transferring the source root certificate is
   *     necessary.
   * @throws IllegalArgumentException if setting up a replication config between a universe with
   *     node-to-node TLS and one without. It is not supported by coreDB.
   */
  protected Optional<File> getSourceCertificateIfNecessary(
      Universe sourceUniverse, Universe targetUniverse) {
    String sourceCertificatePath = sourceUniverse.getCertificateNodetoNode();
    String targetCertificatePath = targetUniverse.getCertificateNodetoNode();

    // Either both universes must have node-to-node encryption or none has.
    if (sourceCertificatePath != null && targetCertificatePath != null) {
      if (sourceCertificatePath.equals(targetCertificatePath)) {
        // If the "certs_for_cdc_dir" gflag is set on the target universe, the certificate must
        // be transferred even though the universes are using the same certs.
        UniverseDefinitionTaskParams.UserIntent userIntent =
            targetUniverse.getUniverseDetails().getPrimaryCluster().userIntent;
        if (userIntent.masterGFlags.containsKey(GFLAG_NAME_TO_SUPPORT_MISMATCH_CERTS)
            || userIntent.tserverGFlags.containsKey(GFLAG_NAME_TO_SUPPORT_MISMATCH_CERTS)) {
          return Optional.of(new File(sourceCertificatePath));
        }
        return Optional.empty();
      }
      return Optional.of(new File(sourceCertificatePath));
    } else if (sourceCertificatePath == null && targetCertificatePath == null) {
      return Optional.empty();
    }
    throw new IllegalArgumentException(
        "A replication config cannot be set between a universe with node-to-node encryption and"
            + " a universe without.");
  }

  protected void createTransferXClusterCertsCopyTasks(
      Collection<NodeDetails> nodes, String configName, File certificate, File producerCertsDir) {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("TransferXClusterCerts", executor);
    for (NodeDetails node : nodes) {
      TransferXClusterCerts.Params transferParams = new TransferXClusterCerts.Params();
      transferParams.universeUUID = taskParams().universeUUID;
      transferParams.nodeName = node.nodeName;
      transferParams.azUuid = node.azUuid;
      transferParams.rootCertPath = certificate;
      transferParams.action = TransferXClusterCerts.Params.Action.COPY;
      transferParams.replicationConfigName = configName;
      if (producerCertsDir != null) {
        transferParams.producerCertsDirOnTarget = producerCertsDir;
      }

      TransferXClusterCerts transferXClusterCertsTask = createTask(TransferXClusterCerts.class);
      transferXClusterCertsTask.initialize(transferParams);
      subTaskGroup.addSubTask(transferXClusterCertsTask);
    }
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    subTaskGroup.setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
  }

  protected void createTransferXClusterCertsCopyTasks(
      Collection<NodeDetails> nodes, String configName, File certificate) {
    createTransferXClusterCertsCopyTasks(nodes, configName, certificate, null);
  }

  protected void createTransferXClusterCertsRemoveTasks(
      Collection<NodeDetails> nodes, String configName, File producerCertsDir) {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("TransferXClusterCerts", executor);
    for (NodeDetails node : nodes) {
      TransferXClusterCerts.Params transferParams = new TransferXClusterCerts.Params();
      transferParams.universeUUID = taskParams().universeUUID;
      transferParams.nodeName = node.nodeName;
      transferParams.azUuid = node.azUuid;
      transferParams.action = TransferXClusterCerts.Params.Action.REMOVE;
      transferParams.replicationConfigName = configName;
      if (producerCertsDir != null) {
        transferParams.producerCertsDirOnTarget = producerCertsDir;
      }

      TransferXClusterCerts transferXClusterCertsTask = createTask(TransferXClusterCerts.class);
      transferXClusterCertsTask.initialize(transferParams);
      subTaskGroup.addSubTask(transferXClusterCertsTask);
    }
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    subTaskGroup.setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
  }

  protected void createTransferXClusterCertsRemoveTasks(
      Collection<NodeDetails> nodes, String configName) {
    createTransferXClusterCertsRemoveTasks(nodes, configName, null);
  }

  protected void upgradeMismatchedXClusterCertsGFlags(
      Collection<NodeDetails> nodes, UniverseDefinitionTaskBase.ServerType serverType) {
    createGFlagsOverrideTasks(nodes, serverType);
    createSetFlagInMemoryTasks(
            nodes,
            serverType,
            true /* force flag update */,
            ImmutableMap.of(
                GFLAG_NAME_TO_SUPPORT_MISMATCH_CERTS, GFLAG_VALUE_TO_SUPPORT_MISMATCH_CERTS),
            false /* updateMasterAddrs */)
        .setSubTaskGroupType(SubTaskGroupType.UpdatingGFlags);
  }

  /**
   * It checks if userIntent does not have the correct value for certs_for_cdc_dir GFlag and will
   * set those values.
   *
   * @param userIntent It contains the GFlags with the intended value to be updated.
   * @param serverType It can be either Master or TServer.
   * @return returns true if the GFlag was updated.
   * @throws IllegalStateException if transferring the GFlag has a value, and it is different from
   *     what we support.
   */
  protected boolean verifyAndSetCertsForCdcDirGFlag(
      UniverseDefinitionTaskParams.UserIntent userIntent,
      UniverseDefinitionTaskBase.ServerType serverType) {
    Map<String, String> gFlags =
        serverType == UniverseDefinitionTaskBase.ServerType.MASTER
            ? userIntent.masterGFlags
            : userIntent.tserverGFlags;
    String gFlagValue = gFlags.get(GFLAG_NAME_TO_SUPPORT_MISMATCH_CERTS);
    if (gFlagValue != null) {
      if (!gFlagValue.equals(GFLAG_VALUE_TO_SUPPORT_MISMATCH_CERTS)) {
        throw new IllegalStateException(
            String.format(
                "%s is present with value %s", GFLAG_NAME_TO_SUPPORT_MISMATCH_CERTS, gFlagValue));
      }
      return false;
    }
    gFlags.put(GFLAG_NAME_TO_SUPPORT_MISMATCH_CERTS, GFLAG_VALUE_TO_SUPPORT_MISMATCH_CERTS);
    return true;
  }

  protected void createSetupSourceCertificateTask(
      Universe targetUniverse, String configName, File certificate, File producerCertsDir) {
    // If the certificate does not exist, fail early.
    if (!certificate.exists()) {
      throw new IllegalArgumentException(String.format("file \"%s\" does not exist", certificate));
    }

    // Set the necessary gflags to support mismatch certificates for xCluster.
    UniverseDefinitionTaskParams.UserIntent userIntent =
        targetUniverse.getUniverseDetails().getPrimaryCluster().userIntent;
    // The gflags must be set only on the nodes in the primary cluster of the target universe.
    taskParams().clusters =
        Collections.singletonList(targetUniverse.getUniverseDetails().getPrimaryCluster());
    boolean gFlagsUpdated = false;
    if (verifyAndSetCertsForCdcDirGFlag(userIntent, UniverseDefinitionTaskBase.ServerType.MASTER)) {
      upgradeMismatchedXClusterCertsGFlags(
          targetUniverse.getMasters(), UniverseDefinitionTaskBase.ServerType.MASTER);
      log.debug(
          "gflag {} set to {} for masters",
          GFLAG_NAME_TO_SUPPORT_MISMATCH_CERTS,
          GFLAG_VALUE_TO_SUPPORT_MISMATCH_CERTS);
      gFlagsUpdated = true;
    }
    if (verifyAndSetCertsForCdcDirGFlag(
        userIntent, UniverseDefinitionTaskBase.ServerType.TSERVER)) {
      upgradeMismatchedXClusterCertsGFlags(
          targetUniverse.getTServersInPrimaryCluster(),
          UniverseDefinitionTaskBase.ServerType.TSERVER);
      log.debug(
          "gflag {} set to {} for tservers",
          GFLAG_NAME_TO_SUPPORT_MISMATCH_CERTS,
          GFLAG_VALUE_TO_SUPPORT_MISMATCH_CERTS);
      gFlagsUpdated = true;
    }
    if (gFlagsUpdated) {
      updateGFlagsPersistTasks(userIntent.masterGFlags, userIntent.tserverGFlags)
          .setSubTaskGroupType(SubTaskGroupType.UpdatingGFlags);
    }

    // Transfer the source universe root certificate to the target universe.
    createTransferXClusterCertsCopyTasks(
        targetUniverse.getNodes(), configName, certificate, producerCertsDir);
    log.debug(
        "Transferring certificate {} to universe {} task created",
        certificate,
        targetUniverse.getUniverseUUID().toString());
  }

  protected void createSetupSourceCertificateTask(
      Universe targetUniverse, String configName, File certificate) {
    createSetupSourceCertificateTask(targetUniverse, configName, certificate, null);
  }
}
