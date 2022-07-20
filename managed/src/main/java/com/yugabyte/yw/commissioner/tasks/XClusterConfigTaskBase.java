// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks;

import com.google.api.client.util.Throwables;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.google.common.collect.Sets;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.TaskExecutor;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.TransferXClusterCerts;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.BootstrapProducer;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.CheckBootstrapRequired;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.SetReplicationPaused;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.SetRestoreTime;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.XClusterConfigModifyTables;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.XClusterConfigRename;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.XClusterConfigSetStatus;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.XClusterConfigSetup;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.XClusterConfigSync;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
import com.yugabyte.yw.models.XClusterTableConfig;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.TaskType;
import io.ebean.Ebean;
import java.io.File;
import java.time.Duration;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Collections;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.annotation.Nullable;
import lombok.extern.slf4j.Slf4j;
import org.yb.WireProtocol.AppStatusPB.ErrorCode;
import org.yb.cdc.CdcConsumer;
import org.yb.client.GetMasterClusterConfigResponse;
import org.yb.client.IsBootstrapRequiredResponse;
import org.yb.client.IsSetupUniverseReplicationDoneResponse;
import org.yb.client.YBClient;
import org.yb.master.CatalogEntityInfo;
import play.api.Play;

@Slf4j
public abstract class XClusterConfigTaskBase extends UniverseDefinitionTaskBase {

  public YBClientService ybService;

  private static final int POLL_TIMEOUT_SECONDS = 300;
  private static final String GFLAG_NAME_TO_SUPPORT_MISMATCH_CERTS = "certs_for_cdc_dir";
  private static final String GFLAG_VALUE_TO_SUPPORT_MISMATCH_CERTS =
      "/home/yugabyte/yugabyte-tls-producer/";

  public static final List<XClusterConfig.XClusterConfigStatusType>
      X_CLUSTER_CONFIG_MUST_DELETE_STATUS_LIST =
          ImmutableList.of(
              XClusterConfig.XClusterConfigStatusType.Deleted,
              XClusterConfig.XClusterConfigStatusType.DeletedUniverse);

  private static final Map<XClusterConfigStatusType, List<TaskType>> STATUS_TO_ALLOWED_TASKS =
      new HashMap<>();

  static {
    STATUS_TO_ALLOWED_TASKS.put(
        XClusterConfigStatusType.Init,
        ImmutableList.of(TaskType.CreateXClusterConfig, TaskType.DeleteXClusterConfig));
    STATUS_TO_ALLOWED_TASKS.put(
        XClusterConfigStatusType.Running,
        ImmutableList.of(TaskType.EditXClusterConfig, TaskType.DeleteXClusterConfig));
    STATUS_TO_ALLOWED_TASKS.put(
        XClusterConfigStatusType.Updating, ImmutableList.of(TaskType.DeleteXClusterConfig));
    STATUS_TO_ALLOWED_TASKS.put(
        XClusterConfigStatusType.DeletedUniverse, ImmutableList.of(TaskType.DeleteXClusterConfig));
    STATUS_TO_ALLOWED_TASKS.put(
        XClusterConfigStatusType.Deleted, ImmutableList.of(TaskType.DeleteXClusterConfig));
    STATUS_TO_ALLOWED_TASKS.put(
        XClusterConfigStatusType.Failed,
        ImmutableList.of(TaskType.EditXClusterConfig, TaskType.DeleteXClusterConfig));
  }

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

  protected XClusterConfig getXClusterConfigFromTaskParams() {
    XClusterConfig xClusterConfig = taskParams().xClusterConfig;
    if (xClusterConfig == null) {
      throw new RuntimeException("xClusterConfig in task params is null");
    }
    return xClusterConfig;
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
    taskParams().xClusterConfig.setStatus(status);
  }

  public static boolean isInMustDeleteStatus(XClusterConfig xClusterConfig) {
    if (xClusterConfig == null) {
      throw new RuntimeException("xClusterConfig cannot be null");
    }
    return X_CLUSTER_CONFIG_MUST_DELETE_STATUS_LIST.contains(xClusterConfig.status);
  }

