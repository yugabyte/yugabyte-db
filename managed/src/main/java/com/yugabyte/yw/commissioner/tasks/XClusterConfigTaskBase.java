// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.google.api.client.util.Throwables;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableSet;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.TaskExecutor;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.AddNamespaceToXClusterReplication;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.BootstrapProducer;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.CheckBootstrapRequired;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.CreateOutboundReplicationGroup;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.DeleteRemnantStreams;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.ReplicateNamespaces;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.SetReplicationPaused;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.SetRestoreTime;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.WaitForReplicationDrain;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.XClusterAddNamespaceToOutboundReplicationGroup;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.XClusterConfigRename;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.XClusterConfigSetStatus;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.XClusterConfigSetStatusForNamespaces;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.XClusterConfigSetStatusForTables;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.XClusterConfigSetup;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.XClusterConfigSync;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.XClusterDbReplicationSetup;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.XClusterRemoveNamespaceFromOutboundReplicationGroup;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.XClusterRemoveNamespaceFromTargetUniverse;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.common.XClusterUtil;
import com.yugabyte.yw.common.backuprestore.BackupHelper;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.common.table.TableInfoUtil;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.controllers.handlers.UniverseTableHandler;
import com.yugabyte.yw.forms.DrConfigTaskParams;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.forms.TableInfoForm.NamespaceInfoResp;
import com.yugabyte.yw.forms.TableInfoForm.TableInfoResp;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData.BootstrapParams;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
import com.yugabyte.yw.models.DrConfig;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.ConfigType;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
import com.yugabyte.yw.models.XClusterNamespaceConfig;
import com.yugabyte.yw.models.XClusterTableConfig;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.helpers.TaskType;
import java.nio.file.Paths;
import java.time.Duration;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.function.Function;
import java.util.stream.Collectors;
import javax.annotation.Nullable;
import lombok.Data;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.lang3.StringUtils;
import org.yb.CommonTypes;
import org.yb.CommonTypes.TableType;
import org.yb.WireProtocol.AppStatusPB.ErrorCode;
import org.yb.cdc.CdcConsumer;
import org.yb.cdc.CdcConsumer.ProducerEntryPB;
import org.yb.cdc.CdcConsumer.StreamEntryPB;
import org.yb.client.GetMasterClusterConfigResponse;
import org.yb.client.GetTableSchemaResponse;
import org.yb.client.GetUniverseReplicationInfoResponse;
import org.yb.client.GetXClusterOutboundReplicationGroupInfoResponse;
import org.yb.client.IsSetupUniverseReplicationDoneResponse;
import org.yb.client.ListTablesResponse;
import org.yb.client.YBClient;
import org.yb.master.CatalogEntityInfo;
import org.yb.master.MasterDdlOuterClass;
import org.yb.master.MasterDdlOuterClass.ListTablesResponsePB.TableInfo;
import org.yb.master.MasterReplicationOuterClass.GetUniverseReplicationInfoResponsePB.DbScopedInfoPB;
import org.yb.master.MasterReplicationOuterClass.GetXClusterOutboundReplicationGroupInfoResponsePB.NamespaceInfoPB;
import org.yb.master.MasterReplicationOuterClass.ReplicationStatusErrorPB;
import org.yb.master.MasterReplicationOuterClass.ReplicationStatusPB;
import org.yb.master.MasterTypes;
import org.yb.master.MasterTypes.RelationType;
import play.mvc.Http.Status;

@Slf4j
public abstract class XClusterConfigTaskBase extends UniverseDefinitionTaskBase {

  protected final XClusterUniverseService xClusterUniverseService;
  public static final String XCLUSTER_ROOT_CERTS_DIR_GFLAG = "certs_for_cdc_dir";
  public static final String DEFAULT_XCLUSTER_ROOT_CERTS_DIR_NAME = "/yugabyte-tls-producer";
  public static final String XCLUSTER_ROOT_CERTIFICATE_NAME = "ca.crt";
  public static final String ENABLE_REPLICATE_TRANSACTION_STATUS_TABLE_GFLAG =
      "enable_replicate_transaction_status_table";
  public static final boolean ENABLE_REPLICATE_TRANSACTION_STATUS_TABLE_DEFAULT = false;
  public static final Boolean TRANSACTION_SOURCE_UNIVERSE_ROLE_ACTIVE_DEFAULT = true;
  public static final Boolean TRANSACTION_TARGET_UNIVERSE_ROLE_ACTIVE_DEFAULT = true;
  public static final String MINIMUN_VERSION_TRANSACTIONAL_XCLUSTER_SUPPORT = "2.18.1.0-b1";
  public static final int LOGICAL_CLOCK_NUM_BITS_IN_HYBRID_CLOCK = 12;
  public static final String TXN_XCLUSTER_SAFETIME_LAG_NAME = "consumer_safe_time_lag";

  public static final List<XClusterConfig.XClusterConfigStatusType>
      X_CLUSTER_CONFIG_MUST_DELETE_STATUS_LIST =
          ImmutableList.of(
              XClusterConfig.XClusterConfigStatusType.DeletionFailed,
              XClusterConfig.XClusterConfigStatusType.DeletedUniverse);

  public static final List<XClusterTableConfig.Status> X_CLUSTER_TABLE_CONFIG_PENDING_STATUS_LIST =
      ImmutableList.of(
          XClusterTableConfig.Status.Validated,
          XClusterTableConfig.Status.Updating,
          XClusterTableConfig.Status.Bootstrapping);

  public static final List<XClusterNamespaceConfig.Status>
      X_CLUSTER_NAMESPACE_CONFIG_RUNNING_STATUS_LIST =
          ImmutableList.of(XClusterNamespaceConfig.Status.Running);

  public static final List<XClusterNamespaceConfig.Status>
      X_CLUSTER_NAMESPACE_CONFIG_PENDING_STATUS_LIST =
          ImmutableList.of(
              XClusterNamespaceConfig.Status.Validated,
              XClusterNamespaceConfig.Status.Updating,
              XClusterNamespaceConfig.Status.Bootstrapping);

  // XCluster setup is not supported for system and matview tables.
  public static final Set<RelationType> X_CLUSTER_SUPPORTED_TABLE_RELATION_TYPE_SET =
      ImmutableSet.of(
          RelationType.USER_TABLE_RELATION,
          RelationType.INDEX_TABLE_RELATION,
          RelationType.COLOCATED_PARENT_TABLE_RELATION);

  private static final Map<XClusterConfigStatusType, List<TaskType>> STATUS_TO_ALLOWED_TASKS =
      new HashMap<>();

  static {
    STATUS_TO_ALLOWED_TASKS.put(
        XClusterConfigStatusType.Initialized,
        ImmutableList.of(
            TaskType.CreateXClusterConfig,
            TaskType.DeleteXClusterConfig,
            TaskType.RestartXClusterConfig));
    STATUS_TO_ALLOWED_TASKS.put(
        XClusterConfigStatusType.Running,
        ImmutableList.of(
            TaskType.EditXClusterConfig,
            TaskType.DeleteXClusterConfig,
            TaskType.RestartXClusterConfig,
            TaskType.SyncXClusterConfig));
    STATUS_TO_ALLOWED_TASKS.put(
        XClusterConfigStatusType.Updating,
        ImmutableList.of(TaskType.DeleteXClusterConfig, TaskType.RestartXClusterConfig));
    STATUS_TO_ALLOWED_TASKS.put(
        XClusterConfigStatusType.DeletedUniverse, ImmutableList.of(TaskType.DeleteXClusterConfig));
    STATUS_TO_ALLOWED_TASKS.put(
        XClusterConfigStatusType.DeletionFailed, ImmutableList.of(TaskType.DeleteXClusterConfig));
    STATUS_TO_ALLOWED_TASKS.put(
        XClusterConfigStatusType.Failed,
        ImmutableList.of(TaskType.DeleteXClusterConfig, TaskType.RestartXClusterConfig));
  }

  protected XClusterConfigTaskBase(
      BaseTaskDependencies baseTaskDependencies, XClusterUniverseService xClusterUniverseService) {
    super(baseTaskDependencies);
    this.xClusterUniverseService = xClusterUniverseService;
  }

  @Override
  public void initialize(ITaskParams params) {
    super.initialize(params);
  }

  @Override
  public String getName() {
    if (taskParams().getXClusterConfig() != null) {
      return String.format(
          "%s(uuid=%s, universe=%s)",
          this.getClass().getSimpleName(),
          taskParams().getXClusterConfig().getUuid(),
          taskParams().getUniverseUUID());
    } else {
      return String.format(
          "%s(universe=%s)", this.getClass().getSimpleName(), taskParams().getUniverseUUID());
    }
  }

  @Override
  protected XClusterConfigTaskParams taskParams() {
    return (XClusterConfigTaskParams) taskParams;
  }

  protected XClusterConfig getXClusterConfigFromTaskParams() {
    XClusterConfig xClusterConfig = taskParams().getXClusterConfig();
    if (xClusterConfig == null) {
      throw new RuntimeException("xClusterConfig in task params is null");
    }
    return xClusterConfig;
  }

  protected Optional<XClusterConfig> maybeGetXClusterConfig() {
    return XClusterConfig.maybeGet(taskParams().getXClusterConfig().getUuid());
  }

  public static boolean isInMustDeleteStatus(XClusterConfig xClusterConfig) {
    if (xClusterConfig == null) {
      throw new RuntimeException("xClusterConfig cannot be null");
    }
    return X_CLUSTER_CONFIG_MUST_DELETE_STATUS_LIST.contains(xClusterConfig.getStatus());
  }

  public static boolean isXClusterSupported(
      MasterDdlOuterClass.ListTablesResponsePB.TableInfo tableInfo) {
    if (!X_CLUSTER_SUPPORTED_TABLE_RELATION_TYPE_SET.contains(tableInfo.getRelationType())) {
      return false;
    }
    // We only pass colocated parent tables and not colocated child tables for xcluster.
    return !TableInfoUtil.isColocatedChildTable(tableInfo);
  }

  public static boolean isXClusterSupported(TableInfoResp tableInfoResp) {
    if (!X_CLUSTER_SUPPORTED_TABLE_RELATION_TYPE_SET.contains(tableInfoResp.relationType)) {
      return false;
    }
    // We only pass colocated parent tables and not colocated child tables for xcluster.
    if (tableInfoResp.isColocatedChildTable()) {
      return false;
    }
    return true;
  }

  public static Map<Boolean, List<String>> getTableIdsPartitionedByIsXClusterSupported(
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tableInfoList) {
    return tableInfoList.stream()
        .collect(
            Collectors.partitioningBy(
                XClusterConfigTaskBase::isXClusterSupported,
                Collectors.mapping(XClusterConfigTaskBase::getTableId, Collectors.toList())));
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
    List<TaskType> allowedTaskTypes = STATUS_TO_ALLOWED_TASKS.get(xClusterConfig.getStatus());
    if (allowedTaskTypes == null) {
      log.warn(
          "Cannot retrieve the list of allowed tasks because it is not defined for status={}",
          xClusterConfig.getStatus());
    }
    return allowedTaskTypes;
  }

  public static String getProducerCertsDir(UUID providerUuid) {
    Provider provider = Provider.getOrBadRequest(providerUuid);
    // For Kubernetes universe, we must use the PV instead of home directory.
    return Paths.get(
            (provider.getCode().equals(CloudType.kubernetes.toString())
                ? KubernetesTaskBase.K8S_NODE_YW_DATA_DIR
                : provider.getYbHome()),
            DEFAULT_XCLUSTER_ROOT_CERTS_DIR_NAME)
        .toString();
  }

  public static String getProducerCertsDir(String providerUuid) {
    return getProducerCertsDir(UUID.fromString(providerUuid));
  }

  protected SubTaskGroup createXClusterConfigSetupTask(
      XClusterConfig xClusterConfig, Set<String> tableIds) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("XClusterConfigSetup");
    XClusterConfigSetup.Params xClusterConfigParams = new XClusterConfigSetup.Params();
    xClusterConfigParams.setUniverseUUID(xClusterConfig.getTargetUniverseUUID());
    xClusterConfigParams.xClusterConfig = xClusterConfig;
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
  protected SubTaskGroup createXClusterConfigSetStatusTask(
      XClusterConfig xClusterConfig, XClusterConfigStatusType desiredStatus) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("XClusterConfigSetStatus");
    XClusterConfigSetStatus.Params setStatusParams = new XClusterConfigSetStatus.Params();
    setStatusParams.setUniverseUUID(xClusterConfig.getTargetUniverseUUID());
    setStatusParams.xClusterConfig = xClusterConfig;
    setStatusParams.desiredStatus = desiredStatus;
    XClusterConfigSetStatus task = createTask(XClusterConfigSetStatus.class);
    task.initialize(setStatusParams);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  protected SubTaskGroup createXClusterConfigSetStatusForTablesTask(
      XClusterConfig xClusterConfig,
      Set<String> tableIds,
      XClusterTableConfig.Status desiredStatus) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("XClusterConfigSetStatusForTables");
    XClusterConfigSetStatusForTables.Params setStatusForTablesParams =
        new XClusterConfigSetStatusForTables.Params();
    setStatusForTablesParams.setUniverseUUID(xClusterConfig.getTargetUniverseUUID());
    setStatusForTablesParams.xClusterConfig = xClusterConfig;
    setStatusForTablesParams.tableIds = tableIds;
    setStatusForTablesParams.desiredStatus = desiredStatus;

    XClusterConfigSetStatusForTables task = createTask(XClusterConfigSetStatusForTables.class);
    task.initialize(setStatusForTablesParams);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createXClusterConfigSetStatusForNamespaceTask(
      XClusterConfig xClusterConfig,
      Set<String> namespaceIds,
      XClusterNamespaceConfig.Status desiredStatus) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("XClusterConfigSetStatusForTables");
    XClusterConfigSetStatusForNamespaces.Params setStatusForTablesParams =
        new XClusterConfigSetStatusForNamespaces.Params();
    setStatusForTablesParams.setUniverseUUID(xClusterConfig.getTargetUniverseUUID());
    setStatusForTablesParams.xClusterConfig = xClusterConfig;
    setStatusForTablesParams.dbs = namespaceIds;
    setStatusForTablesParams.desiredStatus = desiredStatus;

