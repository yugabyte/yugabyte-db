// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks;

import com.google.api.client.util.Throwables;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.Sets;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.TaskExecutor;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
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
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
import com.yugabyte.yw.models.XClusterTableConfig;
import com.yugabyte.yw.models.helpers.TaskType;
import io.ebean.Ebean;
import java.io.File;
import java.time.Duration;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Objects;
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
import org.yb.master.MasterDdlOuterClass;
import play.api.Play;

@Slf4j
public abstract class XClusterConfigTaskBase extends UniverseDefinitionTaskBase {

  public YBClientService ybService;

  private static final int POLL_TIMEOUT_SECONDS = 300;
  public static final String SOURCE_ROOT_CERTS_DIR_GFLAG = "certs_for_cdc_dir";
  public static final String DEFAULT_SOURCE_ROOT_CERTS_DIR_NAME = "/yugabyte-tls-producer";

  public static final List<XClusterConfig.XClusterConfigStatusType>
      X_CLUSTER_CONFIG_MUST_DELETE_STATUS_LIST =
          ImmutableList.of(
              XClusterConfig.XClusterConfigStatusType.DeletionFailed,
              XClusterConfig.XClusterConfigStatusType.DeletedUniverse);

  private static final Map<XClusterConfigStatusType, List<TaskType>> STATUS_TO_ALLOWED_TASKS =
      new HashMap<>();