  public static boolean isTaskAllowed(XClusterConfig xClusterConfig, TaskType taskType) {
    if (taskType == null) {
      throw new RuntimeException("taskType cannot be null");
    }
    List<TaskType> allowedTaskTypes = getAllowedTasks(xClusterConfig);
    // Allow unknown situations to avoid bugs for now.
    if (allowedTaskTypes == null) {
      return true;
    }
    return allowedTaskTypes.contains(taskType);
  }

  public static List<TaskType> getAllowedTasks(XClusterConfig xClusterConfig) {
    if (xClusterConfig == null) {
      throw new RuntimeException(
          "Cannot retrieve the list of allowed tasks because xClusterConfig is null");
    }
    List<TaskType> allowedTaskTypes = STATUS_TO_ALLOWED_TASKS.get(xClusterConfig.status);
    if (allowedTaskTypes == null) {
      log.warn(
          "Cannot retrieve the list of allowed tasks because it is not defined for status={}",
          xClusterConfig.status);
    }
    return allowedTaskTypes;
  }

  protected SubTaskGroup createXClusterConfigSetupTask(Set<String> tableIds) {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("XClusterConfigSetup", executor);
    XClusterConfigSetup.Params xClusterConfigParams = new XClusterConfigSetup.Params();
    xClusterConfigParams.universeUUID = taskParams().xClusterConfig.targetUniverseUUID;
    xClusterConfigParams.xClusterConfig = taskParams().xClusterConfig;
    xClusterConfigParams.tableIds = tableIds;

    XClusterConfigSetup task = createTask(XClusterConfigSetup.class);
    task.initialize(xClusterConfigParams);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * It creates a subtask to set the status of an xCluster config and save it in the Platform DB.
   *
   * @param desiredStatus The xCluster config will have this status
   * @return The created subtask group; it can be used to assign a subtask group type to this
   *     subtask
   */
  protected SubTaskGroup createXClusterConfigSetStatusTask(XClusterConfigStatusType desiredStatus) {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("XClusterConfigSetStatus", executor);
    XClusterConfigSetStatus.Params setStatusParams = new XClusterConfigSetStatus.Params();
    setStatusParams.universeUUID = taskParams().xClusterConfig.targetUniverseUUID;
    setStatusParams.xClusterConfig = taskParams().xClusterConfig;
    setStatusParams.desiredStatus = desiredStatus;

    XClusterConfigSetStatus task = createTask(XClusterConfigSetStatus.class);
    task.initialize(setStatusParams);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * It makes an RPC call to pause/enable the xCluster config and saves it in the Platform DB.
   *
   * @param pause Whether to pause replication
   * @return The created subtask group
   */
  protected SubTaskGroup createSetReplicationPausedTask(boolean pause) {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("SetReplicationPaused", executor);
    SetReplicationPaused.Params setReplicationPausedParams = new SetReplicationPaused.Params();
    setReplicationPausedParams.universeUUID = taskParams().xClusterConfig.targetUniverseUUID;
    setReplicationPausedParams.xClusterConfig = taskParams().xClusterConfig;
    setReplicationPausedParams.pause = pause;

    SetReplicationPaused task = createTask(SetReplicationPaused.class);
    task.initialize(setReplicationPausedParams);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  protected SubTaskGroup createSetReplicationPausedTask(String status) {
    return createSetReplicationPausedTask(status.equals("Paused"));
  }

  protected SubTaskGroup createXClusterConfigModifyTablesTask(
      Set<String> tableIdsToAdd, Set<String> tableIdsToRemove) {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("XClusterConfigModifyTables", executor);
    XClusterConfigModifyTables.Params modifyTablesParams = new XClusterConfigModifyTables.Params();
    modifyTablesParams.universeUUID = taskParams().xClusterConfig.targetUniverseUUID;
    modifyTablesParams.xClusterConfig = taskParams().xClusterConfig;
    modifyTablesParams.tableIdsToAdd = tableIdsToAdd;
    modifyTablesParams.tableIdsToRemove = tableIdsToRemove;

    XClusterConfigModifyTables task = createTask(XClusterConfigModifyTables.class);
    task.initialize(modifyTablesParams);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  protected SubTaskGroup createXClusterConfigRenameTask(String newName) {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("XClusterConfigRename", executor);
    XClusterConfigRename.Params xClusterConfigRenameParams = new XClusterConfigRename.Params();
    xClusterConfigRenameParams.universeUUID = taskParams().xClusterConfig.targetUniverseUUID;
    xClusterConfigRenameParams.xClusterConfig = taskParams().xClusterConfig;
    xClusterConfigRenameParams.newName = newName;

    XClusterConfigRename task = createTask(XClusterConfigRename.class);
    task.initialize(xClusterConfigRenameParams);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  protected SubTaskGroup createDeleteBootstrapIdsTask(boolean forceDelete) {
    return createDeleteBootstrapIdsTask(taskParams().xClusterConfig, forceDelete);
  }

  protected SubTaskGroup createDeleteBootstrapIdsTask() {
    return createDeleteBootstrapIdsTask(false /* forceDelete */);
  }

  protected SubTaskGroup createDeleteReplicationTask(boolean ignoreErrors) {
    return createDeleteReplicationTask(taskParams().xClusterConfig, ignoreErrors);
  }

  protected SubTaskGroup createDeleteReplicationTask() {
    return createDeleteReplicationTask(false /* ignoreErrors */);
  }

  protected SubTaskGroup createDeleteXClusterConfigEntryTask(boolean forceDelete) {
    return createDeleteXClusterConfigEntryTask(taskParams().xClusterConfig, forceDelete);
  }

  protected SubTaskGroup createDeleteXClusterConfigEntryTask() {
    return createDeleteXClusterConfigEntryTask(false /* forceDelete */);
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

  /**
   * It parses a replication group name in the coreDB, and return the source universe UUID and the
   * xCluster config name shown in the Platform UI. The input must have a format of
   * sourceUniverseUuid_xClusterConfigName (e.g., dc510c24-9c4a-4e15-b59b-333e5815e936_MyRepl).
   *
   * @param replicationGroupName The replication group name saved in the coreDB.
   * @return returns an optional of a Pair of source universe UUID and the xCluster config name if
   *     the input has the correct format. Otherwise, the optional is empty.
   */
  public static Optional<Pair<UUID, String>> maybeParseReplicationGroupName(
      String replicationGroupName) {
    String[] sourceUniverseUuidAndConfigName = replicationGroupName.split("_", 2);
    if (sourceUniverseUuidAndConfigName.length != 2) {
      log.warn(
          "Unable to parse XClusterConfig: {}, expected format <sourceUniverseUUID>_<name>",
          replicationGroupName);
      return Optional.empty();
    }
    UUID sourceUniverseUUID;
    try {
      sourceUniverseUUID = UUID.fromString(sourceUniverseUuidAndConfigName[0]);
    } catch (Exception e) {
      log.warn(
          "Unable to parse {} as valid UUID for replication group name: {}",
          sourceUniverseUuidAndConfigName[0],
          replicationGroupName);
      return Optional.empty();
    }
    return Optional.of(new Pair<>(sourceUniverseUUID, sourceUniverseUuidAndConfigName[1]));
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
      transferParams.replicationGroupName = configName;
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
    createTransferXClusterCertsCopyTasks(
        nodes, configName, certificate, null /* producerCertsDir */);
  }

  protected SubTaskGroup createTransferXClusterCertsRemoveTasks(File producerCertsDir) {
    return createTransferXClusterCertsRemoveTasks(taskParams().xClusterConfig, producerCertsDir);
  }

  protected SubTaskGroup createTransferXClusterCertsRemoveTasks() {
    return createTransferXClusterCertsRemoveTasks(null /* producerCertsDir */);
  }

  protected void createDeleteXClusterConfigSubtasks() {
    createDeleteXClusterConfigSubtasks(getXClusterConfigFromTaskParams());
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
          userIntent.masterGFlags.get(GFLAG_NAME_TO_SUPPORT_MISMATCH_CERTS));
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
          userIntent.tserverGFlags.get(GFLAG_NAME_TO_SUPPORT_MISMATCH_CERTS));
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

  /**
   * It creates a group of subtasks to check if bootstrap is required for all the tables in the
   * xCluster config of this task.
   *
   * <p>It creates separate subtasks for each table to increase parallelism because current coreDB
   * implementation can check one table in one RPC call.
   *
   * @param tableIds A set of table IDs to check whether they require bootstrap
   * @return The created subtask group
   */
  protected SubTaskGroup createCheckBootstrapRequiredTask(Set<String> tableIds) {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("CheckBootstrapRequired", executor);
    for (String tableId : tableIds) {
      CheckBootstrapRequired.Params bootstrapRequiredParams = new CheckBootstrapRequired.Params();
      bootstrapRequiredParams.universeUUID = taskParams().xClusterConfig.sourceUniverseUUID;
      bootstrapRequiredParams.xClusterConfig = taskParams().xClusterConfig;
      bootstrapRequiredParams.tableIds = Collections.singleton(tableId);

      CheckBootstrapRequired task = createTask(CheckBootstrapRequired.class);
      task.initialize(bootstrapRequiredParams);
      subTaskGroup.addSubTask(task);
    }
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  protected void checkBootstrapRequired(Set<String> tableIds) {
    XClusterConfig xClusterConfig = getXClusterConfigFromTaskParams();
    log.info(
        "Running checkBootstrapRequired with (sourceUniverse={},xClusterUuid={},tableIds={})",
        xClusterConfig.sourceUniverseUUID,
        xClusterConfig,
        tableIds);

    Universe targetUniverse = Universe.getOrBadRequest(xClusterConfig.targetUniverseUUID);
    String targetUniverseMasterAddresses = targetUniverse.getMasterAddresses();
    String targetUniverseCertificate = targetUniverse.getCertificateNodetoNode();
    try (YBClient client =
        ybService.getClient(targetUniverseMasterAddresses, targetUniverseCertificate)) {
      // If xCluster config state is not Init, sync the stream ids to make sure the right one is
      // passed to the IsBootstrapRequired API.
      if (xClusterConfig.status != XClusterConfig.XClusterConfigStatusType.Init) {
        GetMasterClusterConfigResponse clusterConfigResp = client.getMasterClusterConfig();
        if (clusterConfigResp.hasError()) {
          throw new RuntimeException(
              String.format(
                  "Failed to getMasterClusterConfig from target universe (%s) for xCluster "
                      + "config %s: %s",
                  targetUniverse.universeUUID, xClusterConfig, clusterConfigResp.errorMessage()));
        }
        updateStreamIdsFromTargetUniverseClusterConfig(
            clusterConfigResp.getConfig(), xClusterConfig, tableIds);
      }

      // Check whether bootstrap is required.
      Map<String, Boolean> isBootstrapRequiredMap = isBootstrapRequired(tableIds, xClusterConfig);
      log.debug("IsBootstrapRequired result is {}", isBootstrapRequiredMap);

      // Persist whether bootstrap is required.
      Map<Boolean, List<String>> tableIdsPartitionedByNeedBootstrap =
          isBootstrapRequiredMap
              .keySet()
              .stream()
              .collect(Collectors.partitioningBy(isBootstrapRequiredMap::get));
      xClusterConfig.setNeedBootstrapForTables(
          tableIdsPartitionedByNeedBootstrap.get(true), true /* needBootstrap */);
      xClusterConfig.setNeedBootstrapForTables(
          tableIdsPartitionedByNeedBootstrap.get(false), false /* needBootstrap */);
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    }

    log.info("Completed checkBootstrapRequired");
  }

  /**
   * It returns a set of tables that xCluster replication cannot be setup for them and need
   * bootstrap.
   *
   * <p>Note: You must run {@link #createCheckBootstrapRequiredTask} method before running this
   * method to have the most updated values.
   *
   * @return A set of tables that need to be bootstrapped
   * @see #createCheckBootstrapRequiredTask(Set)
   */
  protected Set<XClusterTableConfig> getTablesNeedBootstrap() {
    return taskParams()
        .xClusterConfig
        .tables
        .stream()
        .filter(tableConfig -> tableConfig.needBootstrap)
        .collect(Collectors.toSet());
  }

  /**
   * It returns a set of tables that xCluster replication can be set up without bootstrapping.
   *
   * @return A set of tables that need to be bootstrapped
   * @see #getTablesNeedBootstrap()
   */
  protected Set<XClusterTableConfig> getTablesNotNeedBootstrap() {
    return taskParams()
        .xClusterConfig
        .tables
        .stream()
        .filter(tableConfig -> !tableConfig.needBootstrap)
        .collect(Collectors.toSet());
  }

  /**
   * It creates a subtask to bootstrap the set of tables passed in.
   *
   * @param tableIds The ids of the tables to be bootstrapped
   */
  protected SubTaskGroup createBootstrapProducerTask(Collection<String> tableIds) {
    SubTaskGroup subTaskGroup = getTaskExecutor().createSubTaskGroup("BootstrapProducer", executor);
    BootstrapProducer.Params bootstrapProducerParams = new BootstrapProducer.Params();
    bootstrapProducerParams.universeUUID = taskParams().xClusterConfig.sourceUniverseUUID;
    bootstrapProducerParams.xClusterConfig = taskParams().xClusterConfig;
    bootstrapProducerParams.tableIds = new ArrayList<>(tableIds);

    BootstrapProducer task = createTask(BootstrapProducer.class);
    task.initialize(bootstrapProducerParams);
    subTaskGroup.addSubTask(task);
    subTaskGroup.setSubTaskGroupType(SubTaskGroupType.BootstrappingProducer);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * It creates a subtask to set the restore time in the Platform DB for tables that underwent
   * bootstrapping. This subtask must be added to the run queue after the restore subtask.
   *
   * @param tableIds Table ids that underwent bootstrapping and a restore to the target universe
   *     happened for them
   */
  protected SubTaskGroup createSetRestoreTimeTask(Set<String> tableIds) {
    TaskExecutor.SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("SetBootstrapBackup", executor);
    SetRestoreTime.Params setRestoreTimeParams = new SetRestoreTime.Params();
    setRestoreTimeParams.universeUUID = taskParams().xClusterConfig.sourceUniverseUUID;
    setRestoreTimeParams.xClusterConfig = taskParams().xClusterConfig;
    setRestoreTimeParams.tableIds = tableIds;

    SetRestoreTime task = createTask(SetRestoreTime.class);
    task.initialize(setRestoreTimeParams);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  protected CatalogEntityInfo.SysClusterConfigEntryPB getClusterConfig(
      YBClient client, UUID universeUuid) throws Exception {
    GetMasterClusterConfigResponse clusterConfigResp = client.getMasterClusterConfig();
    if (clusterConfigResp.hasError()) {
      throw new RuntimeException(
          String.format(
              "Failed to getMasterClusterConfig from target universe (%s): %s",
              universeUuid, clusterConfigResp.errorMessage()));
    }
    return clusterConfigResp.getConfig();
  }

  protected void updateStreamIdsFromTargetUniverseClusterConfig(
      CatalogEntityInfo.SysClusterConfigEntryPB config,
      XClusterConfig xClusterConfig,
      Set<String> tableIds) {
    CdcConsumer.ProducerEntryPB replicationGroup =
        config
            .getConsumerRegistry()
            .getProducerMapMap()
            .get(xClusterConfig.getReplicationGroupName());
    if (replicationGroup == null) {
      throw new RuntimeException(
          String.format(
              "No replication group found with name (%s) in universe (%s) cluster config",
              xClusterConfig.getReplicationGroupName(), xClusterConfig.targetUniverseUUID));
    }

    // Ensure disable stream state is in sync with Platform's point of view.
    if (replicationGroup.getDisableStream() != xClusterConfig.paused) {
      throw new RuntimeException(
          String.format(
              "Detected mismatched disable state for replication group %s and xCluster config %s",
              replicationGroup, xClusterConfig));
    }

    // Parse stream map and convert to a map from source table id to stream id.
    Map<String, CdcConsumer.StreamEntryPB> replicationStreams = replicationGroup.getStreamMapMap();
    Map<String, String> streamMap =
        replicationStreams
            .entrySet()
            .stream()
            .collect(Collectors.toMap(e -> e.getValue().getProducerTableId(), Map.Entry::getKey));

    // Ensure Platform's table set is in sync with cluster config stored in the target universe.
    Set<String> tableIdsWithReplication = xClusterConfig.getTableIdsWithReplicationSetup();
    if (!streamMap.keySet().equals(tableIdsWithReplication)) {
      Set<String> platformMissing = Sets.difference(streamMap.keySet(), tableIdsWithReplication);
      Set<String> clusterConfigMissing =
          Sets.difference(tableIdsWithReplication, streamMap.keySet());
      throw new RuntimeException(
          String.format(
              "Detected mismatch table set in Platform and target universe (%s) cluster "
                  + "config for xCluster config (%s): (missing tables on Platform=%s, "
                  + "missing tables in cluster config=%s).",
              xClusterConfig.targetUniverseUUID,
              xClusterConfig.uuid,
              platformMissing,
              clusterConfigMissing));
    }

    // Persist streamIds.
    for (String tableId : tableIds) {
      Optional<XClusterTableConfig> tableConfig = xClusterConfig.maybeGetTableById(tableId);
      String streamId = streamMap.get(tableId);
      if (!tableConfig.isPresent()) {
        throw new RuntimeException(
            String.format(
                "Could not find tableId (%s) in the xCluster config %s", tableId, xClusterConfig));
      }
      if (tableConfig.get().streamId == null) {
        tableConfig.get().streamId = streamId;
        tableConfig.get().save();
        log.info(
            "StreamId for table {} in xCluster config {} is set to {}",
            tableId,
            xClusterConfig.uuid,
            streamId);
      } else {
        if (!tableConfig.get().streamId.equals(streamId)) {
          throw new RuntimeException(
              String.format(
                  "Bootstrap id (%s) for table (%s) is different from stream id (%s) in the "
                      + "cluster config for xCluster config (%s)",
                  tableConfig.get().streamId, tableId, streamId, xClusterConfig.uuid));
        }
      }
    }
  }

  /**
   * It creates the required parameters to make IsBootstrapRequired API call and then makes the
   * call.
   *
   * <p>Currently, IsBootstrapRequired supports one table at a time.
   *
   * @param ybService The YBClientService object to get a yb client from
   * @param tableIds The table IDs of tables to check whether they need bootstrap
   * @param xClusterConfig The config to check if an existing stream has fallen far behind
   * @param sourceUniverseUuid The UUID of the universe that {@code tableIds} belong to
   * @return A map of tableId to a boolean showing whether that table needs bootstrapping
   */
  private static Map<String, Boolean> isBootstrapRequired(
      YBClientService ybService,
      Set<String> tableIds,
      @Nullable XClusterConfig xClusterConfig,
      UUID sourceUniverseUuid)
      throws Exception {
    log.debug(
        "XClusterConfigTaskBase.isBootstrapRequired is called with xClusterConfig={}, "
            + "tableIds={}, and universeUuid={}",
        xClusterConfig,
        tableIds,
        sourceUniverseUuid);

    // If there is no table to check return an empty map.
    if (tableIds.isEmpty()) {
      return new HashMap<>();
    }

    // Create tableIdStreamId map to pass to the IsBootstrapRequired API.
    Map<String, String> tableIdStreamIdMap;
    if (xClusterConfig != null) {
      tableIdStreamIdMap = xClusterConfig.getTableIdStreamIdMap(tableIds);
    } else {
      tableIdStreamIdMap = new HashMap<>();
      tableIds.forEach(tableId -> tableIdStreamIdMap.put(tableId, null));
    }

    Universe sourceUniverse = Universe.getOrBadRequest(sourceUniverseUuid);
    String sourceUniverseMasterAddresses = sourceUniverse.getMasterAddresses();
    String sourceUniverseCertificate = sourceUniverse.getCertificateNodetoNode();
    try (YBClient client =
        ybService.getClient(sourceUniverseMasterAddresses, sourceUniverseCertificate)) {
      Map<String, Boolean> isBootstrapRequiredMap = new HashMap<>();
      // Currently, only one table can be passed to the IsBootstrapRequired API.
      for (Map.Entry<String, String> entry : tableIdStreamIdMap.entrySet()) {
        Map<String, String> tableIdStreamIdMapSingleEntry =
            Collections.singletonMap(entry.getKey(), entry.getValue());
        // Check whether bootstrap is required.
        IsBootstrapRequiredResponse resp =
            client.isBootstrapRequired(tableIdStreamIdMapSingleEntry);
        if (resp.hasError()) {
          throw new RuntimeException(
              String.format(
                  "IsBootstrapRequired RPC call with %s has errors in xCluster config %s: %s",
                  xClusterConfig, tableIdStreamIdMapSingleEntry, resp.errorMessage()));
        }
        Map<String, Boolean> isBootstrapRequiredResult = resp.getResults();
        log.debug(
            "IsBootstrapRequired RPC call with {} returned {}",
            tableIdStreamIdMapSingleEntry,
            isBootstrapRequiredResult);
        // Parse the result.
        Iterator<Map.Entry<String, Boolean>> it = isBootstrapRequiredResult.entrySet().iterator();
        if (!it.hasNext()) {
          throw new IllegalStateException(
              "Called IsBootstrapRequired for one table but the result is empty");
        }
        Map.Entry<String, Boolean> resultEntry = it.next();
        String tableId = resultEntry.getKey();
        Boolean needBootstrap = resultEntry.getValue();
        if (it.hasNext()) {
          throw new IllegalStateException(
              String.format(
                  "Called IsBootstrapRequired for one table but results for %d tables are "
                      + "returned",
                  isBootstrapRequiredResult.size()));
        }
        if (!tableId.equals(entry.getKey())) {
          throw new IllegalStateException(
              String.format(
                  "Called IsBootstrapRequired for tableId %s but the result is for tableId %s",
                  entry.getKey(), tableId));
        }
        if (isBootstrapRequiredMap.containsKey(tableId)) {
          throw new IllegalStateException(
              String.format("The result for tableId %s is already received", tableId));
        }
        isBootstrapRequiredMap.put(tableId, needBootstrap);
      }
      return isBootstrapRequiredMap;
    }
  }

  /**
   * It checks whether the replication of tables can be started without bootstrap.
   *
   * @see #isBootstrapRequired(YBClientService, Set, XClusterConfig, UUID)
   */
  public static Map<String, Boolean> isBootstrapRequired(
      YBClientService ybService, Set<String> tableIds, UUID sourceUniverseUuid) throws Exception {
    return isBootstrapRequired(ybService, tableIds, null /* xClusterConfig */, sourceUniverseUuid);
  }

  /**
   * It checks whether the replication of tables in an existing xCluster config has fallen behind.
   *
   * @see #isBootstrapRequired(YBClientService, Set, XClusterConfig, UUID)
   */
  public static Map<String, Boolean> isBootstrapRequired(
      YBClientService ybService, Set<String> tableIds, XClusterConfig xClusterConfig)
      throws Exception {
    return isBootstrapRequired(
        ybService, tableIds, xClusterConfig, xClusterConfig.sourceUniverseUUID);
  }

  protected Map<String, Boolean> isBootstrapRequired(
      Set<String> tableIds, XClusterConfig xClusterConfig) throws Exception {
    return isBootstrapRequired(this.ybService, tableIds, xClusterConfig);
  }
}