    XClusterConfigSetStatusForNamespaces task =
        createTask(XClusterConfigSetStatusForNamespaces.class);
    task.initialize(setStatusForTablesParams);
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
  protected SubTaskGroup createSetReplicationPausedTask(
      XClusterConfig xClusterConfig, boolean pause) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("SetReplicationPaused");
    SetReplicationPaused.Params setReplicationPausedParams = new SetReplicationPaused.Params();
    setReplicationPausedParams.setUniverseUUID(xClusterConfig.getTargetUniverseUUID());
    setReplicationPausedParams.xClusterConfig = xClusterConfig;
    setReplicationPausedParams.pause = pause;

    SetReplicationPaused task = createTask(SetReplicationPaused.class);
    task.initialize(setReplicationPausedParams);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  protected SubTaskGroup createSetReplicationPausedTask(
      XClusterConfig xClusterConfig, String status) {
    return createSetReplicationPausedTask(xClusterConfig, status.equals("Paused"));
  }

  protected SubTaskGroup createXClusterConfigRenameTask(
      XClusterConfig xClusterConfig, String newName) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("XClusterConfigRename");
    XClusterConfigRename.Params xClusterConfigRenameParams = new XClusterConfigRename.Params();
    xClusterConfigRenameParams.setUniverseUUID(xClusterConfig.getTargetUniverseUUID());
    xClusterConfigRenameParams.xClusterConfig = xClusterConfig;
    xClusterConfigRenameParams.newName = newName;

    XClusterConfigRename task = createTask(XClusterConfigRename.class);
    task.initialize(xClusterConfigRenameParams);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  protected SubTaskGroup createXClusterConfigSyncTask() {
    SubTaskGroup subTaskGroup = createSubTaskGroup("XClusterConfigSync");
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

  protected void waitForXClusterOperation(
      XClusterConfig xClusterConfig, IPollForXClusterOperation p) {

    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    Duration xclusterWaitTimeout =
        this.confGetter.getConfForScope(universe, UniverseConfKeys.xclusterSetupAlterTimeout);

    try {
      IsSetupUniverseReplicationDoneResponse doneResponse = null;
      int numAttempts = 1;
      long startTime = System.currentTimeMillis();
      while ((System.currentTimeMillis() - startTime) < xclusterWaitTimeout.toMillis()) {
        if (numAttempts % 10 == 0) {
          log.info(
              "Wait for XClusterConfig({}) operation to complete (attempt {}, total timeout {})",
              xClusterConfig.getUuid(),
              numAttempts,
              xclusterWaitTimeout);
        }

        doneResponse = p.poll(xClusterConfig.getReplicationGroupName());
        if (doneResponse.isDone()) {
          break;
        }
        if (doneResponse.hasError()) {
          log.warn(
              "Hit failure during wait for XClusterConfig({}) operation: {}, will continue",
              xClusterConfig.getUuid(),
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
                xClusterConfig.getUuid()));
      }
      if (!doneResponse.isDone()) {
        // TODO: Add unit tests (?) -- May be difficult due to wait
        throw new RuntimeException(
            String.format(
                "Timed out waiting for XClusterConfig(%s) operation to complete after %d ms",
                xClusterConfig.getUuid(), xclusterWaitTimeout.toMillis()));
      }
      if (doneResponse.hasReplicationError()
          && doneResponse.getReplicationError().getCode() != ErrorCode.OK) {
        throw new RuntimeException(
            String.format(
                "XClusterConfig(%s) operation failed: %s",
                xClusterConfig.getUuid(), doneResponse.getReplicationError().toString()));
      }

    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      Throwables.propagate(e);
    }
  }

  /**
   * It creates a group of subtasks to check if bootstrap is required for all the tables in the
   * xCluster config of this task.
   *
   * <p>It creates separate subtasks for each table to increase parallelism because current coreDB
   * implementation can check one table in one RPC call.
   *
   * @param xClusterConfig The xCluster config that includes the tableIds
   * @param tableIds A set of table IDs to check whether they require bootstrap
   * @return The created subtask group
   */
  protected SubTaskGroup createCheckBootstrapRequiredTask(
      XClusterConfig xClusterConfig, Set<String> tableIds) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("CheckBootstrapRequired");
    for (String tableId : tableIds) {
      CheckBootstrapRequired.Params bootstrapRequiredParams = new CheckBootstrapRequired.Params();
      bootstrapRequiredParams.setUniverseUUID(xClusterConfig.getSourceUniverseUUID());
      bootstrapRequiredParams.xClusterConfig = xClusterConfig;
      bootstrapRequiredParams.tableIds = Collections.singleton(tableId);

      CheckBootstrapRequired task = createTask(CheckBootstrapRequired.class);
      task.initialize(bootstrapRequiredParams);
      subTaskGroup.addSubTask(task);
    }
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  protected void checkBootstrapRequiredForReplicationSetup(
      Set<String> tableIds, boolean isForceBootstrap) {
    XClusterConfig xClusterConfig = getXClusterConfigFromTaskParams();
    log.info(
        "Running checkBootstrapRequired with "
            + "(sourceUniverse={},xClusterUuid={},tableIds={},isForceBootstrap={})",
        xClusterConfig.getSourceUniverseUUID(),
        xClusterConfig,
        tableIds,
        isForceBootstrap);

    try {
      // Check whether bootstrap is required.
      Map<String, Boolean> isBootstrapRequiredMap;

      if (isForceBootstrap) {
        // Uncommon case, only happens during DR repair.
        log.info("Forcing all tables to be bootstrapped");
        isBootstrapRequiredMap =
            tableIds.stream().collect(Collectors.toMap(tid -> tid, tid -> true));
      } else {
        isBootstrapRequiredMap =
            isBootstrapRequired(tableIds, xClusterConfig.getSourceUniverseUUID());
      }
      log.debug("IsBootstrapRequired result is {}", isBootstrapRequiredMap);
      // Persist whether bootstrap is required.
      Map<Boolean, List<String>> tableIdsPartitionedByNeedBootstrap =
          isBootstrapRequiredMap.keySet().stream()
              .collect(Collectors.partitioningBy(isBootstrapRequiredMap::get));
      xClusterConfig.updateNeedBootstrapForTables(
          tableIdsPartitionedByNeedBootstrap.get(true), true /* needBootstrap */);
      xClusterConfig.updateNeedBootstrapForTables(
          tableIdsPartitionedByNeedBootstrap.get(false), false /* needBootstrap */);
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    }

    log.info("Completed checkBootstrapRequired");
  }

  protected void checkBootstrapRequiredForReplicationSetup(Set<String> tableIds) {
    checkBootstrapRequiredForReplicationSetup(tableIds, false /*isForceBootstrap */);
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
    return getXClusterConfigFromTaskParams().getTablesById(tableIds).stream()
        .filter(tableConfig -> tableConfig.isNeedBootstrap())
        .collect(Collectors.toSet());
  }

  /**
   * @see #getTablesNeedBootstrap(Set)
   */
  protected Set<XClusterTableConfig> getTablesNeedBootstrap() {
    return getTablesNeedBootstrap(getXClusterConfigFromTaskParams().getTableIds());
  }

  protected Set<String> getTableIdsNeedBootstrap(Set<String> tableIds) {
    return getTablesNeedBootstrap(tableIds).stream()
        .map(tableConfig -> tableConfig.getTableId())
        .collect(Collectors.toSet());
  }

  protected Set<String> getTableIdsNeedBootstrap() {
    return getTableIdsNeedBootstrap(getXClusterConfigFromTaskParams().getTableIds());
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
    return getXClusterConfigFromTaskParams().getTablesById(tableIds).stream()
        .filter(tableConfig -> !tableConfig.isNeedBootstrap())
        .collect(Collectors.toSet());
  }

  /**
   * @see #getTablesNotNeedBootstrap(Set)
   */
  protected Set<XClusterTableConfig> getTablesNotNeedBootstrap() {
    return getTablesNotNeedBootstrap(getXClusterConfigFromTaskParams().getTableIds());
  }

  protected Set<String> getTableIdsNotNeedBootstrap(Set<String> tableIds) {
    return getTablesNotNeedBootstrap(tableIds).stream()
        .map(tableConfig -> tableConfig.getTableId())
        .collect(Collectors.toSet());
  }

  protected Set<String> getTableIdsNotNeedBootstrap() {
    return getTableIdsNotNeedBootstrap(getXClusterConfigFromTaskParams().getTableIds());
  }

  /**
   * It creates a subtask to bootstrap the set of tables passed in.
   *
   * @param tableIds The ids of the tables to be bootstrapped
   */
  protected SubTaskGroup createBootstrapProducerTask(
      XClusterConfig xClusterConfig, Collection<String> tableIds) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("BootstrapProducer");
    BootstrapProducer.Params bootstrapProducerParams = new BootstrapProducer.Params();
    bootstrapProducerParams.setUniverseUUID(xClusterConfig.getSourceUniverseUUID());
    bootstrapProducerParams.xClusterConfig = xClusterConfig;
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
  protected SubTaskGroup createSetRestoreTimeTask(
      XClusterConfig xClusterConfig, Set<String> tableIds) {
    TaskExecutor.SubTaskGroup subTaskGroup = createSubTaskGroup("SetBootstrapBackup");
    SetRestoreTime.Params setRestoreTimeParams = new SetRestoreTime.Params();
    setRestoreTimeParams.setUniverseUUID(xClusterConfig.getSourceUniverseUUID());
    setRestoreTimeParams.xClusterConfig = xClusterConfig;
    setRestoreTimeParams.tableIds = tableIds;

    SetRestoreTime task = createTask(SetRestoreTime.class);
    task.initialize(setRestoreTimeParams);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  protected SubTaskGroup createDeleteRemnantStreamsTask(UUID universeUuid, String namespaceName) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("DeleteRemnantStreams");
    DeleteRemnantStreams.Params deleteRemnantStreamsParams = new DeleteRemnantStreams.Params();
    deleteRemnantStreamsParams.setUniverseUUID(universeUuid);
    deleteRemnantStreamsParams.namespaceName = namespaceName;

    DeleteRemnantStreams task = createTask(DeleteRemnantStreams.class);
    task.initialize(deleteRemnantStreamsParams);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  protected SubTaskGroup createReplicateNamespacesTask(XClusterConfig xClusterConfig) {
    TaskExecutor.SubTaskGroup subTaskGroup = createSubTaskGroup("ReplicateNamespaces");
    XClusterConfigTaskParams replicateNamespacesParams = new XClusterConfigTaskParams();
    replicateNamespacesParams.setUniverseUUID(xClusterConfig.getTargetUniverseUUID());
    replicateNamespacesParams.xClusterConfig = xClusterConfig;
    ReplicateNamespaces task = createTask(ReplicateNamespaces.class);
    task.initialize(replicateNamespacesParams);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public static CatalogEntityInfo.SysClusterConfigEntryPB getClusterConfig(
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

  private static Set<String> getConsumerTableIdsFromClusterConfig(
      CatalogEntityInfo.SysClusterConfigEntryPB clusterConfig,
      Collection<String> excludeReplicationGroupNames) {
    Set<String> consumerTableIdsFromClusterConfig = new HashSet<>();
    Map<java.lang.String, org.yb.cdc.CdcConsumer.ProducerEntryPB> producerMapMap =
        clusterConfig.getConsumerRegistry().getProducerMapMap();
    producerMapMap.forEach(
        (replicationGroupName, replicationGroup) -> {
          if (excludeReplicationGroupNames.contains(replicationGroupName)
              || replicationGroup == null) {
            return;
          }
          consumerTableIdsFromClusterConfig.addAll(
              replicationGroup.getStreamMapMap().values().stream()
                  .map(StreamEntryPB::getConsumerTableId)
                  .collect(Collectors.toSet()));
        });
    return consumerTableIdsFromClusterConfig;
  }

  private static Set<String> getConsumerTableIdsFromClusterConfig(
      CatalogEntityInfo.SysClusterConfigEntryPB clusterConfig, String replicationGroupName) {
    Set<String> consumerTableIdsFromClusterConfig = new HashSet<>();
    ProducerEntryPB replicationGroup =
        getReplicationGroupEntry(clusterConfig, replicationGroupName);
    replicationGroup.getStreamMapMap().values().stream()
        .map(StreamEntryPB::getConsumerTableId)
        .forEach(consumerTableIdsFromClusterConfig::add);
    return consumerTableIdsFromClusterConfig;
  }

  public static ProducerEntryPB getReplicationGroupEntry(
      CatalogEntityInfo.SysClusterConfigEntryPB clusterConfig, String replicationGroupName) {
    return clusterConfig.getConsumerRegistry().getProducerMapOrThrow(replicationGroupName);
  }

  public static Set<String> getProducerTableIdsFromClusterConfig(
      CatalogEntityInfo.SysClusterConfigEntryPB clusterConfig, String replicationGroupName) {
    Set<String> producerTableIdsFromClusterConfig = new HashSet<>();
    ProducerEntryPB replicationGroup =
        getReplicationGroupEntry(clusterConfig, replicationGroupName);
    replicationGroup.getStreamMapMap().values().stream()
        .map(StreamEntryPB::getProducerTableId)
        .forEach(producerTableIdsFromClusterConfig::add);
    return producerTableIdsFromClusterConfig;
  }

  /**
   * It reads information for the associated replication group in DB with the xCluster config and
   * updates its state.
   *
   * @param config The cluster config on the target universe
   * @param xClusterConfig The xCluster config object to sync
   * @param tableIds The list of tables in the {@code xClusterConfig} to sync
   */
  public static void syncXClusterConfigWithReplicationGroup(
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
              xClusterConfig.getReplicationGroupName(), xClusterConfig.getTargetUniverseUUID()));
    }

    // Ensure disable stream state is in sync with Platform's point of view.
    if (replicationGroup.getDisableStream() != xClusterConfig.isPaused()) {
      log.warn(
          "Detected mismatched disable state for replication group {} and xCluster config {}",
          replicationGroup,
          xClusterConfig);
      xClusterConfig.updatePaused(replicationGroup.getDisableStream());
    }

    // Sync id and status for each stream.
    if (xClusterConfig.getType() != ConfigType.Db) {
      Map<String, CdcConsumer.StreamEntryPB> replicationStreams =
          replicationGroup.getStreamMapMap();
      Map<String, String> streamMap =
          replicationStreams.entrySet().stream()
              .collect(Collectors.toMap(e -> e.getValue().getProducerTableId(), Map.Entry::getKey));
      for (String tableId : tableIds) {
        Optional<XClusterTableConfig> tableConfig = xClusterConfig.maybeGetTableById(tableId);
        if (tableConfig.isEmpty()) {
          throw new RuntimeException(
              String.format(
                  "Could not find tableId (%s) in the xCluster config %s",
                  tableId, xClusterConfig));
        }
        String streamId = streamMap.get(tableId);
        if (Objects.isNull(streamId)) {
          log.warn(
              "No stream id found for table ({}) in the cluster config for xCluster config ({})",
              tableId,
              xClusterConfig.getUuid());
          tableConfig.get().setStreamId(null);
          tableConfig.get().setStatus(XClusterTableConfig.Status.Failed);
          tableConfig.get().update();
          continue;
        }
        if (Objects.nonNull(tableConfig.get().getStreamId())
            && !tableConfig.get().getStreamId().equals(streamId)) {
          log.warn(
              "Bootstrap id ({}) for table ({}) is different from stream id ({}) in the "
                  + "cluster config for xCluster config ({})",
              tableConfig.get().getStreamId(),
              tableId,
              streamId,
              xClusterConfig.getUuid());
        }
        tableConfig.get().setStreamId(streamId);
        tableConfig.get().setStatus(XClusterTableConfig.Status.Running);
        tableConfig.get().setReplicationSetupDone(true);
        tableConfig.get().update();
        log.info(
            "StreamId for table {} in xCluster config {} is set to {}",
            tableId,
            xClusterConfig.getUuid(),
            streamId);
      }
    }
  }

  public static boolean supportsMultipleTablesWithIsBootstrapRequired(Universe universe) {
    // The minimum YBDB version that supports multiple tables with IsBootstrapRequired is
    // 2.15.3.0-b64.
    return Util.compareYbVersions(
            "2.15.3.0-b63",
            universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion,
            true /* suppressFormatError */)
        < 0;
  }

  public static boolean supportsGetReplicationStatus(Universe universe) {
    // The minimum YBDB version that supports GetReplicationStatus RPC is 2.18.0.0-b1.
    return Util.compareYbVersions(
            "2.18.0.0-b1",
            universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion,
            true /* suppressFormatError */)
        < 0;
  }

  public static boolean supportsTxnXCluster(Universe universe) {
    // The minimum YBDB version that supports transactional xCluster is 2.17.3.0-b2.
    return Util.compareYbVersions(
            MINIMUN_VERSION_TRANSACTIONAL_XCLUSTER_SUPPORT,
            universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion,
            true /* suppressFormatError */)
        < 0;
  }

  protected Map<String, Boolean> isBootstrapRequired(Set<String> tableIds, UUID sourceUniverseUuid)
      throws Exception {
    return this.xClusterUniverseService.isBootstrapRequired(
        tableIds, null /* xClusterConfig */, sourceUniverseUuid);
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
      xClusterConfig.updateReplicationSetupDone(tableIds, false /* replicationSetupDone */);
      return false;
    }

    Set<String> tableIdsWithReplication =
        replicationGroup.getStreamMapMap().values().stream()
            .map(CdcConsumer.StreamEntryPB::getProducerTableId)
            .collect(Collectors.toSet());
    log.debug("Table ids found in the target universe cluster config: {}", tableIdsWithReplication);
    Map<Boolean, List<String>> tableIdsPartitionedByReplicationSetupDone =
        tableIds.stream().collect(Collectors.partitioningBy(tableIdsWithReplication::contains));
    xClusterConfig.updateReplicationSetupDone(
        tableIdsPartitionedByReplicationSetupDone.get(true), true /* replicationSetupDone */);
    xClusterConfig.updateReplicationSetupDone(
        tableIdsPartitionedByReplicationSetupDone.get(false), false /* replicationSetupDone */);
    return true;
  }