  static {
    STATUS_TO_ALLOWED_TASKS.put(
        XClusterConfigStatusType.Initialized,
        ImmutableList.of(TaskType.CreateXClusterConfig, TaskType.DeleteXClusterConfig));
    STATUS_TO_ALLOWED_TASKS.put(
        XClusterConfigStatusType.Running,
        ImmutableList.of(
            TaskType.EditXClusterConfig,
            TaskType.DeleteXClusterConfig,
            TaskType.RestartXClusterConfig));
    STATUS_TO_ALLOWED_TASKS.put(
        XClusterConfigStatusType.Updating, ImmutableList.of(TaskType.DeleteXClusterConfig));
    STATUS_TO_ALLOWED_TASKS.put(
        XClusterConfigStatusType.DeletedUniverse, ImmutableList.of(TaskType.DeleteXClusterConfig));
    STATUS_TO_ALLOWED_TASKS.put(
        XClusterConfigStatusType.DeletionFailed, ImmutableList.of(TaskType.DeleteXClusterConfig));
    STATUS_TO_ALLOWED_TASKS.put(
        XClusterConfigStatusType.Failed,
        ImmutableList.of(
            TaskType.EditXClusterConfig,
            TaskType.DeleteXClusterConfig,
            TaskType.RestartXClusterConfig));
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

  public static String getProducerCertsDir(UUID providerUuid) {
    return Provider.getOrBadRequest(providerUuid).getYbHome() + DEFAULT_SOURCE_ROOT_CERTS_DIR_NAME;
  }

  public static String getProducerCertsDir(String providerUuid) {
    return getProducerCertsDir(UUID.fromString(providerUuid));
  }

  public String getProducerCertsDir() {
    return getProducerCertsDir(
        Universe.getOrBadRequest(taskParams().xClusterConfig.targetUniverseUUID)
            .getUniverseDetails()
            .getPrimaryCluster()
            .userIntent
            .provider);
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

  protected SubTaskGroup createDeleteXClusterConfigEntryTask() {
    return createDeleteXClusterConfigEntryTask(getXClusterConfigFromTaskParams());
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
   * @param sourceUniverse The source Universe in the xCluster replication config
   * @param targetUniverse The target Universe in the xCluster replication config
   * @return An optional File that is present if transferring the source root certificate is
   *     necessary
   * @throws IllegalArgumentException If setting up a replication config between a universe with
   *     node-to-node TLS and one without; It is not supported by coreDB
   */
  public static Optional<File> getSourceCertificateIfNecessary(
      Universe sourceUniverse, Universe targetUniverse) {
    String sourceCertificatePath = sourceUniverse.getCertificateNodetoNode();
    String targetCertificatePath = targetUniverse.getCertificateNodetoNode();

    if (sourceCertificatePath == null && targetCertificatePath == null) {
      return Optional.empty();
    }
    if (sourceCertificatePath != null && targetCertificatePath != null) {
      UniverseDefinitionTaskParams targetUniverseDetails = targetUniverse.getUniverseDetails();
      UniverseDefinitionTaskParams.UserIntent userIntent =
          targetUniverseDetails.getPrimaryCluster().userIntent;
      // If the "certs_for_cdc_dir" gflag is set, it must be set on masters and tservers with the
      // same value.
      String gflagValueOnMasters = userIntent.masterGFlags.get(SOURCE_ROOT_CERTS_DIR_GFLAG);
      String gflagValueOnTServers = userIntent.tserverGFlags.get(SOURCE_ROOT_CERTS_DIR_GFLAG);
      if ((gflagValueOnMasters != null || gflagValueOnTServers != null)
          && !Objects.equals(gflagValueOnMasters, gflagValueOnTServers)) {
        throw new IllegalStateException(
            String.format(
                "The %s gflag must "
                    + "be set on masters and tservers with the same value or not set at all: "
                    + "gflagValueOnMasters: %s, gflagValueOnTServers: %s",
                SOURCE_ROOT_CERTS_DIR_GFLAG, gflagValueOnMasters, gflagValueOnTServers));
      }
      // If the "certs_for_cdc_dir" gflag is set on the target universe, the certificate must
      // be transferred even though the universes are using the same certs.
      if (!sourceCertificatePath.equals(targetCertificatePath)
          || gflagValueOnMasters != null
          || targetUniverseDetails.xClusterInfo.isSourceRootCertDirPathGflagConfigured()) {
        File sourceCertificate = new File(sourceCertificatePath);
        if (!sourceCertificate.exists()) {
          throw new IllegalStateException(
              String.format("sourceCertificate file \"%s\" does not exist", sourceCertificate));
        }
        return Optional.of(sourceCertificate);
      }
      // The "certs_for_cdc_dir" gflag is not set and certs are equal, so the target universe does
      // not need the source cert.
      return Optional.empty();
    }
    throw new IllegalArgumentException(
        "A replication config cannot be set between a universe with node-to-node encryption "
            + "enabled and a universe with node-to-node encryption disabled.");
  }

  protected SubTaskGroup createTransferXClusterCertsRemoveTasks() {
    return createTransferXClusterCertsRemoveTasks(getXClusterConfigFromTaskParams());
  }

  protected void createDeleteXClusterConfigSubtasks() {
    createDeleteXClusterConfigSubtasks(getXClusterConfigFromTaskParams());
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

  protected void checkBootstrapRequiredForReplicationSetup(Set<String> tableIds) {
    XClusterConfig xClusterConfig = getXClusterConfigFromTaskParams();
    log.info(
        "Running checkBootstrapRequired with (sourceUniverse={},xClusterUuid={},tableIds={})",
        xClusterConfig.sourceUniverseUUID,
        xClusterConfig,
        tableIds);

    try {
      // Check whether bootstrap is required.
      Map<String, Boolean> isBootstrapRequiredMap =
          isBootstrapRequired(tableIds, xClusterConfig.sourceUniverseUUID);
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
   * <p>Note: You must run {@link #checkBootstrapRequiredForReplicationSetup} method before running
   * this method to have the most updated values.
   *
   * @param tableIds A set of tables to check whether they need bootstrapping
   * @return A set of tables that need to be bootstrapped
   * @see #checkBootstrapRequiredForReplicationSetup(Set)
   */
  protected Set<XClusterTableConfig> getTablesNeedBootstrap(Set<String> tableIds) {
    return getXClusterConfigFromTaskParams()
        .getTablesById(tableIds)
        .stream()
        .filter(tableConfig -> tableConfig.needBootstrap)
        .collect(Collectors.toSet());
  }

  /** @see #getTablesNeedBootstrap(Set) */
  protected Set<XClusterTableConfig> getTablesNeedBootstrap() {
    return getTablesNeedBootstrap(getXClusterConfigFromTaskParams().getTables());
  }

  protected Set<String> getTableIdsNeedBootstrap(Set<String> tableIds) {
    return getTablesNeedBootstrap(tableIds)
        .stream()
        .map(tableConfig -> tableConfig.tableId)
        .collect(Collectors.toSet());
  }

  protected Set<String> getTableIdsNeedBootstrap() {
    return getTableIdsNeedBootstrap(getXClusterConfigFromTaskParams().getTables());
  }

  public static Set<String> getTableIds(
      Collection<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tablesInfoList) {
    if (tablesInfoList == null) {
      return Collections.emptySet();
    }
    return tablesInfoList
        .stream()
        .map(tableInfo -> tableInfo.getId().toStringUtf8())
        .collect(Collectors.toSet());
  }

  /**
   * It returns a set of tables that xCluster replication can be set up without bootstrapping.
   *
   * <p>Note: You must run {@link #checkBootstrapRequiredForReplicationSetup} method before running
   * this method to have the most updated values.
   *
   * @param tableIds A set of tables to check whether they need bootstrapping
   * @return A set of tables that do not need to be bootstrapped
   * @see #checkBootstrapRequiredForReplicationSetup(Set)
   */
  protected Set<XClusterTableConfig> getTablesNotNeedBootstrap(Set<String> tableIds) {
    return getXClusterConfigFromTaskParams()
        .getTablesById(tableIds)
        .stream()
        .filter(tableConfig -> !tableConfig.needBootstrap)
        .collect(Collectors.toSet());
  }

  /** @see #getTablesNotNeedBootstrap(Set) */
  protected Set<XClusterTableConfig> getTablesNotNeedBootstrap() {
    return getTablesNotNeedBootstrap(getXClusterConfigFromTaskParams().getTables());
  }

  protected Set<String> getTableIdsNotNeedBootstrap(Set<String> tableIds) {
    return getTablesNotNeedBootstrap(tableIds)
        .stream()
        .map(tableConfig -> tableConfig.tableId)
        .collect(Collectors.toSet());
  }

  protected Set<String> getTableIdsNotNeedBootstrap() {
    return getTableIdsNotNeedBootstrap(getXClusterConfigFromTaskParams().getTables());
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

  protected Map<String, Boolean> isBootstrapRequired(Set<String> tableIds, UUID sourceUniverseUuid)
      throws Exception {
    return isBootstrapRequired(this.ybService, tableIds, sourceUniverseUuid);
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

  /**
   * It synchronizes whether the replication is set up for each table in the Platform DB with what
   * state it has in the target universe cluster config.
   *
   * @param config The cluster config of the target universe
   * @param xClusterConfig The xClusterConfig object that {@code tableIds} belong to
   * @param tableIds The IDs of the table to synchronize
   * @return A boolean showing whether a replication group corresponding to the xCluster config
   *     exists
   */
  protected boolean syncReplicationSetUpStateForTables(
      CatalogEntityInfo.SysClusterConfigEntryPB config,
      XClusterConfig xClusterConfig,
      Set<String> tableIds) {
    CdcConsumer.ProducerEntryPB replicationGroup =
        config
            .getConsumerRegistry()
            .getProducerMapMap()
            .get(xClusterConfig.getReplicationGroupName());
    if (replicationGroup == null) {
      return false;
    }

    Set<String> tableIdsWithReplication =
        replicationGroup
            .getStreamMapMap()
            .values()
            .stream()
            .map(CdcConsumer.StreamEntryPB::getProducerTableId)
            .collect(Collectors.toSet());
    log.debug("Table ids found in the target universe cluster config: {}", tableIdsWithReplication);
    Map<Boolean, List<String>> tableIdsPartitionedByReplicationSetupDone =
        tableIds.stream().collect(Collectors.partitioningBy(tableIdsWithReplication::contains));
    xClusterConfig.setReplicationSetupDone(
        tableIdsPartitionedByReplicationSetupDone.get(true), true /* replicationSetupDone */);
    xClusterConfig.setReplicationSetupDone(
        tableIdsPartitionedByReplicationSetupDone.get(false), false /* replicationSetupDone */);
    return true;
  }
}