  /**
   * It returns the list of table info from a universe for a set of table ids. Moreover, it ensures
   * that all requested tables exist on the source and target universes, and they have the same
   * type. Also, if the table type is YSQL and bootstrap is required, it ensures all the tables in a
   * keyspace are selected because per-table backup/restore is not supported for YSQL.
   *
   * @param ybService The service to get a YB client from
   * @param requestedTableIds The table ids to get the {@code TableInfo} for
   * @param bootstrapParams The parameters used for bootstrapping
   * @param sourceUniverse The universe to gather the {@code TableInfo}s from
   * @param targetUniverse The universe to check that matching tables exist on
   * @param currentReplicationGroupName The replication group name of the current xCluster config if
   *     any
   * @return A list of {@link MasterDdlOuterClass.ListTablesResponsePB.TableInfo} containing table
   *     info of the tables whose id is specified at {@code requestedTableIds}
   */
  // Todo: This method is no longer use in the code base. It is only used in the utests and should
  //  be removed.
  @Deprecated
  public static Pair<List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo>, Set<String>>
      getRequestedTableInfoListAndVerify(
          YBClientService ybService,
          Set<String> requestedTableIds,
          @Nullable BootstrapParams bootstrapParams,
          Universe sourceUniverse,
          Universe targetUniverse,
          @Nullable String currentReplicationGroupName,
          XClusterConfig.ConfigType configType) {
    log.debug(
        "requestedTableIds are {} and BootstrapParams are {}", requestedTableIds, bootstrapParams);
    // Ensure at least one table exists to verify.
    if (requestedTableIds.isEmpty()) {
      throw new IllegalArgumentException("requestedTableIds cannot be empty");
    }
    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> sourceTableInfoList =
        getTableInfoList(ybService, sourceUniverse);

    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTableInfoList =
        filterTableInfoListByTableIds(sourceTableInfoList, requestedTableIds);

    CommonTypes.TableType tableType = getTableType(requestedTableInfoList);

    // Txn xCluster is supported only for YSQL tables.
    if (configType.equals(ConfigType.Txn) && !tableType.equals(TableType.PGSQL_TABLE_TYPE)) {
      throw new IllegalArgumentException(
          String.format(
              "Transaction xCluster is supported only for YSQL tables. Table type %s is selected",
              tableType));
    }

    // XCluster replication can be set up only for YCQL and YSQL tables.
    if (!tableType.equals(CommonTypes.TableType.YQL_TABLE_TYPE)
        && !tableType.equals(CommonTypes.TableType.PGSQL_TABLE_TYPE)) {
      throw new IllegalArgumentException(
          String.format(
              "XCluster replication can be set up only for YCQL and YSQL tables: "
                  + "type %s requested",
              tableType));
    }

    // Make sure all the tables on the source universe have a corresponding table on the target
    // universe.
    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> targetTablesInfoList =
        getTableInfoList(ybService, targetUniverse);
    Map<String, String> sourceTableIdTargetTableIdMap =
        getSourceTableIdTargetTableIdMap(requestedTableInfoList, targetTablesInfoList);

    // If some tables do not exist on the target universe, bootstrapping is required.
    Set<String> sourceTableIdsWithNoTableOnTargetUniverse =
        sourceTableIdTargetTableIdMap.entrySet().stream()
            .filter(entry -> Objects.isNull(entry.getValue()))
            .map(Entry::getKey)
            .collect(Collectors.toSet());
    if (!sourceTableIdsWithNoTableOnTargetUniverse.isEmpty()) {
      if (Objects.isNull(bootstrapParams)) {
        throw new IllegalArgumentException(
            String.format(
                "Table ids %s do not have corresponding tables on the target universe and "
                    + "they must be bootstrapped but bootstrapParams is null",
                sourceTableIdsWithNoTableOnTargetUniverse));
      }
      if (Objects.isNull(bootstrapParams.tables)
          || !bootstrapParams.tables.containsAll(sourceTableIdsWithNoTableOnTargetUniverse)) {
        throw new IllegalArgumentException(
            String.format(
                "Table ids %s do not have corresponding tables on the target universe and "
                    + "they must be bootstrapped but the set of tables in bootstrapParams (%s) "
                    + "does not contain all of them",
                sourceTableIdsWithNoTableOnTargetUniverse, bootstrapParams.tables));
      }
    }

    if (bootstrapParams != null && bootstrapParams.tables != null) {
      // Ensure tables in bootstrapParams is a subset of requestedTableIds.
      if (!bootstrapParams.allowBootstrap
          && !requestedTableIds.containsAll(bootstrapParams.tables)) {
        throw new IllegalArgumentException(
            String.format(
                "The set of tables in bootstrapParams (%s) is not a subset of "
                    + "requestedTableIds (%s)",
                bootstrapParams.tables, requestedTableIds));
      }

      // Bootstrapping must not be done for tables whose corresponding target table is in
      // replication. It also includes tables that are in reverse replication between the same
      // universes.
      Map<String, String> sourceTableIdTargetTableIdWithBootstrapMap =
          sourceTableIdTargetTableIdMap.entrySet().stream()
              .filter(entry -> bootstrapParams.tables.contains(entry.getKey()))
              .collect(
                  HashMap::new,
                  (map, entry) -> map.put(entry.getKey(), entry.getValue()),
                  HashMap::putAll);

      if (!bootstrapParams.allowBootstrap) {
        bootstrapParams.tables =
            getTableIdsWithoutTablesOnTargetInReplication(
                ybService,
                requestedTableInfoList,
                sourceTableIdTargetTableIdWithBootstrapMap,
                targetUniverse,
                currentReplicationGroupName);
      }

      // If table type is YSQL and bootstrap is requested, all tables in that keyspace are selected.
      if (tableType == CommonTypes.TableType.PGSQL_TABLE_TYPE) {
        groupByNamespaceId(requestedTableInfoList)
            .forEach(
                (namespaceId, tablesInfoList) -> {
                  Set<String> selectedTableIdsInNamespaceToBootstrap =
                      getTableIds(tablesInfoList).stream()
                          .filter(bootstrapParams.tables::contains)
                          .collect(Collectors.toSet());
                  if (!selectedTableIdsInNamespaceToBootstrap.isEmpty()) {
                    Set<String> tableIdsInNamespace =
                        sourceTableInfoList.stream()
                            .filter(
                                tableInfo ->
                                    !tableInfo
                                            .getRelationType()
                                            .equals(RelationType.SYSTEM_TABLE_RELATION)
                                        && tableInfo
                                            .getNamespace()
                                            .getId()
                                            .toStringUtf8()
                                            .equals(namespaceId))
                            .map(tableInfo -> tableInfo.getId().toStringUtf8())
                            .collect(Collectors.toSet());
                    if (!bootstrapParams.allowBootstrap
                        && tableIdsInNamespace.size()
                            != selectedTableIdsInNamespaceToBootstrap.size()) {
                      throw new IllegalArgumentException(
                          String.format(
                              "For YSQL tables, all the tables in a keyspace must be selected: "
                                  + "selected: %s, tables in the keyspace: %s",
                              selectedTableIdsInNamespaceToBootstrap, tableIdsInNamespace));
                    }
                  }
                });
      }
    }

    log.debug("requestedTableInfoList is {}", requestedTableInfoList);
    log.debug(
        "sourceTableIdsWithNoTableOnTargetUniverse is {}",
        sourceTableIdsWithNoTableOnTargetUniverse);
    return new Pair<>(requestedTableInfoList, sourceTableIdsWithNoTableOnTargetUniverse);
  }

  private static Set<String> getTableIdsInReplicationOnTargetUniverse(
      YBClientService ybService,
      Collection<String> tableIds,
      Universe targetUniverse,
      @Nullable String currentReplicationGroupName) {
    Set<String> tableIdsInReplicationOnTargetUniverse = new HashSet<>();
    // In replication as source. It will take care of bidirectional cases as well.
    List<XClusterConfig> xClusterConfigsAsSource =
        XClusterConfig.getBySourceUniverseUUID(targetUniverse.getUniverseUUID());
    xClusterConfigsAsSource.forEach(
        xClusterConfig ->
            tableIdsInReplicationOnTargetUniverse.addAll(
                xClusterConfig.getTableIds().stream()
                    .filter(tableIds::contains)
                    .collect(Collectors.toSet())));
    // In replication as target.
    String targetUniverseMasterAddresses = targetUniverse.getMasterAddresses();
    String targetUniverseCertificate = targetUniverse.getCertificateNodetoNode();
    try (YBClient client =
        ybService.getClient(targetUniverseMasterAddresses, targetUniverseCertificate)) {
      CatalogEntityInfo.SysClusterConfigEntryPB clusterConfig =
          getClusterConfig(client, targetUniverse.getUniverseUUID());
      tableIdsInReplicationOnTargetUniverse.addAll(
          getConsumerTableIdsFromClusterConfig(
                  clusterConfig, Collections.singleton(currentReplicationGroupName))
              .stream()
              .filter(tableIds::contains)
              .collect(Collectors.toSet()));
    } catch (Exception e) {
      log.error("getTableIdsInReplicationOnTargetUniverse hit error : {}", e.getMessage());
      throw new RuntimeException(e);
    }
    log.debug("tableIdsInReplicationOnTargetUniverse is {}", tableIdsInReplicationOnTargetUniverse);
    return tableIdsInReplicationOnTargetUniverse;
  }

  public static Set<String> getTableIdsWithoutTablesOnTargetInReplication(
      YBClientService ybService,
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTableInfoList,
      Map<String, String> sourceTableIdTargetTableIdMap,
      Universe targetUniverse,
      @Nullable String currentReplicationGroupName) {
    Set<String> tableIdsInReplicationOnTargetUniverse =
        getTableIdsInReplicationOnTargetUniverse(
            ybService,
            sourceTableIdTargetTableIdMap.values(),
            targetUniverse,
            currentReplicationGroupName);
    if (tableIdsInReplicationOnTargetUniverse.isEmpty()) {
      return sourceTableIdTargetTableIdMap.keySet();
    }

    CommonTypes.TableType tableType = XClusterConfigTaskBase.getTableType(requestedTableInfoList);
    // For YSQL tables, bootstrapping must be skipped for DBs that have any table in bidirectional
    // replication.
    Set<String> sourceYsqlTableIdsToSkipBidirectional = new HashSet<>();
    if (tableType == CommonTypes.TableType.PGSQL_TABLE_TYPE) {
      Set<String> sourceTableIdsInReplicationOnTargetUniverse =
          sourceTableIdTargetTableIdMap.entrySet().stream()
              .filter(entry -> tableIdsInReplicationOnTargetUniverse.contains(entry.getValue()))
              .map(Map.Entry::getKey)
              .collect(Collectors.toSet());
      XClusterConfigTaskBase.groupByNamespaceId(requestedTableInfoList)
          .forEach(
              (namespaceId, tablesInfoList) -> {
                if (tablesInfoList.stream()
                    .anyMatch(
                        tableInfo ->
                            sourceTableIdsInReplicationOnTargetUniverse.contains(
                                XClusterConfigTaskBase.getTableId(tableInfo)))) {
                  sourceYsqlTableIdsToSkipBidirectional.addAll(
                      XClusterConfigTaskBase.getTableIds(tablesInfoList));
                }
              });
    }

    log.warn(
        "Tables {} are in replication on the target universe {} of this config and cannot be"
            + " bootstrapped; Bootstrapping for their corresponding source table will be disabled."
            + " Also, tables {} will be skipped bootstrapping because they or their sibling tables"
            + " are in bidirectional replication.",
        tableIdsInReplicationOnTargetUniverse,
        targetUniverse.getUniverseUUID(),
        sourceYsqlTableIdsToSkipBidirectional);
    return sourceTableIdTargetTableIdMap.entrySet().stream()
        .filter(
            entry ->
                !tableIdsInReplicationOnTargetUniverse.contains(entry.getValue())
                    && !sourceYsqlTableIdsToSkipBidirectional.contains(entry.getKey()))
        .map(Entry::getKey)
        .collect(Collectors.toSet());
  }

  public static Map<String, String> getSourceTableIdTargetTableIdMap(
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedSourceTablesInfoList,
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> targetTablesInfoList) {
    Map<String, String> sourceTableIdTargetTableIdMap = new HashMap<>();
    if (requestedSourceTablesInfoList.isEmpty()) {
      log.warn("requestedSourceTablesInfoList is empty");
      return sourceTableIdTargetTableIdMap;
    }
    Set<String> notFoundNamespaces = new HashSet<>();
    Set<String> notFoundTables = new HashSet<>();
    // All tables in `requestedTableInfoList` have the same type.
    CommonTypes.TableType tableType = requestedSourceTablesInfoList.get(0).getTableType();

    // Namespace name for one table type is unique.
    Map<String, List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo>>
        targetNamespaceNameTablesInfoListMap =
            groupByNamespaceName(
                targetTablesInfoList.stream()
                    .filter(tableInfo -> tableInfo.getTableType().equals(tableType))
                    .collect(Collectors.toList()));

    groupByNamespaceName(requestedSourceTablesInfoList)
        .forEach(
            (namespaceName, tableInfoListOnSource) -> {
              List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tableInfoListOnTarget =
                  targetNamespaceNameTablesInfoListMap.get(namespaceName);
              if (tableInfoListOnTarget != null) {
                if (tableType.equals(TableType.PGSQL_TABLE_TYPE)) {
                  Map<String, List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo>>
                      schemaNameTableInfoListOnSourceMap =
                          groupByPgSchemaName(tableInfoListOnSource);
                  Map<String, List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo>>
                      schemaNameTableInfoListOnTargetMap =
                          groupByPgSchemaName(tableInfoListOnTarget);
                  schemaNameTableInfoListOnSourceMap.forEach(
                      (schemaName, tableInfoListInSchemaOnSource) -> {
                        List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo>
                            tableInfoListInSchemaOnTarget =
                                schemaNameTableInfoListOnTargetMap.get(schemaName);
                        if (tableInfoListInSchemaOnTarget != null) {
                          Pair<Map<String, String>, Set<String>> tableIdMapNotFoundTableIdsSetPair =
                              getSourceTableIdTargetTableIdMapUnique(
                                  tableInfoListInSchemaOnSource, tableInfoListInSchemaOnTarget);
                          sourceTableIdTargetTableIdMap.putAll(
                              tableIdMapNotFoundTableIdsSetPair.getFirst());
                          tableIdMapNotFoundTableIdsSetPair
                              .getSecond()
                              .forEach(
                                  sourceTableId ->
                                      sourceTableIdTargetTableIdMap.put(sourceTableId, null));
                          notFoundTables.addAll(tableIdMapNotFoundTableIdsSetPair.getSecond());
                        } else {
                          tableInfoListInSchemaOnSource.forEach(
                              sourceTableInfo ->
                                  sourceTableIdTargetTableIdMap.put(
                                      getTableId(sourceTableInfo), null));
                          notFoundTables.addAll(getTableIds(tableInfoListInSchemaOnSource));
                        }
                      });
                } else {
                  Pair<Map<String, String>, Set<String>> tableIdMapNotFoundTableIdsSetPair =
                      getSourceTableIdTargetTableIdMapUnique(
                          tableInfoListOnSource, tableInfoListOnTarget);
                  sourceTableIdTargetTableIdMap.putAll(
                      tableIdMapNotFoundTableIdsSetPair.getFirst());
                  tableIdMapNotFoundTableIdsSetPair
                      .getSecond()
                      .forEach(
                          sourceTableId -> sourceTableIdTargetTableIdMap.put(sourceTableId, null));
                  notFoundTables.addAll(tableIdMapNotFoundTableIdsSetPair.getSecond());
                }
              } else {
                tableInfoListOnSource.forEach(
                    sourceTableInfo ->
                        sourceTableIdTargetTableIdMap.put(getTableId(sourceTableInfo), null));
                notFoundNamespaces.add(namespaceName);
              }
            });
    if (!notFoundNamespaces.isEmpty() || !notFoundTables.isEmpty()) {
      log.warn(
          "Not found namespaces on the target universe: {}, not found tables on the "
              + "target universe: {}",
          notFoundNamespaces,
          notFoundTables);
    }
    log.debug("sourceTableIdTargetTableIdMap is {}", sourceTableIdTargetTableIdMap);
    return sourceTableIdTargetTableIdMap;
  }

  /**
   * Returns a map that maps target table IDs to source table IDs.
   *
   * @param sourceTableInfoList A list of source table information.
   * @param targetTableInfoList A list of target table information.
   * @return A map that maps target table IDs to source table IDs.
   */
  public static Map<String, String> getTargetTableIdToSourceTableIdMap(
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> sourceTableInfoList,
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> targetTableInfoList) {
    log.debug(
        "switching the sequence of source and target tables in order to achieve the"
            + " targetTableIdToSourceTableIdMap");
    Map<String, String> targetTableIdToSourceTableIdMap =
        getSourceTableIdTargetTableIdMap(targetTableInfoList, sourceTableInfoList);
    return targetTableIdToSourceTableIdMap;
  }

  /**
   * It assumes table names in both {@code tableInfoListOnSource} and {@code tableInfoListOnTarget}
   * are unique except for colocated parent table names.
   *
   * @param tableInfoListOnSource The list of table info on the source in a namespace.schema for
   *     `PGSQL_TABLE_TYPE` or namespace for others
   * @param tableInfoListOnTarget The list of table info on the target in a namespace.schema for
   *     `PGSQL_TABLE_TYPE` or namespace for others
   * @return A pair containing a map of source table ID to their corresponding target table ID and a
   *     set of not found table IDs
   */
  private static Pair<Map<String, String>, Set<String>> getSourceTableIdTargetTableIdMapUnique(
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tableInfoListOnSource,
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tableInfoListOnTarget) {
    Map<String, String> sourceTableIdTargetTableIdMap = new HashMap<>();
    Set<String> notFoundTables = new HashSet<>();

    // Find colocated parent table in target universe namespace if exists. There is at most one
    //  colocated parent table per namespace.
    MasterDdlOuterClass.ListTablesResponsePB.TableInfo targetColocatedParentTable =
        tableInfoListOnTarget.stream()
            .filter(TableInfoUtil::isColocatedParentTable)
            .findFirst()
            .orElse(null);

    Map<String, String> tableNameTableIdOnTargetMap =
        tableInfoListOnTarget.stream()
            .collect(
                Collectors.toMap(
                    TableInfo::getName, tableInfo -> tableInfo.getId().toStringUtf8()));
    tableInfoListOnSource.forEach(
        tableInfo -> {
          String tableIdOnTarget = tableNameTableIdOnTargetMap.get(tableInfo.getName());
          if (tableIdOnTarget != null) {
            sourceTableIdTargetTableIdMap.put(tableInfo.getId().toStringUtf8(), tableIdOnTarget);
          } else if (TableInfoUtil.isColocatedParentTable(tableInfo)
              && targetColocatedParentTable != null) {
            sourceTableIdTargetTableIdMap.put(
                tableInfo.getId().toStringUtf8(),
                targetColocatedParentTable.getId().toStringUtf8());
          } else {
            notFoundTables.add(tableInfo.getId().toStringUtf8());
          }
        });
    return new Pair<>(sourceTableIdTargetTableIdMap, notFoundTables);
  }

  /**
   * It gets the table schema information for a list of main tables from a universe.
   *
   * @param ybService The service to get a YB client from
   * @param universe The universe to get the table schema information from
   * @param mainTableUuidList A set of main table uuids to get the schema information for
   * @return A map of main table uuid to its schema information
   */
  public static Map<String, GetTableSchemaResponse> getTableSchemas(
      YBClientService ybService, Universe universe, Set<String> mainTableUuidList) {
    if (mainTableUuidList.isEmpty()) {
      return Collections.emptyMap();
    }
    Map<String, GetTableSchemaResponse> tableSchemaMap = new HashMap<>();
    String universeMasterAddresses = universe.getMasterAddresses();
    String universeCertificate = universe.getCertificateNodetoNode();
    try (YBClient client = ybService.getClient(universeMasterAddresses, universeCertificate)) {
      for (String tableUuid : mainTableUuidList) {
        // To make sure there is no `-` in the table UUID.
        tableUuid = tableUuid.replace("-", "");
        tableSchemaMap.put(tableUuid, client.getTableSchemaByUUID(tableUuid));
      }
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
    return tableSchemaMap;
  }

  /**
   * Checks if the table info will contain an indexed table ID.
   *
   * @param ybSoftwareVersion The version of the YB software.
   * @return True if the table info contains an indexed table ID, false otherwise.
   */
  public static boolean universeTableInfoContainsIndexedTableId(String ybSoftwareVersion) {
    return Util.compareYbVersions("2.21.1.0-b168", ybSoftwareVersion, true) <= 0;
  }

  /**
   * It returns a map from main table id to a list of index table ids associated with the main table
   * in the universe. `getIndexedTableId` was added to YBDB 2.21.1.0-b168.
   *
   * @param tableInfoList The list of table info in the universe
   * @return A map from main table id to a list of index table ids
   */
  public static Map<String, List<String>> getMainTableIndexTablesMap(
      Universe universe,
      Collection<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tableInfoList) {
    // Ensure it is not called for a universe older than 2.21.1.0-b168. For older version use the
    // other getMainTableIndexTablesMap method that uses an RPC available in older universes.
    if (!universeTableInfoContainsIndexedTableId(
        universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion)) {
      throw new IllegalStateException(
          "This method is only supported for universes newer than or equal to 2.21.1.0-b168");
    }
    Map<String, List<String>> mainTableIndexTablesMap = new HashMap<>();
    tableInfoList.forEach(
        tableInfo -> {
          if (TableInfoUtil.isIndexTable(tableInfo)) {
            String mainTableId = tableInfo.getIndexedTableId().replace("-", "");
            mainTableIndexTablesMap
                .computeIfAbsent(mainTableId, k -> new ArrayList<>())
                .add(getTableId(tableInfo));
          }
        });
    log.debug("mainTableIndexTablesMap is {}", mainTableIndexTablesMap);
    return mainTableIndexTablesMap;
  }

  public static Map<String, List<String>> getMainTableIndexTablesMap(
      YBClientService ybService, Universe universe, Set<String> mainTableUuidList) {
    Map<String, GetTableSchemaResponse> tableSchemaMap =
        getTableSchemas(ybService, universe, mainTableUuidList);
    Map<String, List<String>> mainTableIndexTablesMap = new HashMap<>();
    tableSchemaMap.forEach(
        (mainTableUuid, tableSchemaResponse) -> {
          List<String> indexTableUuidList = new ArrayList<>();
          tableSchemaResponse
              .getIndexes()
              .forEach(
                  indexInfo -> {
                    if (!indexInfo.getIndexedTableId().equals(mainTableUuid)) {
                      throw new IllegalStateException(
                          String.format(
                              "Received index table with uuid %s as part of "
                                  + "getTableSchemaByUUID response for the main table %s, but "
                                  + "indexedTableId in its index info is %s",
                              indexInfo.getTableId(),
                              mainTableUuid,
                              indexInfo.getIndexedTableId()));
                    }
                    indexTableUuidList.add(indexInfo.getTableId().replace("-", ""));
                  });
          mainTableIndexTablesMap.put(mainTableUuid, indexTableUuidList);
        });
    log.debug("mainTableIndexTablesMap for {} is {}", mainTableUuidList, mainTableIndexTablesMap);
    return mainTableIndexTablesMap;
  }

  public static Set<String> getDroppedTableIds(
      YBClientService ybService, Universe universe, Set<String> tableIds) {
    if (tableIds.isEmpty()) {
      return Collections.emptySet();
    }
    Set<String> droppedTables = new HashSet<>();
    Set<String> allTables =
        getTableInfoList(ybService, universe).stream()
            .map(table -> table.getId().toStringUtf8())
            .collect(Collectors.toSet());
    for (String tableUuid : tableIds) {
      tableUuid = tableUuid.replace("-", "");
      if (!allTables.contains(tableUuid)) {
        droppedTables.add(tableUuid);
      }
    }
    return droppedTables;
  }

  public static Set<String> getTableIdsToAdd(
      Set<String> currentTableIds, Set<String> destinationTableIds) {
    return destinationTableIds.stream()
        .filter(tableId -> !currentTableIds.contains(tableId))
        .collect(Collectors.toSet());
  }

  public static Set<String> getTableIdsToRemove(
      Set<String> currentTableIds, Set<String> destinationTableIds) {
    return currentTableIds.stream()
        .filter(tableId -> !destinationTableIds.contains(tableId))
        .collect(Collectors.toSet());
  }

  public static Pair<Set<String>, Set<String>> getTableIdsDiff(
      Set<String> currentTableIds, Set<String> destinationTableIds) {
    return new Pair<>(
        getTableIdsToAdd(currentTableIds, destinationTableIds),
        getTableIdsToRemove(currentTableIds, destinationTableIds));
  }

  public static Set<String> convertUuidStringsToIdStringSet(Collection<String> tableUuids) {
    return tableUuids.stream()
        .map(uuidOrId -> uuidOrId.replace("-", ""))
        .collect(Collectors.toSet());
  }

  // TableInfo helpers: Convenient methods for working with
  // MasterDdlOuterClass.ListTablesResponsePB.TableInfo.
  // --------------------------------------------------------------------------------
  public static List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> getTableInfoList(
      YBClientService ybService, Universe universe) {
    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tableInfoList;
    String universeMasterAddresses = universe.getMasterAddresses();
    String universeCertificate = universe.getCertificateNodetoNode();
    try (YBClient client = ybService.getClient(universeMasterAddresses, universeCertificate)) {
      ListTablesResponse listTablesResponse =
          client.getTablesList(
              null /* nameFilter */, false /* excludeSystemTables */, null /* namespace */);
      tableInfoList = listTablesResponse.getTableInfoList();
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
    // DB treats colocated parent tables as system tables. Thus need to filter system tables on YBA
    // side.
    return tableInfoList.stream()
        .filter(tableInfo -> !TableInfoUtil.isSystemTable(tableInfo))
        .collect(Collectors.toList());
  }

  /**
   * This method returns all the tablesInfo list present in the namespace on a universe.
   *
   * @param ybService The service to get a YB client from
   * @param universe The universe to get the table schema information from
   * @param tableType The table type to filter the tables
   * @param namespaceName The namespace name to get the table schema information from
   * @return A list of {@link MasterDdlOuterClass.ListTablesResponsePB.TableInfo} containing table
   *     info of the tables in the namespace
   */
  public static List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo>
      getTableInfoListByNamespaceName(
          YBClientService ybService, Universe universe, TableType tableType, String namespaceName) {
    return getTableInfoList(ybService, universe).stream()
        .filter(tableInfo -> tableInfo.getNamespace().getName().equals(namespaceName))
        .filter(tableInfo -> tableInfo.getTableType().equals(tableType))
        .collect(Collectors.toList());
  }

  /**
   * This method returns all the tablesInfo list present in the namespace on a universe.
   *
   * @param ybService The service to get a YB client from
   * @param universe The universe to get the table schema information from
   * @param tableType The table type to filter the tables
   * @param namespaceId The namespace Id to get the table schema information from
   * @return A list of {@link MasterDdlOuterClass.ListTablesResponsePB.TableInfo} containing table
   *     info of the tables in the namespace
   */
  public static List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo>
      getTableInfoListByNamespaceId(
          YBClientService ybService, Universe universe, TableType tableType, String namespaceId) {
    return getTableInfoList(ybService, universe).stream()
        .filter(tableInfo -> tableInfo.getNamespace().getId().toStringUtf8().equals(namespaceId))
        .filter(tableInfo -> tableInfo.getTableType().equals(tableType))
        .collect(Collectors.toList());
  }

  public static List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> getTableInfoList(
      YBClientService ybService, Universe universe, Collection<String> tableIds) {
    return getTableInfoList(ybService, universe).stream()
        .filter(tableInfo -> tableIds.contains(tableInfo.getId().toStringUtf8()))
        .collect(Collectors.toList());
  }

  protected final List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> getTableInfoList(
      Universe universe) {
    return getTableInfoList(this.ybService, universe);
  }

  public static String getTableId(MasterDdlOuterClass.ListTablesResponsePB.TableInfo tableInfo) {
    return tableInfo.getId().toStringUtf8();
  }

  public static String getNamespaceId(
      MasterDdlOuterClass.ListTablesResponsePB.TableInfo tableInfo) {
    return tableInfo.getNamespace().getId().toStringUtf8();
  }

  public static Set<String> getTableIds(
      Collection<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tablesInfoList) {
    if (tablesInfoList == null) {
      return Collections.emptySet();
    }
    return tablesInfoList.stream()
        .map(XClusterConfigTaskBase::getTableId)
        .collect(Collectors.toSet());
  }

  public static Set<MasterTypes.NamespaceIdentifierPB> getNamespaces(
      Collection<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tablesInfoList) {
    if (tablesInfoList == null) {
      return Collections.emptySet();
    }
    Set<MasterTypes.NamespaceIdentifierPB> namespaces = new HashSet<>();
    Set<String> namespaceIds = new HashSet<>();
    tablesInfoList.forEach(
        tableInfo -> {
          if (!namespaceIds.contains(tableInfo.getNamespace().getId().toStringUtf8())) {
            namespaces.add(tableInfo.getNamespace());
            namespaceIds.add(tableInfo.getNamespace().getId().toStringUtf8());
          }
        });
    return namespaces;
  }

  public static Map<String, MasterDdlOuterClass.ListTablesResponsePB.TableInfo>
      getTableIdToTableInfoMap(
          Collection<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tablesInfoList) {
    return tablesInfoList.stream()
        .collect(Collectors.toMap(XClusterConfigTaskBase::getTableId, Function.identity()));
  }

  public static Map<String, List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo>>
      groupByNamespaceId(
          Collection<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tablesInfoList) {
    return tablesInfoList.stream()
        .collect(
            Collectors.groupingBy(tableInfo -> tableInfo.getNamespace().getId().toStringUtf8()));
  }

  public static Map<String, List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo>>
      groupByNamespaceName(
          Collection<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tablesInfoList) {
    return tablesInfoList.stream()
        .collect(Collectors.groupingBy(tableInfo -> tableInfo.getNamespace().getName()));
  }

  public static Map<String, List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo>>
      groupByPgSchemaName(
          Collection<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tablesInfoList) {
    if (tablesInfoList.isEmpty()) {
      return Collections.emptyMap();
    }
    // All tables in `requestedTableInfoList` have the same type.
    CommonTypes.TableType tableType = tablesInfoList.iterator().next().getTableType();
    if (!tableType.equals(TableType.PGSQL_TABLE_TYPE)) {
      throw new RuntimeException(
          String.format(
              "Only PGSQL_TABLE_TYPE tables have schema name but received tables of type %s",
              tableType));
    }
    return tablesInfoList.stream().collect(Collectors.groupingBy(TableInfo::getPgschemaName));
  }

  public static CommonTypes.TableType getTableType(
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tableInfoList) {
    CommonTypes.TableType tableType = tableInfoList.get(0).getTableType();
    // Ensure all tables have the same type.
    if (!tableInfoList.stream().allMatch(tableInfo -> tableInfo.getTableType().equals(tableType))) {
      throw new IllegalArgumentException("At least one table has a different type from others");
    }
    return tableType;
  }

  // --------------------------------------------------------------------------------
  // End of TableInfo helpers.

  public static boolean isTransactionalReplication(
      @Nullable Universe sourceUniverse, Universe targetUniverse) {
    Map<String, String> targetMasterGFlags =
        GFlagsUtil.getBaseGFlags(
            ServerType.MASTER,
            targetUniverse.getUniverseDetails().getPrimaryCluster(),
            targetUniverse.getUniverseDetails().clusters);
    String gflagValueOnTarget =
        targetMasterGFlags.get(ENABLE_REPLICATE_TRANSACTION_STATUS_TABLE_GFLAG);
    Boolean gflagValueOnTargetBoolean =
        gflagValueOnTarget != null ? Boolean.valueOf(gflagValueOnTarget) : null;

    if (sourceUniverse != null) {
      Map<String, String> sourceMasterGFlags =
          GFlagsUtil.getBaseGFlags(
              ServerType.MASTER,
              sourceUniverse.getUniverseDetails().getPrimaryCluster(),
              sourceUniverse.getUniverseDetails().clusters);

      // Replication between a universe with the gflag `enable_replicate_transaction_status_table`
      // and a universe without it is not allowed.
      String gflagValueOnSource =
          sourceMasterGFlags.get(ENABLE_REPLICATE_TRANSACTION_STATUS_TABLE_GFLAG);
      Boolean gflagValueOnSourceBoolean =
          gflagValueOnSource != null ? Boolean.valueOf(gflagValueOnSource) : null;
      if (!Objects.equals(gflagValueOnSourceBoolean, gflagValueOnTargetBoolean)) {
        throw new PlatformServiceException(
            Status.BAD_REQUEST,
            String.format(
                "The gflag %s must be set to the same value on both universes",
                ENABLE_REPLICATE_TRANSACTION_STATUS_TABLE_GFLAG));
      }
    }

    return gflagValueOnTargetBoolean == null
        ? ENABLE_REPLICATE_TRANSACTION_STATUS_TABLE_DEFAULT
        : gflagValueOnTargetBoolean;
  }

  public static boolean otherXClusterConfigsAsTargetExist(
      UUID universeUuid, UUID xClusterConfigUuid) {
    List<XClusterConfig> xClusterConfigs = XClusterConfig.getByTargetUniverseUUID(universeUuid);
    if (xClusterConfigs.isEmpty()) {
      return false;
    }
    return xClusterConfigs.size() != 1
        || !xClusterConfigs.get(0).getUuid().equals(xClusterConfigUuid);
  }

  public static void validateBackupRequestParamsForBootstrapping(
      CustomerConfigService customerConfigService,
      BackupHelper backupHelper,
      BootstrapParams.BootstrapBackupParams bootstrapBackupParams,
      UUID customerUUID) {
    CustomerConfig customerConfig =
        customerConfigService.getOrBadRequest(
            customerUUID, bootstrapBackupParams.storageConfigUUID);
    if (!customerConfig.getState().equals(CustomerConfig.ConfigState.Active)) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST, "Cannot create backup as config is queued for deletion.");
    }
    backupHelper.validateStorageConfig(customerConfig);
  }

  public static void checkConfigDoesNotAlreadyExist(
      String name, UUID sourceUniverseUUID, UUID targetUniverseUUID) {
    XClusterConfig xClusterConfig =
        XClusterConfig.getByNameSourceTarget(name, sourceUniverseUUID, targetUniverseUUID);

    if (xClusterConfig != null) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST,
          "xCluster config between source universe "
              + sourceUniverseUUID
              + " and target universe "
              + targetUniverseUUID
              + " with name '"
              + name
              + "' already exists.");
    }
  }

  /**
   * It ensures that none of the tables specified at parameter {@code tables} is in replication
   * between two universes in more than one xCluster config.
   *
   * @param tableIds The set of tables that must not be already in replication between the same
   *     universe in the same direction
   * @param sourceUniverseUUID The source universe uuid
   * @param targetUniverseUUID The target universe uuid
   */
  public static void verifyTablesNotInReplication(
      Set<String> tableIds, UUID sourceUniverseUUID, UUID targetUniverseUUID) {
    List<XClusterConfig> xClusterConfigs =
        XClusterConfig.getBetweenUniverses(sourceUniverseUUID, targetUniverseUUID);
    xClusterConfigs.forEach(
        config -> {
          Set<String> tablesInReplication = config.getTableIds();
          tablesInReplication.retainAll(tableIds);
          if (!tablesInReplication.isEmpty()) {
            throw new PlatformServiceException(
                Status.BAD_REQUEST,
                String.format(
                    "Table(s) with ID %s are already in replication between these universes in "
                        + "the same direction",
                    tablesInReplication));
          }
        });
  }

  public static List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo>
      filterTableInfoListByTableIds(
          List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tableInfoList,
          Collection<String> tableIds) {
    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> filteredTableInfoList =
        tableInfoList.stream()
            .filter(tableInfo -> tableIds.contains(getTableId(tableInfo)))
            .collect(Collectors.toList());
    // All tables are found.
    if (filteredTableInfoList.size() != tableIds.size()) {
      Set<String> foundTableIds = getTableIds(filteredTableInfoList);
      Set<String> missingTableIds =
          tableIds.stream()
              .filter(tableId -> !foundTableIds.contains(tableId))
              .collect(Collectors.toSet());
      throw new IllegalArgumentException(
          String.format(
              "Some of the tables were not found: was %d, found %d, missing tables: %s",
              tableIds.size(), filteredTableInfoList.size(), missingTableIds));
    }
    log.debug("filteredTableInfoList is {}", filteredTableInfoList);
    return filteredTableInfoList;
  }

  /**
   * Updates the replication details from the database for the given xCluster configuration. It does
   * not update the xCluster config object in the DB intentionally because the replication status is
   * a temporary status and can be fetched each time that the xCluster object is fetched.
   *
   * @param xClusterUniverseService The service for managing xCluster universes.
   * @param ybClientService The service for interacting with the YB client.
   * @param tableHandler The handler for managing universe tables.
   * @param xClusterConfig The xCluster configuration to update the replication details for.
   */
  public static void updateReplicationDetailsFromDB(
      XClusterUniverseService xClusterUniverseService,
      YBClientService ybClientService,
      UniverseTableHandler tableHandler,
      XClusterConfig xClusterConfig) {
    Optional<Universe> targetUniverseOptional =
        Objects.isNull(xClusterConfig.getTargetUniverseUUID())
            ? Optional.empty()
            : Universe.maybeGet(xClusterConfig.getTargetUniverseUUID());
    if (!targetUniverseOptional.isPresent()) {
      log.warn(
          "The target universe for the xCluster config {} is not found; ignoring gathering"
              + " replication stream statuses",
          xClusterConfig);
      return;
    }

    ReplicationClusterData replicationClusterData = null;
    try {
      replicationClusterData = collectReplicationClusterData(ybClientService, xClusterConfig);
    } catch (Exception e) {
      log.error(
          "Error getting cluster details for xCluster config {}", xClusterConfig.getUuid(), e);
      return;
    }

    if (xClusterConfig.getType() == ConfigType.Db) {
      Set<String> sourceIndexTables =
          replicationClusterData.getSourceTableInfoList().stream()
              .filter(TableInfoUtil::isIndexTable)
              .map(tableInfo -> tableInfo.getId().toStringUtf8())
              .collect(Collectors.toSet());
      if (replicationClusterData.getSourceUniverseReplicationInfo() != null) {
        Set<XClusterTableConfig> tableConfigs = new HashSet<>();
        for (NamespaceInfoPB namespaceInfo :
            replicationClusterData.getSourceUniverseReplicationInfo().getNamespaceInfos()) {
          String namespaceId = namespaceInfo.getNamespaceId();
          Optional<XClusterNamespaceConfig> namespaceConfigOptional =
              xClusterConfig.maybeGetNamespaceById(namespaceId);

          Map<String, String> tableStreamMap = namespaceInfo.getTableStreamsMap();
          for (String tableId : tableStreamMap.keySet()) {
            XClusterTableConfig tableConfig = new XClusterTableConfig(xClusterConfig, tableId);
            tableConfig.setStreamId(tableStreamMap.get(tableId));

            tableConfig.setReplicationSetupDone(true);
            tableConfig.setIndexTable(sourceIndexTables.contains(tableId));

            if (namespaceConfigOptional.isPresent()) {
              tableConfig.setStatus(
                  XClusterUtil.dbStatusToTableStatus(namespaceConfigOptional.get().getStatus()));
            }
            tableConfigs.add(tableConfig);
          }
        }
        xClusterConfig.setTables(tableConfigs);
      }
    }

    setReplicationStatus(xClusterUniverseService, xClusterConfig, targetUniverseOptional.get());

    try {
      if (Arrays.asList(XClusterConfigStatusType.Running, XClusterConfigStatusType.Updating)
          .contains(xClusterConfig.getStatus())) {
        addTransientTableConfigs(
            xClusterConfig,
            xClusterUniverseService,
            ybClientService,
            replicationClusterData.sourceTableInfoList,
            replicationClusterData.targetTableInfoList,
            replicationClusterData.clusterConfig);
      }
    } catch (Exception e) {
      log.error(
          "Error getting table details not in replication for xCluster config {}",
          xClusterConfig.getUuid(),
          e);
    }

    try {
      addSourceAndTargetTableInfo(
          xClusterConfig,
          xClusterUniverseService,
          tableHandler,
          replicationClusterData.sourceTableInfoList,
          replicationClusterData.targetTableInfoList,
          replicationClusterData.clusterConfig);
      if (xClusterConfig.getType() == XClusterConfig.ConfigType.Db) {
        addSourceAndTargetDbInfo(
            xClusterConfig,
            replicationClusterData.getSourceNamespaceInfoList(),
            replicationClusterData.getTargetNamespaceInfoList(),
            replicationClusterData.getTargetUniverseReplicationInfo());
      }
    } catch (Exception e) {
      log.error(
          "Error getting table details not in replication for xCluster config {}",
          xClusterConfig.getUuid(),
          e);
    }
  }

  /**
   * Sets the replication status for the given XClusterConfig and targetUniverse.
   *
   * @param xClusterUniverseService The service used to interact with the XClusterUniverse.
   * @param xClusterConfig The XClusterConfig containing the replication details.
   * @param targetUniverse The target Universe for which the replication status is being set.
   */
  private static void setReplicationStatus(
      XClusterUniverseService xClusterUniverseService,
      XClusterConfig xClusterConfig,
      Universe targetUniverse) {
    if (supportsGetReplicationStatus(targetUniverse)) {
      try {
        Map<String, ReplicationStatusPB> streamIdReplicationStatusMap =
            xClusterUniverseService.getReplicationStatus(xClusterConfig).stream()
                .collect(
                    Collectors.toMap(
                        status -> status.getStreamId().toStringUtf8(), Function.identity()));
        for (XClusterTableConfig tableConfig : xClusterConfig.getTableDetails()) {
          if (tableConfig.getStatus() == XClusterTableConfig.Status.Running) {
            ReplicationStatusPB replicationStatus =
                streamIdReplicationStatusMap.get(tableConfig.getStreamId());
            if (Objects.isNull(replicationStatus)) {
              tableConfig.setStatus(XClusterTableConfig.Status.UnableToFetch);
            } else {
              List<ReplicationStatusErrorPB> replicationErrors = replicationStatus.getErrorsList();
              if (!replicationErrors.isEmpty()) {
                String errorsString =
                    replicationErrors.stream()
                        .map(
                            replicationError ->
                                "ErrorCode="
                                    + replicationError.getError()
                                    + " ErrorMessage='"
                                    + replicationError.getErrorDetail()
                                    + "'")
                        .collect(Collectors.joining("\n"));
                log.error(
                    "Replication stream for the tableId {} (stream id {}) has the following "
                        + "errors {}",
                    tableConfig.getTableId(),
                    tableConfig.getStreamId(),
                    errorsString);
                tableConfig
                    .getReplicationStatusErrors()
                    .addAll(
                        replicationErrors.stream()
                            .map(
                                e ->
                                    XClusterTableConfig.ReplicationStatusError.fromErrorCode(
                                        e.getError()))
                            .filter(Objects::nonNull)
                            .collect(Collectors.toSet()));
                if (!tableConfig.getReplicationStatusErrors().isEmpty()) {
                  tableConfig.setStatus(XClusterTableConfig.Status.Error);
                }
              }
            }
          }
        }
      } catch (Exception e) {
        log.error("xClusterUniverseService.getReplicationStatus hit error : {}", e.getMessage());
        xClusterConfig.getTableDetails().stream()
            .filter(tableConfig -> tableConfig.getStatus() == XClusterTableConfig.Status.Running)
            .forEach(
                tableConfig -> tableConfig.setStatus(XClusterTableConfig.Status.UnableToFetch));
      }
    } else { // Fall back to IsBootstrapRequired API for older universes.
      Set<String> tableIdsInRunningStatus =
          xClusterConfig.getTableIdsInStatus(
              xClusterConfig.getTableIds(), XClusterTableConfig.Status.Running);
      try {
        Map<String, Boolean> isBootstrapRequiredMap =
            xClusterUniverseService.isBootstrapRequired(
                tableIdsInRunningStatus,
                xClusterConfig,
                xClusterConfig.getSourceUniverseUUID(),
                true /* ignoreErrors */);

        // If IsBootstrapRequired API returns null, set the statuses to UnableToFetch.
        if (Objects.isNull(isBootstrapRequiredMap)) {
          xClusterConfig.getTableDetails().stream()
              .filter(tableConfig -> tableIdsInRunningStatus.contains(tableConfig.getTableId()))
              .forEach(
                  tableConfig -> tableConfig.setStatus(XClusterTableConfig.Status.UnableToFetch));
        } else {
          Set<String> tableIdsInErrorStatus =
              isBootstrapRequiredMap.entrySet().stream()
                  .filter(Entry::getValue)
                  .map(Map.Entry::getKey)
                  .collect(Collectors.toSet());
          xClusterConfig.getTableDetails().stream()
              .filter(tableConfig -> tableIdsInErrorStatus.contains(tableConfig.getTableId()))
              .forEach(tableConfig -> tableConfig.setStatus(XClusterTableConfig.Status.Error));

          // Set the status for the rest of tables where isBootstrapRequired RPC failed.
          xClusterConfig.getTableDetails().stream()
              .filter(
                  tableConfig ->
                      tableIdsInRunningStatus.contains(tableConfig.getTableId())
                          && !isBootstrapRequiredMap.containsKey(tableConfig.getTableId()))
              .forEach(
                  tableConfig -> tableConfig.setStatus(XClusterTableConfig.Status.UnableToFetch));
        }
      } catch (Exception e) {
        log.error("XClusterConfigTaskBase.isBootstrapRequired hit error : {}", e.getMessage());
      }
    }
  }

  private static void addSourceAndTargetDbInfo(
      XClusterConfig xClusterConfig,
      Set<MasterTypes.NamespaceIdentifierPB> sourceNamespaceInfoList,
      Set<MasterTypes.NamespaceIdentifierPB> targetNamespaceInfoList,
      GetUniverseReplicationInfoResponse targetUniverseReplicationInfo) {
    if (targetUniverseReplicationInfo == null) {
      return;
    }

    Map<String, String> sourceToTargetNamespaceId =
        targetUniverseReplicationInfo.getDbScopedInfos().stream()
            .collect(
                Collectors.toMap(
                    DbScopedInfoPB::getSourceNamespaceId, DbScopedInfoPB::getTargetNamespaceId));

    if (CollectionUtils.isNotEmpty(sourceNamespaceInfoList)) {
      Map<String, NamespaceInfoResp> sourceDbIdToDbInfo =
          sourceNamespaceInfoList.stream()
              .map(NamespaceInfoResp::createFromNamespaceIdentifier)
              .collect(Collectors.toMap(NamespaceInfoResp::getNamespaceId, Function.identity()));
      xClusterConfig
          .getNamespaceDetails()
          .forEach(
              namespaceConfig -> {
                namespaceConfig.setSourceNamespaceInfo(
                    sourceDbIdToDbInfo.getOrDefault(namespaceConfig.getSourceNamespaceId(), null));
              });
    }

    if (CollectionUtils.isNotEmpty(targetNamespaceInfoList)) {
      Map<String, NamespaceInfoResp> targetDbIdToDbInfo =
          targetNamespaceInfoList.stream()
              .map(NamespaceInfoResp::createFromNamespaceIdentifier)
              .collect(Collectors.toMap(NamespaceInfoResp::getNamespaceId, Function.identity()));
      xClusterConfig
          .getNamespaceDetails()
          .forEach(
              namespaceConfig -> {
                String targetNamespaceId =
                    sourceToTargetNamespaceId.getOrDefault(
                        namespaceConfig.getSourceNamespaceId(), null);
                if (StringUtils.isNotEmpty(targetNamespaceId)) {
                  namespaceConfig.setTargetNamespaceInfo(
                      targetDbIdToDbInfo.getOrDefault(targetNamespaceId, null));
                }
              });
    }
  }

  /**
   * Collects replication cluster data for the given XClusterConfig.
   *
   * @param ybClientService The YBClientService instance.
   * @param xClusterConfig The XClusterConfig instance.
   * @return The ReplicationClusterData containing the collected data.
   */
  private static ReplicationClusterData collectReplicationClusterData(
      YBClientService ybClientService, XClusterConfig xClusterConfig) {
    Universe sourceUniverse = Universe.getOrBadRequest(xClusterConfig.getSourceUniverseUUID());
    Universe targetUniverse = Universe.getOrBadRequest(xClusterConfig.getTargetUniverseUUID());

    ReplicationClusterData data = new ReplicationClusterData();
    CompletableFuture<Void> sourceUniverseTableInfoFuture =
        CompletableFuture.runAsync(
            () -> {
              try {
                data.setSourceTableInfoList(getTableInfoList(ybClientService, sourceUniverse));
              } catch (Exception e) {
                log.error(
                    "Error getting table info list for source universe {}", sourceUniverse, e);
              }
            });

    CompletableFuture<Void> targetUniverseTableInfoFuture =
        CompletableFuture.runAsync(
            () -> {
              try {
                data.setTargetTableInfoList(getTableInfoList(ybClientService, targetUniverse));
              } catch (Exception e) {
                log.error(
                    "Error getting table info list for target universe {}", targetUniverse, e);
              }
            });

    CompletableFuture<Void> targetUniverseClusterConfigFuture =
        CompletableFuture.runAsync(
            () -> {
              try (YBClient client =
                  ybClientService.getClient(
                      targetUniverse.getMasterAddresses(),
                      targetUniverse.getCertificateNodetoNode())) {
                CatalogEntityInfo.SysClusterConfigEntryPB config =
                    getClusterConfig(client, targetUniverse.getUniverseUUID());
                data.setClusterConfig(config);
              } catch (Exception e) {
                log.error(
                    "Error getting table ids to skip bidirectional replication for xCluster config"
                        + " {}",
                    xClusterConfig.getUuid(),
                    e);
              }
            });

    CompletableFuture<Void> sourceUniverseNamespaceInfoFuture =
        CompletableFuture.runAsync(
            () -> {
              if (xClusterConfig.getType() == XClusterConfig.ConfigType.Db) {
                try {
                  data.setSourceNamespaceInfoList(
                      getNamespaces(ybClientService, sourceUniverse, null));
                } catch (Exception e) {
                  log.error("Error getting db info list for source universe {}", sourceUniverse, e);
                }
              }
            });
    CompletableFuture<Void> targetUniverseNamespaceInfoFuture =
        CompletableFuture.runAsync(
            () -> {
              if (xClusterConfig.getType() == XClusterConfig.ConfigType.Db) {
                try {
                  data.setTargetNamespaceInfoList(
                      getNamespaces(ybClientService, targetUniverse, null));
                } catch (Exception e) {
                  log.error("Error getting db info list for target universe {}", targetUniverse, e);
                }
              }
            });

    CompletableFuture<Void> targetUniverseReplicationInfoFuture =
        CompletableFuture.runAsync(
            () -> {
              if (xClusterConfig.getType() == XClusterConfig.ConfigType.Db) {
                try {
                  data.setTargetUniverseReplicationInfo(
                      getUniverseReplicationInfo(
                          ybClientService,
                          targetUniverse,
                          xClusterConfig.getReplicationGroupName()));
                } catch (Exception e) {
                  log.error(
                      "Error getting universe replication info for target universe {}",
                      targetUniverse,
                      e);
                }
              }
            });

    CompletableFuture<Void> sourceUniverseReplicationInfoFuture =
        CompletableFuture.runAsync(
            () -> {
              if (xClusterConfig.getType() == XClusterConfig.ConfigType.Db) {
                try {
                  data.setSourceUniverseReplicationInfo(
                      getXClusterOutboundReplicationGroupInfo(
                          ybClientService,
                          sourceUniverse,
                          xClusterConfig.getReplicationGroupName()));
                } catch (Exception e) {
                  log.error(
                      "Error getting universe replication info for source universe {}",
                      sourceUniverse,
                      e);
                }
              }
            });

    CompletableFuture.allOf(
            sourceUniverseTableInfoFuture,
            targetUniverseTableInfoFuture,
            targetUniverseClusterConfigFuture,
            sourceUniverseNamespaceInfoFuture,
            targetUniverseNamespaceInfoFuture,
            sourceUniverseReplicationInfoFuture,
            targetUniverseReplicationInfoFuture)
        .join();
    return data;
  }

  /**
   * Adds transient table configurations to the XClusterConfig that are not part of the replication
   * but should be.
   *
   * @param xClusterConfig The XClusterConfig object to add the table configurations to.
   * @param xClusterUniverseService The XClusterUniverseService used to retrieve table
   *     configurations.
   * @param ybClientService The YBClientService used to interact with the YBClient.
   * @param sourceTableInfoList The list of source table information.
   * @param targetTableInfoList The list of target table information.
   * @param clusterConfig The cluster configuration.
   * @throws Exception if an error occurs while adding the table configurations.
   */
  private static void addTransientTableConfigs(
      XClusterConfig xClusterConfig,
      XClusterUniverseService xClusterUniverseService,
      YBClientService ybClientService,
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> sourceTableInfoList,
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> targetTableInfoList,
      CatalogEntityInfo.SysClusterConfigEntryPB clusterConfig)
      throws Exception {
    Set<String> sourceUniverseTableIds =
        sourceTableInfoList.stream()
            .map(tableInfo -> XClusterConfigTaskBase.getTableId(tableInfo))
            .collect(Collectors.toSet());

    // Update the status for tables that were previously being replicated but have been dropped from
    // the source.
    xClusterConfig.getTableDetails().stream()
        .filter(tableConfig -> tableConfig.getStatus() == XClusterTableConfig.Status.Running)
        .forEach(
            tableConfig -> {
              if (!sourceUniverseTableIds.contains(tableConfig.getTableId())) {
                tableConfig.setStatus(XClusterTableConfig.Status.DroppedFromSource);
              }
            });

    Map<String, String> sourceTableIdToTargetTableIdMap =
        getSourceTableIdTargetTableIdMap(sourceTableInfoList, targetTableInfoList);

    // Update the status for tables that were previously being replicated but have been dropped from
    // the target.
    xClusterConfig.getTableDetails().stream()
        .filter(
            tableConfig ->
                Arrays.asList(
                        XClusterTableConfig.Status.Running,
                        XClusterTableConfig.Status.UnableToFetch)
                    .contains(tableConfig.getStatus()))
        .forEach(
            tableConfig -> {
              String targetTableId = sourceTableIdToTargetTableIdMap.get(tableConfig.getTableId());
              if (targetTableId == null) {
                tableConfig.setStatus(XClusterTableConfig.Status.DroppedFromTarget);
              }
            });

    Pair<List<XClusterTableConfig>, List<XClusterTableConfig>> tableConfigs =
        getXClusterTableConfigNotInReplication(
            xClusterUniverseService,
            ybClientService,
            xClusterConfig,
            clusterConfig,
            sourceTableInfoList,
            targetTableInfoList);

    tableConfigs
        .getFirst()
        .forEach(
            tableConfig -> {
              Optional<XClusterTableConfig> existingTableConfig =
                  xClusterConfig.getTableDetails().stream()
                      .filter(t -> t.getTableId().equals(tableConfig.getTableId()))
                      .findFirst();
              if (!existingTableConfig.isPresent()) {
                xClusterConfig.addTableConfig(tableConfig);
              } else {
                log.info(
                    "Found table {} with status {} on source universe but already exists in"
                        + " xCluster config in YBA",
                    tableConfig.getTableId(),
                    tableConfig.getStatus());
              }
            });

    if (tableConfigs.getSecond().size() > 0) {
      Map<String, String> targetTableIdToSourceTableIdMap =
          getTargetTableIdToSourceTableIdMap(sourceTableInfoList, targetTableInfoList);

      tableConfigs
          .getSecond()
          .forEach(
              tableConfig -> {
                String targetTableId = tableConfig.getTableId();
                String sourceTableId = targetTableIdToSourceTableIdMap.get(targetTableId);
                if (sourceTableId != null) {
                  Optional<XClusterTableConfig> existingTableConfig =
                      xClusterConfig.getTableDetails().stream()
                          .filter(t -> t.getTableId().equals(sourceTableId))
                          .findFirst();
                  if (!existingTableConfig.isPresent()
                      || existingTableConfig
                          .get()
                          .getStatus()
                          .equals(XClusterTableConfig.Status.ExtraTableOnSource)) {
                    xClusterConfig.addTableConfig(tableConfig);
                  } else {
                    log.info(
                        "Found table target: {} source: {} with status {} on target universe but"
                            + " already exists in xCluster config in YBA",
                        targetTableId,
                        sourceTableId,
                        tableConfig.getStatus());
                  }
                } else {
                  xClusterConfig.addTableConfig(tableConfig);
                }
              });
    }
  }

  /**
   * Adds the source and target table information to the XClusterConfig object.
   *
   * @param xClusterConfig The XClusterConfig object to update.
   * @param xClusterUniverseService The XClusterUniverseService object for retrieving table
   *     mappings.
   * @param tableHandler The UniverseTableHandler object for retrieving table information.
   * @param sourceTableInfoList The list of source table information.
   * @param targetTableInfoList The list of target table information.
   * @param config The SysClusterConfigEntryPB object for cluster configuration.
   */
  private static void addSourceAndTargetTableInfo(
      XClusterConfig xClusterConfig,
      XClusterUniverseService xClusterUniverseService,
      UniverseTableHandler tableHandler,
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> sourceTableInfoList,
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> targetTableInfoList,
      CatalogEntityInfo.SysClusterConfigEntryPB config) {
    Universe sourceUniverse = Universe.getOrBadRequest(xClusterConfig.getSourceUniverseUUID());

    List<TableInfoResp> sourceUniverseTableInfoRespList =
        tableHandler.getTableInfoRespFromTableInfo(
            sourceUniverse,
            sourceTableInfoList,
            false /* includeParentTableInfo */,
            false /* excludeColocatedTables */,
            true /* includeColocatedParentTables */,
            true /* xClusterSupportedOnly */);
    Map<String, TableInfoResp> sourceTableIdTableInfoRespMap =
        sourceUniverseTableInfoRespList.stream()
            .collect(Collectors.toMap(TableInfoResp::getTableId, Function.identity()));

    // Update tableInfo from source universe
    xClusterConfig
        .getTableDetails()
        .forEach(
            tableConfig -> {
              if (tableConfig.getStatus().equals(XClusterTableConfig.Status.ExtraTableOnTarget)) {
                return;
              }
              tableConfig.setSourceTableInfo(
                  sourceTableIdTableInfoRespMap.get(tableConfig.getTableId()));
            });

    Universe targetUniverse = Universe.getOrBadRequest(xClusterConfig.getTargetUniverseUUID());
    Map<String, String> producerConsumerTableIdMap =
        xClusterUniverseService.getSourceTableIdToTargetTableIdMapFromClusterConfig(
            targetUniverse, xClusterConfig.getReplicationGroupName(), config);

    List<TableInfoResp> targetUniverseTableInfoRespList =
        tableHandler.getTableInfoRespFromTableInfo(
            targetUniverse,
            targetTableInfoList,
            false /* includeParentTableInfo */,
            false /* excludeColocatedTables */,
            true /* includeColocatedParentTables */,
            true /* xClusterSupportedOnly */);
    Map<String, TableInfoResp> targetTableIdTableInfoRespMap =
        targetUniverseTableInfoRespList.stream()
            .collect(Collectors.toMap(TableInfoResp::getTableId, Function.identity()));

    // Update tableInfo from target universe.
    xClusterConfig
        .getTableDetails()
        .forEach(
            tableConfig -> {
              if (tableConfig.getStatus().equals(XClusterTableConfig.Status.ExtraTableOnSource)) {
                return;
              }
              String consumerTableId = producerConsumerTableIdMap.get(tableConfig.getTableId());
              if (consumerTableId != null) {
                tableConfig.setTargetTableInfo(targetTableIdTableInfoRespMap.get(consumerTableId));
              } else {
                tableConfig.setTargetTableInfo(
                    targetTableIdTableInfoRespMap.get(tableConfig.getTableId()));
              }
            });
  }

  /**
   * Retrieves the list of source and target table configurations that are not present in
   * replication for the given XClusterConfig.
   *
   * @param xClusterUniverseService The service for managing XClusterUniverse.
   * @param ybClientService The service for interacting with the YBClient.
   * @param xClusterConfig The XClusterConfig for which to retrieve the table configurations.
   * @param clusterConfig The SysClusterConfigEntryPB for the cluster.
   * @param sourceTableInfoList The list of source table information.
   * @param targetTableInfoList The list of target table information.
   * @return A Pair containing the list of source table configurations and the list of target table
   *     configurations.
   */
  public static Pair<List<XClusterTableConfig>, List<XClusterTableConfig>>
      getXClusterTableConfigNotInReplication(
          XClusterUniverseService xClusterUniverseService,
          YBClientService ybClientService,
          XClusterConfig xClusterConfig,
          CatalogEntityInfo.SysClusterConfigEntryPB clusterConfig,
          List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> sourceTableInfoList,
          List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> targetTableInfoList) {
    List<XClusterTableConfig> targetTableConfigs = new ArrayList<>();
    List<XClusterTableConfig> sourceTableConfigs = new ArrayList<>();

    targetTableConfigs =
        getTargetOnlyTable(
            xClusterUniverseService,
            ybClientService,
            xClusterConfig,
            clusterConfig,
            targetTableInfoList);
    sourceTableConfigs =
        getSourceOnlyTable(
            xClusterUniverseService,
            ybClientService,
            xClusterConfig,
            clusterConfig,
            sourceTableInfoList);

    return new Pair<>(sourceTableConfigs, targetTableConfigs);
  }

  public static List<XClusterTableConfig> getTargetOnlyTable(
      XClusterUniverseService xClusterUniverseService,
      YBClientService ybClientService,
      XClusterConfig xClusterConfig,
      CatalogEntityInfo.SysClusterConfigEntryPB clusterConfig,
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> targetTableInfoList) {
    try {
      Set<String> tableIdsInReplicationOnTargetUniverse =
          getConsumerTableIdsFromClusterConfig(
              clusterConfig, xClusterConfig.getReplicationGroupName());

      return extractTablesNotInReplication(
          xClusterConfig,
          xClusterConfig.getTargetUniverseUUID(),
          ybClientService,
          tableIdsInReplicationOnTargetUniverse,
          targetTableInfoList,
          XClusterTableConfig.Status.ExtraTableOnTarget);
    } catch (Exception e) {
      log.error(
          "Error getting target only table for xCluster config {}", xClusterConfig.getUuid(), e);
      return new ArrayList<>();
    }
  }

  public static List<XClusterTableConfig> getSourceOnlyTable(
      XClusterUniverseService xClusterUniverseService,
      YBClientService ybClientService,
      XClusterConfig xClusterConfig,
      CatalogEntityInfo.SysClusterConfigEntryPB clusterConfig,
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> sourceTableInfoList) {

    try {
      Set<String> tableIdsInReplicationOnSourceUniverse =
          getProducerTableIdsFromClusterConfig(
              clusterConfig, xClusterConfig.getReplicationGroupName());

      return extractTablesNotInReplication(
          xClusterConfig,
          xClusterConfig.getSourceUniverseUUID(),
          ybClientService,
          tableIdsInReplicationOnSourceUniverse,
          sourceTableInfoList,
          XClusterTableConfig.Status.ExtraTableOnSource);
    } catch (Exception e) {
      log.error(
          "Error getting source only table for xCluster config {}", xClusterConfig.getUuid(), e);
      return new ArrayList<>();
    }
  }

  public static List<XClusterTableConfig> extractTablesNotInReplication(
      XClusterConfig xClusterConfig,
      UUID universeUUID,
      YBClientService ybClientService,
      Set<String> tablesIdsInReplication,
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> allTables,
      XClusterTableConfig.Status missingTableStatus) {

    Set<String> tableIdsNotInReplication = new HashSet<>();

    if (xClusterConfig.getTableType().equals(XClusterConfig.TableType.YSQL)) {
      // Gather all namespaceIds for tables that are in replication.
      Set<String> namespaceIdsInReplication =
          allTables.stream()
              .filter(tableInfo -> tablesIdsInReplication.contains(getTableId(tableInfo)))
              .map(tableInfo -> tableInfo.getNamespace().getId().toStringUtf8())
              .collect(Collectors.toSet());

      // All the tables that is xCluster supported and belong to the namespace that is in
      // replication but not in the tablesIdsInReplication are the tables that are not in
      // replication.
      tableIdsNotInReplication =
          allTables.stream()
              .filter(tableInfo -> namespaceIdsInReplication.contains(getNamespaceId(tableInfo)))
              .filter(tableInfo -> isXClusterSupported(tableInfo))
              .filter(tableInfo -> !tablesIdsInReplication.contains(getTableId(tableInfo)))
              .map(tableInfo -> getTableId(tableInfo))
              .collect(Collectors.toSet());
    } else if (xClusterConfig.getTableType().equals(XClusterConfig.TableType.YCQL)) {
      Universe universe = Universe.getOrBadRequest(universeUUID);
      // On old universes, tableInfo does not contain the field indexedTableId, so we need to gather
      // index table ids using tableSchemas.
      if (universeTableInfoContainsIndexedTableId(
          universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion)) {
        // Gather all index table ids not in replication by parsing each tables and checking if it's
        // indexedTableId is in replication.
        tableIdsNotInReplication =
            allTables.stream()
                .filter(
                    tableInfo ->
                        tableInfo.getRelationType().equals(RelationType.INDEX_TABLE_RELATION))
                .filter(tableInfo -> !tablesIdsInReplication.contains(getTableId(tableInfo)))
                .filter(
                    tableInfo ->
                        tableInfo.getIndexedTableId() != null
                            && tablesIdsInReplication.contains(tableInfo.getIndexedTableId()))
                .map(tableInfo -> getTableId(tableInfo))
                .collect(Collectors.toSet());

        // Extract main table ids for tables that are not in replication but their index tables are
        // in replication.
        Set<String> mainTableIdsNotInReplicationWithIndexInReplication =
            allTables.stream()
                .filter(tableInfo -> TableInfoUtil.isIndexTable(tableInfo))
                .filter(tableInfo -> tablesIdsInReplication.contains(getTableId(tableInfo)))
                .filter(
                    tableInfo -> !tablesIdsInReplication.contains(tableInfo.getIndexedTableId()))
                .map(tableInfo -> tableInfo.getIndexedTableId())
                .collect(Collectors.toSet());

        if (mainTableIdsNotInReplicationWithIndexInReplication.size() > 0) {
          List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo>
              mainTableIdsNotInReplicationWithIndexInReplicationInfo =
                  allTables.stream()
                      .filter(
                          tableInfo ->
                              mainTableIdsNotInReplicationWithIndexInReplication.contains(
                                  getTableId(tableInfo)))
                      .collect(Collectors.toList());

          Map<String, List<String>> mainTableIdToIndexTableIdsNotInReplicationMap =
              getMainTableIndexTablesMap(
                  universe, mainTableIdsNotInReplicationWithIndexInReplicationInfo);

          // Add main table ids that are not in replication and its indexes which are not
          // in replication.
          for (Map.Entry<String, List<String>> entry :
              mainTableIdToIndexTableIdsNotInReplicationMap.entrySet()) {
            boolean indexTableInReplication =
                entry.getValue().stream()
                    .anyMatch(indexTableId -> tablesIdsInReplication.contains(indexTableId));
            if (indexTableInReplication) {
              tableIdsNotInReplication.add(entry.getKey());
              for (String indexTableId : entry.getValue()) {
                if (!tablesIdsInReplication.contains(indexTableId)) {
                  tableIdsNotInReplication.add(indexTableId);
                }
              }
            }
          }
        }

      } else {
        // Gather all main table ids for tables that are in replication.
        Set<String> mainTableIdsInReplication =
            allTables.stream()
                .filter(tableInfo -> !TableInfoUtil.isIndexTable(tableInfo))
                .filter(tableInfo -> tablesIdsInReplication.contains(getTableId(tableInfo)))
                .map(tableInfo -> getTableId(tableInfo))
                .collect(Collectors.toSet());

        // Index table ids which has its main table in replication.
        Set<String> indexTableIdsWithMainTableIdsInReplication = new HashSet<>();

        // Gather all index table ids for tables that are in replication and add index table
        // ids that are not in replication.
        Map<String, List<String>> mainTableIdToIndexTableIdsInReplicationMap =
            getMainTableIndexTablesMap(ybClientService, universe, mainTableIdsInReplication);
        for (List<String> indexTableIdsList : mainTableIdToIndexTableIdsInReplicationMap.values()) {
          for (String indexTableId : indexTableIdsList) {
            if (!tablesIdsInReplication.contains(indexTableId)) {
              tableIdsNotInReplication.add(indexTableId);
            }
            indexTableIdsWithMainTableIdsInReplication.add(indexTableId);
          }
        }

        // Index tables that are in replication but their main tables are not in replication.
        Set<String> indexTableIdsInReplicationWithMissingMainTable =
            tablesIdsInReplication.stream()
                .filter(tableId -> !indexTableIdsWithMainTableIdsInReplication.contains(tableId))
                .collect(Collectors.toSet());
        if (indexTableIdsInReplicationWithMissingMainTable.size() > 0) {
          // All main tables in a universe which are not in replication.
          Set<String> mainTableIdsNotInReplication =
              allTables.stream()
                  .filter(tableInfo -> !TableInfoUtil.isIndexTable(tableInfo))
                  .filter(tableInfo -> !tablesIdsInReplication.contains(getTableId(tableInfo)))
                  .map(tableInfo -> getTableId(tableInfo))
                  .collect(Collectors.toSet());
          Map<String, List<String>> mainTableIdToIndexTableIdsNotInReplicationMap =
              getMainTableIndexTablesMap(ybClientService, universe, mainTableIdsNotInReplication);
          // Search through the indexes of each main table not in replication and if found,
          // add the main table and index table to the list of tables not in replication
          // if they are not already in replication.
          for (Map.Entry<String, List<String>> entry :
              mainTableIdToIndexTableIdsNotInReplicationMap.entrySet()) {
            boolean indexTableInReplication =
                entry.getValue().stream()
                    .anyMatch(indexTableId -> tablesIdsInReplication.contains(indexTableId));
            if (indexTableInReplication) {
              tableIdsNotInReplication.add(entry.getKey());
              for (String indexTableId : entry.getValue()) {
                if (!tablesIdsInReplication.contains(indexTableId)) {
                  tableIdsNotInReplication.add(indexTableId);
                }
              }
            }
          }
        }
      }
    } else {
      throw new IllegalArgumentException(
          "Unsupported table type " + xClusterConfig.getTableType() + " for xCluster config");
    }

    List<XClusterTableConfig> tableConfigNotInReplication = new ArrayList<>();
    for (String tableId : tableIdsNotInReplication) {
      XClusterTableConfig tableConfig = new XClusterTableConfig();
      tableConfig.setTableId(tableId);
      tableConfig.setStatus(missingTableStatus);
      tableConfigNotInReplication.add(tableConfig);
    }
    return tableConfigNotInReplication;
  }

  public static Set<MasterTypes.NamespaceIdentifierPB> getNamespaces(
      YBClientService ybService, Universe universe, Set<String> dbIds) {
    try (YBClient client =
        ybService.getClient(universe.getMasterAddresses(), universe.getCertificateNodetoNode())) {
      List<MasterTypes.NamespaceIdentifierPB> namespaces =
          client.getNamespacesList().getNamespacesList();
      if (CollectionUtils.isEmpty(dbIds)) {
        return new HashSet<MasterTypes.NamespaceIdentifierPB>(namespaces);
      }
      return namespaces.stream()
          .filter(db -> dbIds.contains(db.getId().toStringUtf8()))
          .collect(Collectors.toSet());
    } catch (Exception e) {
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
    }
  }

  public static List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> filterIndexTableInfoList(
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tableInfoList) {
    return tableInfoList.stream()
        .filter(tableInfo -> TableInfoUtil.isIndexTable(tableInfo))
        .collect(Collectors.toList());
  }

  public static Map<String, String> getIndexTableIdToParentTableIdMap(
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tableInfoList) {
    return tableInfoList.stream()
        .filter(TableInfoUtil::isIndexTable)
        .collect(
            Collectors.toMap(
                tableInfo -> getTableId(tableInfo), tableInfo -> tableInfo.getIndexedTableId()));
  }

  public static List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> getRequestedTableInfoList(
      Set<String> dbIds,
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> sourceTableInfoList) {
    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> requestedTableInfoList =
        sourceTableInfoList.stream()
            .filter(
                tableInfo ->
                    isXClusterSupported(tableInfo)
                        && dbIds.contains(tableInfo.getNamespace().getId().toStringUtf8()))
            .collect(Collectors.toList());
    Set<String> foundDbIds =
        requestedTableInfoList.stream()
            .map(tableInfo -> tableInfo.getNamespace().getId().toStringUtf8())
            .collect(Collectors.toSet());
    // Ensure all DB names are found.
    if (foundDbIds.size() != dbIds.size()) {
      Set<String> missingDbIds =
          dbIds.stream().filter(dbId -> !foundDbIds.contains(dbId)).collect(Collectors.toSet());
      throw new IllegalArgumentException(
          String.format(
              "Some of the DB ids were not found: was %d, found %d, missing dbs: %s",
              dbIds.size(), foundDbIds.size(), missingDbIds));
    }
    return requestedTableInfoList;
  }

  // DR methods.
  // --------------------------------------------------------------------------------
  protected DrConfig getDrConfigFromTaskParams() {
    XClusterConfigTaskParams params = taskParams();
    if (params instanceof DrConfigTaskParams) {
      DrConfigTaskParams drConfigTaskParams = (DrConfigTaskParams) params;
      DrConfig drConfig = drConfigTaskParams.getDrConfig();
      if (drConfig == null) {
        throw new RuntimeException("drConfig in task params is null");
      }
      return drConfig;
    }
    throw new IllegalArgumentException(
        "taskParams() is not an instance of DrConfigTaskParams and thus cannot access "
            + "its drConfig");
  }

  protected SubTaskGroup createWaitForReplicationDrainTask(XClusterConfig xClusterConfig) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("WaitForReplicationDrain");
    WaitForReplicationDrain.Params WaitForReplicationDrainParams =
        new WaitForReplicationDrain.Params();
    WaitForReplicationDrainParams.setUniverseUUID(xClusterConfig.getSourceUniverseUUID());
    WaitForReplicationDrainParams.xClusterConfig = xClusterConfig;
    WaitForReplicationDrain task = createTask(WaitForReplicationDrain.class);
    task.initialize(WaitForReplicationDrainParams);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public static GetUniverseReplicationInfoResponse getUniverseReplicationInfo(
      YBClientService ybService, Universe universe, String replicationGroup) throws Exception {
    String universeMasterAddresses = universe.getMasterAddresses();
    String universeCertificate = universe.getCertificateNodetoNode();

    try (YBClient client = ybService.getClient(universeMasterAddresses, universeCertificate)) {
      return client.getUniverseReplicationInfo(replicationGroup);
    }
  }

  public static GetXClusterOutboundReplicationGroupInfoResponse
      getXClusterOutboundReplicationGroupInfo(
          YBClientService ybService, Universe universe, String replicationGroup) throws Exception {
    String universeMasterAddresses = universe.getMasterAddresses();
    String universeCertificate = universe.getCertificateNodetoNode();

    try (YBClient client = ybService.getClient(universeMasterAddresses, universeCertificate)) {
      return client.getXClusterOutboundReplicationGroupInfo(replicationGroup);
    }
  }

  /** Represents the data required for replication between clusters. */
  @Data
  public static class ReplicationClusterData {
    private List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> sourceTableInfoList;
    private List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> targetTableInfoList;
    private CatalogEntityInfo.SysClusterConfigEntryPB clusterConfig;
    private Set<MasterTypes.NamespaceIdentifierPB> sourceNamespaceInfoList;
    private Set<MasterTypes.NamespaceIdentifierPB> targetNamespaceInfoList;
    private GetUniverseReplicationInfoResponse targetUniverseReplicationInfo;
    private GetXClusterOutboundReplicationGroupInfoResponse sourceUniverseReplicationInfo;
  }

  // --------------------------------------------------------------------------------
  // End of DR methods.

  // DB Scoped replication methods.
  // --------------------------------------------------------------------------------

  /**
   * Checkpoints the databases on the source universe and verifies the checkpointing is completed.
   *
   * @param xClusterConfig config used
   * @param sourceDbIds db ids on the source universe to checkpoint.
   * @return The created subtask group
   */
  protected SubTaskGroup createCreateOutboundReplicationGroupTask(
      XClusterConfig xClusterConfig, Set<String> sourceDbIds) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("CreateOutboundReplicationGroup");
    XClusterConfigTaskParams xClusterConfigParams = new XClusterConfigTaskParams();
    xClusterConfigParams.setUniverseUUID(xClusterConfig.getSourceUniverseUUID());
    xClusterConfigParams.xClusterConfig = xClusterConfig;
    xClusterConfigParams.dbs = sourceDbIds;

    CreateOutboundReplicationGroup task = createTask(CreateOutboundReplicationGroup.class);
    task.initialize(xClusterConfigParams);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Set up db scoped xcluster replication.
   *
   * @param xClusterConfig config used
   * @return The created subtask group
   */
  protected SubTaskGroup createXClusterDbReplicationSetupTask(XClusterConfig xClusterConfig) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("XClusterDbReplicationSetup");
    XClusterConfigTaskParams xClusterConfigParams = new XClusterConfigTaskParams();
    xClusterConfigParams.xClusterConfig = xClusterConfig;

    XClusterDbReplicationSetup task = createTask(XClusterDbReplicationSetup.class);
    task.initialize(xClusterConfigParams);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Checkpoints the databases to be added on the source universe and verifies the checkpointing is
   * completed.
   *
   * @param xClusterConfig config used
   * @param dbId db ids on the source universe that are being added to checkpoint.
   * @return The created subtask group
   */
  protected SubTaskGroup createXClusterAddNamespaceToOutboundReplicationGroupTask(
      XClusterConfig xClusterConfig, String dbId) {
    SubTaskGroup subTaskGroup =
        createSubTaskGroup("XClusterAddNamespaceToOutboundReplicationGroup");
    XClusterAddNamespaceToOutboundReplicationGroup.Params taskParams =
        new XClusterAddNamespaceToOutboundReplicationGroup.Params();
    taskParams.setUniverseUUID(xClusterConfig.getSourceUniverseUUID());
    taskParams.xClusterConfig = xClusterConfig;
    taskParams.dbToAdd = dbId;
    XClusterAddNamespaceToOutboundReplicationGroup task =
        createTask(XClusterAddNamespaceToOutboundReplicationGroup.class);
    task.initialize(taskParams);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Removes namespace from replication on the source universe only.
   *
   * @param xClusterConfig config used
   * @param dbId db id on the source universe that are being added to checkpoint.
   * @return The created subtask group
   */
  protected SubTaskGroup createXClusterRemoveNamespaceFromOutboundReplicationGroupTask(
      XClusterConfig xClusterConfig, String dbId) {
    SubTaskGroup subTaskGroup =
        createSubTaskGroup("XClusterRemoveNamespaceFromOutboundReplication");
    XClusterRemoveNamespaceFromOutboundReplicationGroup.Params taskParams =
        new XClusterRemoveNamespaceFromOutboundReplicationGroup.Params();
    taskParams.xClusterConfig = xClusterConfig;
    taskParams.dbToRemove = dbId;
    XClusterRemoveNamespaceFromOutboundReplicationGroup task =
        createTask(XClusterRemoveNamespaceFromOutboundReplicationGroup.class);
    task.initialize(taskParams);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Removes namespace from replication on the target universe only.
   *
   * @param xClusterConfig config used
   * @param dbId db id on the source universe that is being removed from the replication.
   * @return The created subtask group
   */
  protected SubTaskGroup createXClusterRemoveNamespaceFromTargetUniverseTask(
      XClusterConfig xClusterConfig, String dbId) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("XClusterRemoveNamespaceFromTargetUniverse");
    XClusterRemoveNamespaceFromTargetUniverse.Params taskParams =
        new XClusterRemoveNamespaceFromTargetUniverse.Params();
    taskParams.xClusterConfig = xClusterConfig;
    taskParams.dbToRemove = dbId;
    XClusterRemoveNamespaceFromTargetUniverse task =
        createTask(XClusterRemoveNamespaceFromTargetUniverse.class);
    task.initialize(taskParams);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Add the databases to the replication.
   *
   * @param xClusterConfig config used
   * @param dbIds db ids on the source universe that are being added to the replication.
   * @return The created subtask group
   */
  protected SubTaskGroup createAddNamespaceToXClusterReplicationTask(
      XClusterConfig xClusterConfig, String dbId) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("AddNamespaceToXClusterReplication");
    AddNamespaceToXClusterReplication.Params taskParams =
        new AddNamespaceToXClusterReplication.Params();
    taskParams.setUniverseUUID(xClusterConfig.getSourceUniverseUUID());
    taskParams.xClusterConfig = xClusterConfig;
    taskParams.dbToAdd = dbId;
    AddNamespaceToXClusterReplication task = createTask(AddNamespaceToXClusterReplication.class);
    task.initialize(taskParams);
    subTaskGroup.addSubTask(task);

    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  // --------------------------------------------------------------------------------
  // End of DB Scoped replication methods.
}
