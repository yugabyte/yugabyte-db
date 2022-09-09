// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.google.common.base.Preconditions.checkNotNull;
import static com.yugabyte.yw.common.Util.SYSTEM_PLATFORM_DB;
import static com.yugabyte.yw.common.Util.getUUIDRepresentation;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.api.client.util.Objects;
import com.google.common.annotations.VisibleForTesting;
import com.google.common.base.Strings;
import com.google.common.collect.ImmutableMap;
import com.google.common.collect.Streams;
import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.HealthChecker;
import com.yugabyte.yw.commissioner.TaskExecutor;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleClusterServerCtl;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleDestroyServer;
import com.yugabyte.yw.commissioner.tasks.subtasks.BackupTable;
import com.yugabyte.yw.commissioner.tasks.subtasks.BackupTableYb;
import com.yugabyte.yw.commissioner.tasks.subtasks.BackupTableYbc;
import com.yugabyte.yw.commissioner.tasks.subtasks.BackupUniverseKeys;
import com.yugabyte.yw.commissioner.tasks.subtasks.BulkImport;
import com.yugabyte.yw.commissioner.tasks.subtasks.ChangeAdminPassword;
import com.yugabyte.yw.commissioner.tasks.subtasks.ChangeMasterConfig;
import com.yugabyte.yw.commissioner.tasks.subtasks.CreateAlertDefinitions;
import com.yugabyte.yw.commissioner.tasks.subtasks.CreateTable;
import com.yugabyte.yw.commissioner.tasks.subtasks.DeleteBackup;
import com.yugabyte.yw.commissioner.tasks.subtasks.DeleteBackupYb;
import com.yugabyte.yw.commissioner.tasks.subtasks.DeleteNode;
import com.yugabyte.yw.commissioner.tasks.subtasks.DeleteTableFromUniverse;
import com.yugabyte.yw.commissioner.tasks.subtasks.DeleteTablesFromUniverse;
import com.yugabyte.yw.commissioner.tasks.subtasks.DestroyEncryptionAtRest;
import com.yugabyte.yw.commissioner.tasks.subtasks.DisableEncryptionAtRest;
import com.yugabyte.yw.commissioner.tasks.subtasks.EnableEncryptionAtRest;
import com.yugabyte.yw.commissioner.tasks.subtasks.LoadBalancerStateChange;
import com.yugabyte.yw.commissioner.tasks.subtasks.ManageAlertDefinitions;
import com.yugabyte.yw.commissioner.tasks.subtasks.ManipulateDnsRecordTask;
import com.yugabyte.yw.commissioner.tasks.subtasks.MarkUniverseForHealthScriptReUpload;
import com.yugabyte.yw.commissioner.tasks.subtasks.ModifyBlackList;
import com.yugabyte.yw.commissioner.tasks.subtasks.PauseServer;
import com.yugabyte.yw.commissioner.tasks.subtasks.PersistResizeNode;
import com.yugabyte.yw.commissioner.tasks.subtasks.PersistSystemdUpgrade;
import com.yugabyte.yw.commissioner.tasks.subtasks.RebootServer;
import com.yugabyte.yw.commissioner.tasks.subtasks.ResetUniverseVersion;
import com.yugabyte.yw.commissioner.tasks.subtasks.RestoreBackupYb;
import com.yugabyte.yw.commissioner.tasks.subtasks.RestoreBackupYbc;
import com.yugabyte.yw.commissioner.tasks.subtasks.RestoreUniverseKeys;
import com.yugabyte.yw.commissioner.tasks.subtasks.RestoreUniverseKeysYb;
import com.yugabyte.yw.commissioner.tasks.subtasks.RestoreUniverseKeysYbc;
import com.yugabyte.yw.commissioner.tasks.subtasks.ResumeServer;
import com.yugabyte.yw.commissioner.tasks.subtasks.RunYsqlUpgrade;
import com.yugabyte.yw.commissioner.tasks.subtasks.SetActiveUniverseKeys;
import com.yugabyte.yw.commissioner.tasks.subtasks.SetFlagInMemory;
import com.yugabyte.yw.commissioner.tasks.subtasks.SetNodeState;
import com.yugabyte.yw.commissioner.tasks.subtasks.SetNodeStatus;
import com.yugabyte.yw.commissioner.tasks.subtasks.SwamperTargetsFileUpdate;
import com.yugabyte.yw.commissioner.tasks.subtasks.TransferXClusterCerts;
import com.yugabyte.yw.commissioner.tasks.subtasks.UnivSetCertificate;
import com.yugabyte.yw.commissioner.tasks.subtasks.UniverseUpdateSucceeded;
import com.yugabyte.yw.commissioner.tasks.subtasks.UpdateAndPersistGFlags;
import com.yugabyte.yw.commissioner.tasks.subtasks.UpdateMountedDisks;
import com.yugabyte.yw.commissioner.tasks.subtasks.UpdatePlacementInfo;
import com.yugabyte.yw.commissioner.tasks.subtasks.UpdateSoftwareVersion;
import com.yugabyte.yw.commissioner.tasks.subtasks.UpdateUniverseYbcDetails;
import com.yugabyte.yw.commissioner.tasks.subtasks.UpgradeYbc;
import com.yugabyte.yw.commissioner.tasks.subtasks.WaitForDataMove;
import com.yugabyte.yw.commissioner.tasks.subtasks.WaitForEncryptionKeyInMemory;
import com.yugabyte.yw.commissioner.tasks.subtasks.WaitForFollowerLag;
import com.yugabyte.yw.commissioner.tasks.subtasks.WaitForLeaderBlacklistCompletion;
import com.yugabyte.yw.commissioner.tasks.subtasks.WaitForLeadersOnPreferredOnly;
import com.yugabyte.yw.commissioner.tasks.subtasks.WaitForLoadBalance;
import com.yugabyte.yw.commissioner.tasks.subtasks.WaitForMasterLeader;
import com.yugabyte.yw.commissioner.tasks.subtasks.WaitForServer;
import com.yugabyte.yw.commissioner.tasks.subtasks.WaitForServerReady;
import com.yugabyte.yw.commissioner.tasks.subtasks.WaitForTServerHeartBeats;
import com.yugabyte.yw.commissioner.tasks.subtasks.WaitForYbcServer;
import com.yugabyte.yw.commissioner.tasks.subtasks.check.CheckMemory;
import com.yugabyte.yw.commissioner.tasks.subtasks.nodes.UpdateNodeProcess;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.DeleteBootstrapIds;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.DeleteReplication;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.DeleteXClusterConfigEntry;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.ResetXClusterConfigEntry;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.XClusterConfigUpdateMasterAddresses;
import com.yugabyte.yw.commissioner.tasks.subtasks.xcluster.XClusterInfoPersist;
import com.yugabyte.yw.common.DnsManager;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.UniverseInProgressException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.forms.BulkImportParams;
import com.yugabyte.yw.forms.EncryptionAtRestConfig.OpType;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.forms.RestoreBackupParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.KmsConfig;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.helpers.ColumnDetails;
import com.yugabyte.yw.models.helpers.ColumnDetails.YQLDataType;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeStatus;
import com.yugabyte.yw.models.helpers.TableDetails;
import com.yugabyte.yw.models.helpers.TaskType;
import java.io.File;
import java.time.Duration;
import java.util.ArrayList;
import java.util.Collection;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.TimeUnit;
import java.util.function.Consumer;
import java.util.stream.Collectors;
import java.util.stream.IntStream;
import javax.annotation.Nullable;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections.CollectionUtils;
import org.apache.commons.collections.MapUtils;
import org.slf4j.MDC;
import org.yb.ColumnSchema.SortOrder;
import org.yb.CommonTypes.TableType;
import org.yb.client.GetTableSchemaResponse;
import org.yb.client.ListTablesResponse;
import org.yb.client.ModifyClusterConfigIncrementVersion;
import org.yb.client.YBClient;
import org.yb.master.MasterDdlOuterClass;
import org.yb.master.MasterTypes;
import play.api.Play;
import play.libs.Json;

@Slf4j
public abstract class UniverseTaskBase extends AbstractTaskBase {

  protected static final String MIN_WRITE_READ_TABLE_CREATION_RELEASE = "2.6.0.0";

  @VisibleForTesting static final Duration SLEEP_TIME_FORCE_LOCK_RETRY = Duration.ofSeconds(10);

  protected String ysqlPassword;
  protected String ycqlPassword;
  private String ysqlCurrentPassword = Util.DEFAULT_YSQL_PASSWORD;
  private String ysqlUsername = Util.DEFAULT_YSQL_USERNAME;
  private String ycqlCurrentPassword = Util.DEFAULT_YCQL_PASSWORD;
  private String ycqlUsername = Util.DEFAULT_YCQL_USERNAME;
  private String ysqlDb = Util.YUGABYTE_DB;

  enum VersionCheckMode {
    NEVER,
    ALWAYS,
    HA_ONLY
  }

  // Set of locked universes in this task.
  private final Set<UUID> lockedUniversesUuid = new HashSet<>();

  @Inject
  protected UniverseTaskBase(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  private Universe universe = null;

  // The task params.
  @Override
  protected UniverseTaskParams taskParams() {
    return (UniverseTaskParams) taskParams;
  }

  protected Universe getUniverse() {
    return getUniverse(false);
  }

  protected Universe getUniverse(boolean fetchFromDB) {
    if (fetchFromDB) {
      return Universe.getOrBadRequest(taskParams().universeUUID);
    } else {
      if (universe == null) {
        universe = Universe.getOrBadRequest(taskParams().universeUUID);
      }
      return universe;
    }
  }

  protected boolean isLeaderBlacklistValidRF(String nodeName) {
    Cluster curCluster = Universe.getCluster(getUniverse(), nodeName);
    if (curCluster == null) {
      return false;
    }
    return curCluster.userIntent.replicationFactor > 1;
  }

  protected UserIntent getUserIntent() {
    return getUserIntent(false);
  }

  protected UserIntent getUserIntent(boolean fetchFromDB) {
    return getUniverse(fetchFromDB).getUniverseDetails().getPrimaryCluster().userIntent;
  }

  private UniverseUpdater getLockingUniverseUpdater(
      int expectedUniverseVersion, boolean checkSuccess) {
    return getLockingUniverseUpdater(expectedUniverseVersion, checkSuccess, false, false);
  }

  private UniverseUpdater getLockingUniverseUpdater(
      int expectedUniverseVersion,
      boolean checkSuccess,
      boolean isForceUpdate,
      boolean isResumeOrDelete) {
    return getLockingUniverseUpdater(
        expectedUniverseVersion, checkSuccess, isForceUpdate, isResumeOrDelete, null);
  }

  private UniverseUpdater getLockingUniverseUpdater(
      int expectedUniverseVersion,
      boolean checkSuccess,
      boolean isForceUpdate,
      boolean isResumeOrDelete,
      Consumer<Universe> callback) {
    TaskType owner = TaskExecutor.getTaskType(getClass());
    if (owner == null) {
      log.trace("TaskType not found for class " + this.getClass().getCanonicalName());
    }
    return universe -> {
      if (isFirstTry()) {
        // Universe already has a reference to the last task UUID in case of retry.
        // Check version only when it is a first try.
        verifyUniverseVersion(expectedUniverseVersion, universe);
      }
      UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
      if (universeDetails.universePaused && !isResumeOrDelete) {
        String msg = "Universe " + taskParams().universeUUID + " is currently paused";
        log.error(msg);
        throw new RuntimeException(msg);
      }
      // If this universe is already being edited, fail the request.
      if (!isForceUpdate && universeDetails.updateInProgress) {
        String msg = "Universe " + taskParams().universeUUID + " is already being updated";
        log.error(msg);
        throw new UniverseInProgressException(msg);
      }
      // If the task is retried, check if the task UUID is same as the one in the universe.
      // Check this condition only on retry to retain same behavior as before.
      if (!isForceUpdate
          && !universeDetails.updateSucceeded
          && taskParams().getPreviousTaskUUID() != null
          && !Objects.equal(taskParams().getPreviousTaskUUID(), universeDetails.updatingTaskUUID)) {
        String msg = "Only the last task " + taskParams().getPreviousTaskUUID() + " can be retried";
        log.error(msg);
        throw new RuntimeException(msg);
      }
      markUniverseUpdateInProgress(owner, universe, checkSuccess);
      if (callback != null) {
        callback.accept(universe);
      }
    };
  }

  private void markUniverseUpdateInProgress(
      TaskType owner, Universe universe, boolean checkSuccess) {
    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
    // Persist the updated information about the universe. Mark it as being edited.
    universeDetails.updateInProgress = true;
    universeDetails.updatingTask = owner;
    universeDetails.updatingTaskUUID = userTaskUUID;
    if (checkSuccess) {
      universeDetails.updateSucceeded = false;
    }
    universe.setUniverseDetails(universeDetails);
  }

  /**
   * verifyUniverseVersion
   *
   * @param expectedUniverseVersion
   * @param universe
   *     <p>This is attempting to flag situations where the UI is operating on a stale copy of the
   *     universe for example, when multiple browsers or users are operating on the same universe.
   *     <p>This assumes that the UI supplies the expectedUniverseVersion in the API call but this
   *     is not always true. If the UI does not supply it, expectedUniverseVersion is set from
   *     universe.version itself so this check is not useful in that case.
   */
  public void verifyUniverseVersion(int expectedUniverseVersion, Universe universe) {
    if (expectedUniverseVersion != -1 && expectedUniverseVersion != universe.version) {
      String msg =
          "Universe "
              + taskParams().universeUUID
              + " version "
              + universe.version
              + ", is different from the expected version of "
              + expectedUniverseVersion
              + ". User "
              + "would have to sumbit the operation from a refreshed top-level universe page.";
      log.error(msg);
      throw new IllegalStateException(msg);
    }
  }

  private Universe lockUniverseForUpdate(
      UUID universeUuid, int expectedUniverseVersion, UniverseUpdater updater, boolean checkExist) {
    // Perform the update. If unsuccessful, this will throw a runtime exception which we do not
    // catch as we want to fail.
    Universe universe = saveUniverseDetails(universeUuid, updater, checkExist);
    lockedUniversesUuid.add(universeUuid);
    log.trace("Locked universe {} at version {}.", universeUuid, expectedUniverseVersion);
    // Return the universe object that we have already updated.
    return universe;
  }

  private Universe lockUniverseForUpdate(
      UUID universeUuid, int expectedUniverseVersion, UniverseUpdater updater) {
    return lockUniverseForUpdate(
        universeUuid, expectedUniverseVersion, updater, false /* checkExist */);
  }

  private Universe lockUniverseForUpdate(int expectedUniverseVersion, UniverseUpdater updater) {
    return lockUniverseForUpdate(taskParams().universeUUID, expectedUniverseVersion, updater);
  }

  public SubTaskGroup createManageEncryptionAtRestTask() {
    SubTaskGroup subTaskGroup = null;
    AbstractTaskBase task = null;
    switch (taskParams().encryptionAtRestConfig.opType) {
      case ENABLE:
        subTaskGroup = getTaskExecutor().createSubTaskGroup("EnableEncryptionAtRest", executor);
        task = createTask(EnableEncryptionAtRest.class);
        EnableEncryptionAtRest.Params enableParams = new EnableEncryptionAtRest.Params();
        enableParams.universeUUID = taskParams().universeUUID;
        enableParams.encryptionAtRestConfig = taskParams().encryptionAtRestConfig;
        task.initialize(enableParams);
        subTaskGroup.addSubTask(task);
        getRunnableTask().addSubTaskGroup(subTaskGroup);
        break;
      case DISABLE:
        subTaskGroup = getTaskExecutor().createSubTaskGroup("DisableEncryptionAtRest", executor);
        task = createTask(DisableEncryptionAtRest.class);
        DisableEncryptionAtRest.Params disableParams = new DisableEncryptionAtRest.Params();
        disableParams.universeUUID = taskParams().universeUUID;
        task.initialize(disableParams);
        subTaskGroup.addSubTask(task);
        getRunnableTask().addSubTaskGroup(subTaskGroup);
        break;
      default:
      case UNDEFINED:
        break;
    }

    UniverseUpdater updater =
        new UniverseUpdater() {
          @Override
          public void run(Universe universe) {
            log.info(
                String.format(
                    "Setting encryption at rest status to %s for universe %s",
                    taskParams().encryptionAtRestConfig.opType.name(),
                    universe.universeUUID.toString()));
            // Persist the updated information about the universe.
            // It should have been marked as being edited in lockUniverseForUpdate().
            UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
            if (!universeDetails.updateInProgress) {
              String msg =
                  "Universe "
                      + taskParams().universeUUID
                      + " has not been marked as being updated.";
              log.error(msg);
              throw new RuntimeException(msg);
            }

            universeDetails.encryptionAtRestConfig = taskParams().encryptionAtRestConfig;

            universeDetails.encryptionAtRestConfig.encryptionAtRestEnabled =
                taskParams().encryptionAtRestConfig.opType.equals(OpType.ENABLE);
            universe.setUniverseDetails(universeDetails);
          }
        };
    // Perform the update. If unsuccessful, this will throw a runtime exception which we do not
    // catch as we want to fail.
    saveUniverseDetails(updater);
    log.trace("Wrote user intent for universe {}.", taskParams().universeUUID);

    return subTaskGroup;
  }

  public SubTaskGroup createSetActiveUniverseKeysTask() {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("SetActiveUniverseKeys", executor);
    SetActiveUniverseKeys task = createTask(SetActiveUniverseKeys.class);
    SetActiveUniverseKeys.Params params = new SetActiveUniverseKeys.Params();
    params.universeUUID = taskParams().universeUUID;
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createDestroyEncryptionAtRestTask() {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("DestroyEncryptionAtRest", executor);
    DestroyEncryptionAtRest task = createTask(DestroyEncryptionAtRest.class);
    DestroyEncryptionAtRest.Params params = new DestroyEncryptionAtRest.Params();
    params.universeUUID = taskParams().universeUUID;
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  @Override
  public void initialize(ITaskParams params) {
    super.initialize(params);
    if (taskParams().universeUUID != null) {
      MDC.put("universe-id", taskParams().universeUUID.toString());
    }
    // Create the threadpool for the subtasks to use.
    createThreadpool();
  }

  @Override
  public String getName() {
    return super.getName() + "(" + taskParams().universeUUID + ")";
  }

  /**
   * Locks the universe for updates by setting the 'updateInProgress' flag. If the universe is
   * already being modified, then throws an exception.
   *
   * @param expectedUniverseVersion Lock only if the current version of the universe is at this
   *     version. -1 implies always lock the universe.
   * @param callback Callback is invoked for any pre-processing to be done on the Universe before it
   *     is saved in transaction with 'updateInProgress' flag.
   * @return
   */
  public Universe lockUniverseForUpdate(int expectedUniverseVersion, Consumer<Universe> callback) {
    UniverseUpdater updater =
        getLockingUniverseUpdater(expectedUniverseVersion, true, false, false, callback);
    return lockUniverseForUpdate(expectedUniverseVersion, updater);
  }

  /**
   * It locks the universe for updates by setting the 'updateInProgress' flag. If the universe is
   * already being modified, then throws an exception.
   *
   * @param universeUuid The UUID of the universe to lock
   * @param expectedUniverseVersion Lock only if the current version of the universe is at this
   *     version; -1 implies always lock the universe
   * @return The locked universe
   */
  public Universe lockUniverseForUpdate(UUID universeUuid, int expectedUniverseVersion) {
    UniverseUpdater updater =
        getLockingUniverseUpdater(expectedUniverseVersion, true, false, false);
    return lockUniverseForUpdate(universeUuid, expectedUniverseVersion, updater);
  }

  /**
   * Locks the universe for updates by setting the 'updateInProgress' flag. If the universe is
   * already being modified, then throws an exception.
   *
   * @param expectedUniverseVersion Lock only if the current version of the universe is at this
   *     version. -1 implies always lock the universe.
   */
  public Universe lockUniverseForUpdate(int expectedUniverseVersion) {
    return lockUniverseForUpdate(expectedUniverseVersion, false /* isResumeOrDelete */);
  }

  public Universe lockUniverseForUpdate(int expectedUniverseVersion, boolean isResumeOrDelete) {
    UniverseUpdater updater =
        getLockingUniverseUpdater(expectedUniverseVersion, true, false, isResumeOrDelete);
    return lockUniverseForUpdate(expectedUniverseVersion, updater);
  }

  public Universe forceLockUniverseForUpdate(int expectedUniverseVersion) {
    return forceLockUniverseForUpdate(expectedUniverseVersion, false /* isResumeOrDelete */);
  }

  public Universe forceLockUniverseForUpdate(
      int expectedUniverseVersion, boolean isResumeOrDelete) {
    log.info(
        "Force lock universe {} at version {}.",
        taskParams().universeUUID,
        expectedUniverseVersion);
    if (runtimeConfigFactory
        .forUniverse(Universe.getOrBadRequest(taskParams().universeUUID))
        .getBoolean("yb.task.override_force_universe_lock")) {
      UniverseUpdater updater =
          getLockingUniverseUpdater(
              expectedUniverseVersion,
              true /* checkSuccess */,
              true /* isForceUpdate */,
              isResumeOrDelete);
      return lockUniverseForUpdate(expectedUniverseVersion, updater);
    }
    long retryNumber = 0;
    long maxNumberOfRetries =
        config.getDuration("yb.task.max_force_universe_lock_timeout", TimeUnit.SECONDS)
            / SLEEP_TIME_FORCE_LOCK_RETRY.getSeconds();
    while (retryNumber < maxNumberOfRetries) {
      retryNumber++;
      try {
        return lockUniverseForUpdate(expectedUniverseVersion, isResumeOrDelete);
      } catch (UniverseInProgressException e) {
        log.debug(
            "Universe {} was locked: {}; retrying after {} seconds... (try number {} out of {})",
            taskParams().universeUUID,
            e.getMessage(),
            SLEEP_TIME_FORCE_LOCK_RETRY.getSeconds(),
            retryNumber,
            maxNumberOfRetries);
      }
      waitFor(SLEEP_TIME_FORCE_LOCK_RETRY);
    }
    return lockUniverseForUpdate(expectedUniverseVersion, isResumeOrDelete);
  }

  /**
   * Locks the universe by setting the 'updateInProgress' flag. If the universe is already being
   * modified, then throws an exception. Any tasks involving tables should use this method, not any
   * other.
   *
   * @param expectedUniverseVersion Lock only if the current version of the unvierse is at this
   *     version. -1 implies always lock the universe.
   */
  public Universe lockUniverse(int expectedUniverseVersion) {
    return lockUniverse(taskParams().universeUUID, expectedUniverseVersion);
  }

  public Universe lockUniverse(UUID universeUuid, int expectedUniverseVersion) {
    UniverseUpdater updater = getLockingUniverseUpdater(expectedUniverseVersion, false);
    return lockUniverseForUpdate(universeUuid, expectedUniverseVersion, updater);
  }

  public Universe lockUniverseIfExist(UUID universeUuid, int expectedUniverseVersion) {
    UniverseUpdater updater =
        getLockingUniverseUpdater(expectedUniverseVersion, false /*checkSuccess*/);
    return lockUniverseForUpdate(
        universeUuid, expectedUniverseVersion, updater, true /* checkExist */);
  }

  public Universe unlockUniverseForUpdate(UUID universeUuid) {
    return unlockUniverseForUpdate(universeUuid, null /* error */);
  }

  public Universe unlockUniverseForUpdate() {
    return unlockUniverseForUpdate((String) null);
  }

  public Universe unlockUniverseForUpdate(String error) {
    return unlockUniverseForUpdate(taskParams().universeUUID, error);
  }

  public Universe unlockUniverseForUpdate(UUID universeUUID, String error) {
    if (!lockedUniversesUuid.contains(universeUUID)) {
      log.warn("Unlock universe({}) called when it was not locked.", universeUUID);
      return null;
    }
    final String err = error;
    // Create the update lambda.
    UniverseUpdater updater =
        new UniverseUpdater() {
          @Override
          public void run(Universe universe) {
            // If this universe is not being edited, fail the request.
            UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
            if (!universeDetails.updateInProgress) {
              String msg = "Universe " + universeUUID + " is not being edited.";
              log.error(msg);
              throw new RuntimeException(msg);
            }
            // Persist the updated information about the universe. Mark it as being edited.
            universeDetails.updateInProgress = false;
            universeDetails.updatingTask = null;
            universeDetails.errorString = err;
            if (universeDetails.updateSucceeded) {
              // Clear the task UUID only if the update succeeded.
              universeDetails.updatingTaskUUID = null;
              // Do not save the transient state in the universe.
              universeDetails.nodeDetailsSet.forEach(
                  n -> {
                    n.masterState = null;
                  });
            }
            universe.setUniverseDetails(universeDetails);
          }
        };
    // Update the progress flag to false irrespective of the version increment failure.
    // Universe version in master does not need to be updated as this does not change
    // the Universe state. It simply sets updateInProgress flag to false.
    universe = Universe.saveDetails(universeUUID, updater, false);
    lockedUniversesUuid.remove(universeUUID);
    log.trace("Unlocked universe {} for updates.", universeUUID);
    return universe;
  }

  /** Create a task to mark the change on a universe as success. */
  public SubTaskGroup createMarkUniverseUpdateSuccessTasks() {
    return createMarkUniverseUpdateSuccessTasks(taskParams().universeUUID);
  }

  public SubTaskGroup createMarkUniverseUpdateSuccessTasks(UUID universeUuid) {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("FinalizeUniverseUpdate", executor);
    UniverseUpdateSucceeded.Params params = new UniverseUpdateSucceeded.Params();
    params.universeUUID = universeUuid;
    UniverseUpdateSucceeded task = createTask(UniverseUpdateSucceeded.class);
    task.initialize(params);
    task.setUserTaskUUID(userTaskUUID);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createChangeAdminPasswordTask(
      Cluster primaryCluster,
      String ysqlPassword,
      String ysqlCurrentPassword,
      String ysqlUserName,
      String ysqlDbName,
      String ycqlPassword,
      String ycqlCurrentPassword,
      String ycqlUserName) {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("ChangeAdminPassword", executor);
    ChangeAdminPassword.Params params = new ChangeAdminPassword.Params();
    params.universeUUID = taskParams().universeUUID;
    params.primaryCluster = primaryCluster;
    params.ycqlNewPassword = ycqlPassword;
    params.ysqlNewPassword = ysqlPassword;
    params.ycqlCurrentPassword = ycqlCurrentPassword;
    params.ysqlCurrentPassword = ysqlCurrentPassword;
    params.ycqlUserName = ycqlUserName;
    params.ysqlUserName = ysqlUserName;
    params.ysqlDbName = ysqlDbName;
    ChangeAdminPassword task = createTask(ChangeAdminPassword.class);
    task.initialize(params);
    task.setUserTaskUUID(userTaskUUID);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public void checkAndCreateChangeAdminPasswordTask(Cluster primaryCluster) {
    // Change admin password for Admin user, as specified.
    if ((primaryCluster.userIntent.enableYSQL && primaryCluster.userIntent.enableYSQLAuth)
        || (primaryCluster.userIntent.enableYCQL && primaryCluster.userIntent.enableYCQLAuth)) {
      createChangeAdminPasswordTask(
              primaryCluster,
              ysqlPassword,
              ysqlCurrentPassword,
              ysqlUsername,
              ysqlDb,
              ycqlPassword,
              ycqlCurrentPassword,
              ycqlUsername)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
    }
  }

  /** Create a task to mark the final software version on a universe. */
  public SubTaskGroup createUpdateSoftwareVersionTask(
      String softwareVersion, boolean isSoftwareUpdateViaVm) {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("FinalizeUniverseUpdate", executor);
    UpdateSoftwareVersion.Params params = new UpdateSoftwareVersion.Params();
    params.universeUUID = taskParams().universeUUID;
    params.softwareVersion = softwareVersion;
    params.prevSoftwareVersion = taskParams().ybPrevSoftwareVersion;
    params.isSoftwareUpdateViaVm = isSoftwareUpdateViaVm;
    UpdateSoftwareVersion task = createTask(UpdateSoftwareVersion.class);
    task.initialize(params);
    task.setUserTaskUUID(userTaskUUID);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createUpdateYbcTask(String ybcSoftwareVersion) {
    SubTaskGroup subTaskGroup = getTaskExecutor().createSubTaskGroup("FinalizeYbcUpdate", executor);
    UpdateUniverseYbcDetails.Params params = new UpdateUniverseYbcDetails.Params();
    params.universeUUID = taskParams().universeUUID;
    params.ybcSoftwareVersion = ybcSoftwareVersion;
    params.enableYbc = true;
    params.ybcInstalled = true;
    UpdateUniverseYbcDetails task = createTask(UpdateUniverseYbcDetails.class);
    task.initialize(params);
    task.setUserTaskUUID(userTaskUUID);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /** Creates task to update disabled ybc state in universe details. */
  public SubTaskGroup createDisableYbcUniverseDetails() {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("UpdateDisableYbcDetails", executor);
    UpdateUniverseYbcDetails.Params params = new UpdateUniverseYbcDetails.Params();
    params.universeUUID = taskParams().universeUUID;
    params.ybcSoftwareVersion = null;
    params.enableYbc = false;
    params.ybcInstalled = false;
    UpdateUniverseYbcDetails task = createTask(UpdateUniverseYbcDetails.class);
    task.initialize(params);
    task.setUserTaskUUID(userTaskUUID);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createMarkUniverseForHealthScriptReUploadTask() {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("MarkUniverseForHealthScriptReUpload", executor);
    MarkUniverseForHealthScriptReUpload task =
        createTask(MarkUniverseForHealthScriptReUpload.class);
    task.initialize(taskParams());
    task.setUserTaskUUID(userTaskUUID);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createUpdateSoftwareVersionTask(String softwareVersion) {
    return createUpdateSoftwareVersionTask(softwareVersion, false /*isSoftwareUpdateViaVm*/);
  }

  /** Create a task to run YSQL upgrade on the universe. */
  public SubTaskGroup createRunYsqlUpgradeTask(String ybSoftwareVersion) {
    SubTaskGroup subTaskGroup = getTaskExecutor().createSubTaskGroup("RunYsqlUpgrade", executor);

    RunYsqlUpgrade task = createTask(RunYsqlUpgrade.class);

    RunYsqlUpgrade.Params params = new RunYsqlUpgrade.Params();
    params.universeUUID = taskParams().universeUUID;
    params.ybSoftwareVersion = ybSoftwareVersion;

    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /** Create a task to check memory limit on the universe nodes */
  public SubTaskGroup createAvailabeMemoryCheck(
      Collection<NodeDetails> nodes, String memoryType, Long memoryLimitKB) {
    SubTaskGroup subTaskGroup = getTaskExecutor().createSubTaskGroup("CheckMemory", executor);
    CheckMemory task = createTask(CheckMemory.class);
    CheckMemory.Params params = new CheckMemory.Params();
    params.universeUUID = taskParams().universeUUID;
    params.memoryType = memoryType;
    params.memoryLimitKB = memoryLimitKB;
    params.nodeIpList =
        nodes.stream().map(node -> node.cloudInfo.private_ip).collect(Collectors.toList());
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createPersistResizeNodeTask(String instanceType) {
    return createPersistResizeNodeTask(instanceType, null);
  }

  /** Create a task to persist changes by ResizeNode task */
  public SubTaskGroup createPersistResizeNodeTask(String instanceType, Integer volumeSize) {
    return createPersistResizeNodeTask(instanceType, volumeSize, null);
  }

  /** Create a task to persist changes by ResizeNode task for specific clusters */
  public SubTaskGroup createPersistResizeNodeTask(
      String instanceType, Integer volumeSize, List<UUID> clusterIds) {
    SubTaskGroup subTaskGroup = getTaskExecutor().createSubTaskGroup("PersistResizeNode", executor);
    PersistResizeNode.Params params = new PersistResizeNode.Params();
    params.universeUUID = taskParams().universeUUID;
    params.instanceType = instanceType;
    params.volumeSize = volumeSize;
    params.clusters = clusterIds;
    PersistResizeNode task = createTask(PersistResizeNode.class);
    task.initialize(params);
    task.setUserTaskUUID(userTaskUUID);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /** Create a task to persist changes by Systemd Upgrade task */
  public SubTaskGroup createPersistSystemdUpgradeTask(Boolean useSystemd) {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("PersistSystemdUpgrade", executor);
    PersistSystemdUpgrade.Params params = new PersistSystemdUpgrade.Params();
    params.universeUUID = taskParams().universeUUID;
    params.useSystemd = useSystemd;
    PersistSystemdUpgrade task = createTask(PersistSystemdUpgrade.class);
    task.initialize(params);
    task.setUserTaskUUID(userTaskUUID);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /** Create a task to mark the updated cert on a universe. */
  public SubTaskGroup createUnivSetCertTask(UUID certUUID) {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("FinalizeUniverseUpdate", executor);
    UnivSetCertificate.Params params = new UnivSetCertificate.Params();
    params.universeUUID = taskParams().universeUUID;
    params.certUUID = certUUID;
    UnivSetCertificate task = createTask(UnivSetCertificate.class);
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /** Create a task to create default alert definitions on a universe. */
  public SubTaskGroup createUnivCreateAlertDefinitionsTask() {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("CreateAlertDefinitions", executor);
    CreateAlertDefinitions task = createTask(CreateAlertDefinitions.class);
    task.initialize(taskParams());
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /** Create a task to activate or diactivate universe alert definitions. */
  public SubTaskGroup createUnivManageAlertDefinitionsTask(boolean active) {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("ManageAlertDefinitions", executor);
    ManageAlertDefinitions task = createTask(ManageAlertDefinitions.class);
    ManageAlertDefinitions.Params params = new ManageAlertDefinitions.Params();
    params.universeUUID = taskParams().universeUUID;
    params.active = active;
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /** Create a task to ping yb-controller servers on each node */
  public SubTaskGroup createWaitForYbcServerTask(Collection<NodeDetails> nodeDetailsSet) {
    SubTaskGroup subTaskGroup = getTaskExecutor().createSubTaskGroup("WaitForYbcServer", executor);
    WaitForYbcServer task = createTask(WaitForYbcServer.class);
    WaitForYbcServer.Params params = new WaitForYbcServer.Params();
    params.universeUUID = taskParams().universeUUID;
    params.nodeDetailsSet = new HashSet<>(nodeDetailsSet);
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Creates a task list to destroy nodes and adds it to the task queue.
   *
   * @param nodes : a collection of nodes that need to be removed
   * @param isForceDelete if this is true, ignore ansible errors
   * @param deleteNode if true, the node info is deleted from the universe db.
   * @param deleteRootVolumes if true, the volumes are deleted.
   */
  public SubTaskGroup createDestroyServerTasks(
      Collection<NodeDetails> nodes,
      boolean isForceDelete,
      boolean deleteNode,
      boolean deleteRootVolumes) {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("AnsibleDestroyServers", executor);
    for (NodeDetails node : nodes) {
      // Check if the private ip for the node is set. If not, that means we don't have
      // a clean state to delete the node. Log it, free up the onprem node
      // so that the client can use the node instance to create another universe.
      if (node.cloudInfo.private_ip == null) {
        log.warn(
            String.format(
                "Node %s doesn't have a private IP. Skipping node delete.", node.nodeName));
        if (node.cloudInfo.cloud.equals(
            com.yugabyte.yw.commissioner.Common.CloudType.onprem.name())) {
          try {
            NodeInstance providerNode = NodeInstance.getByName(node.nodeName);
            providerNode.clearNodeDetails();
          } catch (Exception ex) {
            log.warn("On-prem node {} doesn't have a linked instance ", node.nodeName);
          }
          continue;
        }
        if (node.nodeUuid == null) {
          // No other way to identify the node.
          continue;
        }
      }
      AnsibleDestroyServer.Params params = new AnsibleDestroyServer.Params();
      // Set the device information (numVolumes, volumeSize, etc.)
      params.deviceInfo = taskParams().deviceInfo;
      // Set the region name to the proper provider code so we can use it in the cloud API calls.
      params.azUuid = node.azUuid;
      // Add the node name.
      params.nodeName = node.nodeName;
      // Add the node UUID.
      params.nodeUuid = node.nodeUuid;
      // Add the universe uuid.
      params.universeUUID = taskParams().universeUUID;
      // Flag to be set where errors during Ansible Destroy Server will be ignored.
      params.isForceDelete = isForceDelete;
      // Flag to track if node info should be deleted from universe db.
      params.deleteNode = deleteNode;
      // Flag to track if volumes should be deleted from universe.
      params.deleteRootVolumes = deleteRootVolumes;
      // Add the instance type
      params.instanceType = node.cloudInfo.instance_type;
      // Assign the node IP to ensure deletion of the correct node.
      params.nodeIP = node.cloudInfo.private_ip;
      // Create the Ansible task to destroy the server.
      AnsibleDestroyServer task = createTask(AnsibleDestroyServer.class);
      task.initialize(params);
      task.setUserTaskUUID(userTaskUUID);
      // Add it to the task list.
      subTaskGroup.addSubTask(task);
    }
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Creates a task list to pause the nodes and adds to the task queue.
   *
   * @param nodes : a collection of nodes that need to be paused.
   */
  public SubTaskGroup createPauseServerTasks(Collection<NodeDetails> nodes) {
    SubTaskGroup subTaskGroup = getTaskExecutor().createSubTaskGroup("PauseServer", executor);
    for (NodeDetails node : nodes) {
      // Check if the private ip for the node is set. If not, that means we don't have
      // a clean state to pause the node. Log it and skip the node.
      if (node.cloudInfo.private_ip == null) {
        log.warn(
            String.format("Node %s doesn't have a private IP. Skipping pause.", node.nodeName));
        continue;
      }
      PauseServer.Params params = new PauseServer.Params();
      // Set the device information (numVolumes, volumeSize, etc.)
      params.deviceInfo = taskParams().deviceInfo;
      // Set the region name to the proper provider code so we can use it in the cloud API calls.
      params.azUuid = node.azUuid;
      // Add the node name.
      params.nodeName = node.nodeName;
      // Add the universe uuid.
      params.universeUUID = taskParams().universeUUID;
      // Add the instance type
      params.instanceType = node.cloudInfo.instance_type;
      // Assign the node IP to pause the node.
      params.nodeIP = node.cloudInfo.private_ip;
      // Create the task to pause the server.
      PauseServer task = createTask(PauseServer.class);
      task.initialize(params);
      task.setUserTaskUUID(userTaskUUID);
      // Add it to the task list.
      subTaskGroup.addSubTask(task);
    }
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Creates a task list to resume nodes and adds it to the task queue.
   *
   * @param nodes : a collection of nodes that need to be resumed.
   */
  public SubTaskGroup createResumeServerTasks(Collection<NodeDetails> nodes) {
    SubTaskGroup subTaskGroup = getTaskExecutor().createSubTaskGroup("ResumeServer", executor);
    for (NodeDetails node : nodes) {
      // Check if the private ip for the node is set. If not, that means we don't have
      // a clean state to resume the node. Log it and skip the node.
      if (node.cloudInfo.private_ip == null) {
        log.warn(
            String.format(
                "Node %s doesn't have a private IP. Skipping node resume.", node.nodeName));
        continue;
      }
      ResumeServer.Params params = new ResumeServer.Params();
      // Set the device information (numVolumes, volumeSize, etc.)
      params.deviceInfo = taskParams().deviceInfo;
      // Set the region name to the proper provider code so we can use it in the cloud API calls.
      params.azUuid = node.azUuid;
      // Add the node name.
      params.nodeName = node.nodeName;
      // Add the universe uuid.
      params.universeUUID = taskParams().universeUUID;
      // Add the instance type
      params.instanceType = node.cloudInfo.instance_type;
      // Assign the node IP to resume the nodes.
      params.nodeIP = node.cloudInfo.private_ip;
      // Create the task to resume the server.
      ResumeServer task = createTask(ResumeServer.class);
      task.initialize(params);
      task.setUserTaskUUID(userTaskUUID);
      // Add it to the task list.
      subTaskGroup.addSubTask(task);
    }
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Create tasks to update the state of the nodes.
   *
   * @param nodes set of nodes to be updated.
   * @param nodeState State into which these nodes will be transitioned.
   */
  public SubTaskGroup createSetNodeStateTasks(
      Collection<NodeDetails> nodes, NodeDetails.NodeState nodeState) {
    SubTaskGroup subTaskGroup = getTaskExecutor().createSubTaskGroup("SetNodeState", executor);
    for (NodeDetails node : nodes) {
      SetNodeState.Params params = new SetNodeState.Params();
      params.universeUUID = taskParams().universeUUID;
      params.azUuid = node.azUuid;
      params.nodeName = node.nodeName;
      params.state = nodeState;
      SetNodeState task = createTask(SetNodeState.class);
      task.initialize(params);
      task.setUserTaskUUID(userTaskUUID);
      subTaskGroup.addSubTask(task);
    }
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createWaitForKeyInMemoryTask(NodeDetails node) {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("WaitForEncryptionKeyInMemory", executor);
    WaitForEncryptionKeyInMemory.Params params = new WaitForEncryptionKeyInMemory.Params();
    params.universeUUID = taskParams().universeUUID;
    params.nodeAddress = HostAndPort.fromParts(node.cloudInfo.private_ip, node.masterRpcPort);
    params.nodeName = node.nodeName;
    WaitForEncryptionKeyInMemory task = createTask(WaitForEncryptionKeyInMemory.class);
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Create task to execute a Cluster CTL command against specific process
   *
   * @param node node for which the CTL command needs to be executed
   * @param processType, Master/TServer process type
   * @param command, actual command (start, stop, create)
   * @return SubTaskGroup
   */
  public SubTaskGroup createServerControlTask(
      NodeDetails node, UniverseDefinitionTaskBase.ServerType processType, String command) {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("AnsibleClusterServerCtl", executor);
    subTaskGroup.addSubTask(getServerControlTask(node, processType, command, 0));
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Create task to check if a specific process is ready to serve requests on a given node.
   *
   * @param node node for which the check needs to be executed.
   * @param serverType server process type on the node to the check.
   * @param sleepTimeMs default sleep time if server does not support check for readiness.
   * @return SubTaskGroup
   */
  public SubTaskGroup createWaitForServerReady(
      NodeDetails node, ServerType serverType, int sleepTimeMs) {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("WaitForServerReady", executor);
    WaitForServerReady.Params params = new WaitForServerReady.Params();
    params.universeUUID = taskParams().universeUUID;
    params.nodeName = node.nodeName;
    params.serverType = serverType;
    params.waitTimeMs = sleepTimeMs;
    WaitForServerReady task = createTask(WaitForServerReady.class);
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Create task to check if a specific process is caught up to other processes.
   *
   * @param node node for which the check needs to be executed.
   * @param serverType server process type on the node to the check.
   * @return SubTaskGroup
   */
  public SubTaskGroup createWaitForFollowerLagTask(NodeDetails node, ServerType serverType) {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("WaitForLeaderBlacklistCompletion", executor);
    WaitForFollowerLag.Params params = new WaitForFollowerLag.Params();
    params.universeUUID = taskParams().universeUUID;
    params.serverType = serverType;
    params.node = node;
    params.nodeName = node.nodeName;
    WaitForFollowerLag task = createTask(WaitForFollowerLag.class);
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Create tasks to execute Cluster CTL command against specific process in parallel
   *
   * @param nodes set of nodes to issue control command in parallel.
   * @param processType, Master/TServer process type
   * @param command, actual command (start, stop, create)
   * @return SubTaskGroup
   */
  public SubTaskGroup createServerControlTasks(
      List<NodeDetails> nodes, UniverseDefinitionTaskBase.ServerType processType, String command) {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("AnsibleClusterServerCtl", executor);
    for (NodeDetails node : nodes) {
      subTaskGroup.addSubTask(getServerControlTask(node, processType, command, 0));
    }
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  private AnsibleClusterServerCtl getServerControlTask(
      NodeDetails node,
      UniverseDefinitionTaskBase.ServerType processType,
      String command,
      int sleepAfterCmdMillis) {
    AnsibleClusterServerCtl.Params params = new AnsibleClusterServerCtl.Params();
    // Add the node name.
    params.nodeName = node.nodeName;
    // Add the universe uuid.
    params.universeUUID = taskParams().universeUUID;
    // Add the az uuid.
    params.azUuid = node.azUuid;
    // The service and the command we want to run.
    params.process = processType.toString().toLowerCase();
    params.command = command;
    params.sleepAfterCmdMills = sleepAfterCmdMillis;

    params.enableYbc = taskParams().enableYbc;
    params.ybcSoftwareVersion = taskParams().ybcSoftwareVersion;
    params.installYbc = taskParams().installYbc;
    params.ybcInstalled = taskParams().ybcInstalled;

    // Set the InstanceType
    params.instanceType = node.cloudInfo.instance_type;
    params.checkVolumesAttached = processType == ServerType.TSERVER && command.equals("start");
    // Create the Ansible task to get the server info.
    AnsibleClusterServerCtl task = createTask(AnsibleClusterServerCtl.class);
    task.initialize(params);
    return task;
  }

  /**
   * Create task to update the state of single node.
   *
   * @param node node for which we need to update the state
   * @param nodeState State into which these nodes will be transitioned.
   */
  public SubTaskGroup createSetNodeStateTask(NodeDetails node, NodeDetails.NodeState nodeState) {
    SubTaskGroup subTaskGroup = getTaskExecutor().createSubTaskGroup("SetNodeState", executor);
    SetNodeState.Params params = new SetNodeState.Params();
    params.azUuid = node.azUuid;
    params.universeUUID = taskParams().universeUUID;
    params.nodeName = node.nodeName;
    params.state = nodeState;
    SetNodeState task = createTask(SetNodeState.class);
    task.initialize(params);
    task.setUserTaskUUID(userTaskUUID);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Create tasks to update the status of the nodes.
   *
   * @param nodes the set if nodes to be updated.
   * @param nodeStatus the status into which these nodes will be transitioned.
   * @return
   */
  public SubTaskGroup createSetNodeStatusTasks(
      Collection<NodeDetails> nodes, NodeStatus nodeStatus) {
    SubTaskGroup subTaskGroup = getTaskExecutor().createSubTaskGroup("SetNodeStatus", executor);
    for (NodeDetails node : nodes) {
      SetNodeStatus.Params params = new SetNodeStatus.Params();
      params.universeUUID = taskParams().universeUUID;
      params.azUuid = node.azUuid;
      params.nodeName = node.nodeName;
      params.nodeStatus = nodeStatus;
      SetNodeStatus task = createTask(SetNodeStatus.class);
      task.initialize(params);
      task.setUserTaskUUID(userTaskUUID);
      subTaskGroup.addSubTask(task);
    }
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Create a task to update the swamper target file
   *
   * @param removeFile, flag to state if we want to remove the swamper or not
   */
  public void createSwamperTargetUpdateTask(boolean removeFile) {
    if (!config.getBoolean(MetricQueryHelper.PROMETHEUS_MANAGEMENT_ENABLED)) {
      return;
    }
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("SwamperTargetFileUpdate", executor);
    SwamperTargetsFileUpdate.Params params = new SwamperTargetsFileUpdate.Params();
    SwamperTargetsFileUpdate task = createTask(SwamperTargetsFileUpdate.class);
    params.universeUUID = taskParams().universeUUID;
    params.removeFile = removeFile;
    task.initialize(params);
    subTaskGroup.setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
  }

  /**
   * Create a task to create a table.
   *
   * @param tableType type of the table.
   * @param tableName name of the table.
   * @param tableDetails table options and related details.
   * @param ifNotExist create only if it does not exist.
   * @return
   */
  public SubTaskGroup createTableTask(
      TableType tableType, String tableName, TableDetails tableDetails, boolean ifNotExist) {
    SubTaskGroup subTaskGroup = getTaskExecutor().createSubTaskGroup("CreateTable", executor);
    CreateTable task = createTask(CreateTable.class);
    CreateTable.Params params = new CreateTable.Params();
    params.universeUUID = taskParams().universeUUID;
    params.tableType = tableType;
    params.tableName = tableName;
    params.tableDetails = tableDetails;
    params.ifNotExist = ifNotExist;
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public void checkAndCreateRedisTableTask(Cluster primaryCluster) {
    if (primaryCluster.userIntent.enableYEDIS) {
      // Create a simple redis table.
      createTableTask(
              TableType.REDIS_TABLE_TYPE,
              YBClient.REDIS_DEFAULT_TABLE_NAME,
              null /* table details */,
              true /* ifNotExist */)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
    }
  }

  /** Create a task to create write/read test table wor write/read metric and alert. */
  public SubTaskGroup createReadWriteTestTableTask(int numPartitions, boolean ifNotExist) {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("CreateReadWriteTestTable", executor);

    CreateTable task = createTask(CreateTable.class);

    ColumnDetails idColumn = new ColumnDetails();
    idColumn.isClusteringKey = true;
    idColumn.name = "id";
    idColumn.type = YQLDataType.SMALLINT;
    idColumn.sortOrder = SortOrder.ASC;

    TableDetails details = new TableDetails();
    details.tableName = "write_read_test";
    details.keyspace = SYSTEM_PLATFORM_DB;
    details.columns = new ArrayList<>();
    details.columns.add(idColumn);
    // Split at 0, 100, 200, 300 ... (numPartitions - 1) * 100
    if (numPartitions > 1) {
      details.splitValues =
          IntStream.range(0, numPartitions)
              .mapToObj(num -> String.valueOf(num * 100))
              .collect(Collectors.toList());
    }

    CreateTable.Params params = new CreateTable.Params();
    params.universeUUID = taskParams().universeUUID;
    params.tableType = TableType.PGSQL_TABLE_TYPE;
    params.tableName = details.tableName;
    params.tableDetails = details;
    params.ifNotExist = ifNotExist;

    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public void checkAndCreateReadWriteTestTableTask(Cluster primaryCluster) {
    if (primaryCluster.userIntent.enableYSQL
        && CommonUtils.isReleaseEqualOrAfter(
            MIN_WRITE_READ_TABLE_CREATION_RELEASE, primaryCluster.userIntent.ybSoftwareVersion)) {
      // Create read-write test table
      List<NodeDetails> tserverLiveNodes =
          getUniverse()
              .getUniverseDetails()
              .getNodesInCluster(primaryCluster.uuid)
              .stream()
              .filter(nodeDetails -> nodeDetails.isTserver)
              .collect(Collectors.toList());
      createReadWriteTestTableTask(tserverLiveNodes.size(), true)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
    }
  }

  /**
   * Create a task to delete a table.
   *
   * @param params The necessary parameters for dropping a table.
   */
  public SubTaskGroup createDeleteTableFromUniverseTask(DeleteTableFromUniverse.Params params) {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("DeleteTableFromUniverse", executor);
    DeleteTableFromUniverse task = createTask(DeleteTableFromUniverse.class);
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * It creates a subtask to delete a set of tables in one universe.
   *
   * <p>Note: DeleteTablesFromUniverse deletes the tables in sequence because the coreDB might not
   * support deleting several tables in parallel.
   *
   * @param universeUuid The UUID of the universe to delete the tables from
   * @param keyspaceTablesMap A map from keyspace name to a list of tables' names in that keyspace
   *     to be deleted
   */
  public SubTaskGroup createDeleteTablesFromUniverseTask(
      UUID universeUuid, Map<String, List<String>> keyspaceTablesMap) {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("DeleteTablesFromUniverse", executor);
    DeleteTablesFromUniverse.Params deleteTablesFromUniverseParams =
        new DeleteTablesFromUniverse.Params();
    deleteTablesFromUniverseParams.universeUUID = universeUuid;
    deleteTablesFromUniverseParams.keyspaceTablesMap = keyspaceTablesMap;

    DeleteTablesFromUniverse task = createTask(DeleteTablesFromUniverse.class);
    task.initialize(deleteTablesFromUniverseParams);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createWaitForServersTasks(Collection<NodeDetails> nodes, ServerType type) {
    return createWaitForServersTasks(
        nodes, type, config.getDuration("yb.wait_for_server_timeout") /* default timeout */);
  }

  /**
   * Create a task list to ping all servers until they are up.
   *
   * @param nodes : a collection of nodes that need to be pinged.
   * @param type : Master or tserver type server running on these nodes.
   * @param timeout : time to wait for each rpc call to the server.
   */
  public SubTaskGroup createWaitForServersTasks(
      Collection<NodeDetails> nodes, ServerType type, Duration timeout) {
    SubTaskGroup subTaskGroup = getTaskExecutor().createSubTaskGroup("WaitForServer", executor);
    for (NodeDetails node : nodes) {
      WaitForServer.Params params = new WaitForServer.Params();
      params.universeUUID = taskParams().universeUUID;
      params.nodeName = node.nodeName;
      params.serverType = type;
      params.serverWaitTimeoutMs = timeout.toMillis();
      WaitForServer task = createTask(WaitForServer.class);
      task.initialize(params);
      subTaskGroup.addSubTask(task);
    }
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  protected SubTaskGroup createUpdateMountedDisksTask(
      NodeDetails node, String currentInstanceType, DeviceInfo currentDeviceInfo) {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("UpdateMountedDisks", executor);
    UpdateMountedDisks.Params params = new UpdateMountedDisks.Params();

    params.nodeName = node.nodeName;
    params.universeUUID = taskParams().universeUUID;
    params.azUuid = node.azUuid;
    params.instanceType = currentInstanceType;
    params.deviceInfo = currentDeviceInfo;

    UpdateMountedDisks updateMountedDisksTask = createTask(UpdateMountedDisks.class);
    updateMountedDisksTask.initialize(params);
    subTaskGroup.addSubTask(updateMountedDisksTask);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /** Creates a task to persist customized gflags to be used by server processes. */
  public SubTaskGroup updateGFlagsPersistTasks(
      Map<String, String> masterGFlags, Map<String, String> tserverGFlags) {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("UpdateAndPersistGFlags", executor);
    UpdateAndPersistGFlags.Params params = new UpdateAndPersistGFlags.Params();
    params.universeUUID = taskParams().universeUUID;
    params.masterGFlags = masterGFlags;
    params.tserverGFlags = tserverGFlags;
    UpdateAndPersistGFlags task = createTask(UpdateAndPersistGFlags.class);
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Creates a task to bulk import data from an s3 bucket into a given table.
   *
   * @param taskParams Info about the table and universe of the table to import into.
   */
  public SubTaskGroup createBulkImportTask(BulkImportParams taskParams) {
    SubTaskGroup subTaskGroup = getTaskExecutor().createSubTaskGroup("BulkImport", executor);
    BulkImport task = createTask(BulkImport.class);
    task.initialize(taskParams);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Creates a task delete the given node name from the univers.
   *
   * @param nodeName name of a node in the taskparams' uuid universe.
   */
  public SubTaskGroup createDeleteNodeFromUniverseTask(String nodeName) {
    SubTaskGroup subTaskGroup = getTaskExecutor().createSubTaskGroup("DeleteNode", executor);
    NodeTaskParams params = new NodeTaskParams();
    params.nodeName = nodeName;
    params.universeUUID = taskParams().universeUUID;
    DeleteNode task = createTask(DeleteNode.class);
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Add or Remove Master process on the node
   *
   * @param node the node to add/remove master process on
   * @param isAdd whether Master is being added or removed.
   * @param subTask subtask type
   */
  public void createChangeConfigTask(
      NodeDetails node, boolean isAdd, UserTaskDetails.SubTaskGroupType subTask) {
    createChangeConfigTask(node, isAdd, subTask, false);
  }

  public void createChangeConfigTask(
      NodeDetails node,
      boolean isAdd,
      UserTaskDetails.SubTaskGroupType subTask,
      boolean useHostPort) {
    // Create a new task list for the change config so that it happens one by one.
    String subtaskGroupName =
        "ChangeMasterConfig(" + node.nodeName + ", " + (isAdd ? "add" : "remove") + ")";
    SubTaskGroup subTaskGroup = getTaskExecutor().createSubTaskGroup(subtaskGroupName, executor);
    // Create the task params.
    ChangeMasterConfig.Params params = new ChangeMasterConfig.Params();
    // Set the azUUID
    params.azUuid = node.azUuid;
    // Add the node name.
    params.nodeName = node.nodeName;
    // Add the universe uuid.
    params.universeUUID = taskParams().universeUUID;
    // This is an add master.
    params.opType =
        isAdd ? ChangeMasterConfig.OpType.AddMaster : ChangeMasterConfig.OpType.RemoveMaster;
    params.useHostPort = useHostPort;
    // Create the task.
    ChangeMasterConfig changeConfig = createTask(ChangeMasterConfig.class);
    changeConfig.initialize(params);
    // Add it to the task list.
    subTaskGroup.addSubTask(changeConfig);
    // Add the task list to the task queue.
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    // Configure the user facing subtask for this task list.
    subTaskGroup.setSubTaskGroupType(subTask);
  }

  /**
   * Start T-Server process on the given node
   *
   * @param currentNode the node to operate upon
   * @param taskType Command start/stop
   * @return Subtask group
   */
  public SubTaskGroup createTServerTaskForNode(NodeDetails currentNode, String taskType) {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("AnsibleClusterServerCtl", executor);
    AnsibleClusterServerCtl.Params params = new AnsibleClusterServerCtl.Params();
    // Add the node name.
    params.nodeName = currentNode.nodeName;
    // Add the universe uuid.
    params.universeUUID = taskParams().universeUUID;
    // Add the az uuid.
    params.azUuid = currentNode.azUuid;
    // The service and the command we want to run.
    params.process = "tserver";
    params.command = taskType;
    // Set the InstanceType
    params.instanceType = currentNode.cloudInfo.instance_type;
    // Create the Ansible task to get the server info.
    AnsibleClusterServerCtl task = createTask(AnsibleClusterServerCtl.class);
    task.initialize(params);
    // Add it to the task list.
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Wait for Master Leader Election
   *
   * @return subtask group
   */
  public SubTaskGroup createWaitForMasterLeaderTask() {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("WaitForMasterLeader", executor);
    WaitForMasterLeader task = createTask(WaitForMasterLeader.class);
    WaitForMasterLeader.Params params = new WaitForMasterLeader.Params();
    params.universeUUID = taskParams().universeUUID;
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  // Helper function to create a process update object.
  private UpdateNodeProcess getUpdateTaskProcess(
      String nodeName, ServerType processType, Boolean isAdd) {
    // Create the task params.
    UpdateNodeProcess.Params params = new UpdateNodeProcess.Params();
    params.processType = processType;
    params.isAdd = isAdd;
    params.universeUUID = taskParams().universeUUID;
    params.nodeName = nodeName;
    UpdateNodeProcess updateNodeProcess = createTask(UpdateNodeProcess.class);
    updateNodeProcess.initialize(params);
    return updateNodeProcess;
  }

  /**
   * Update the process state across all the given servers in Yugaware DB.
   *
   * @param servers : Set of nodes whose process state is to be updated.
   * @param processType : process type: master or tserver.
   * @param isAdd : true if the process is being added, false otherwise.
   */
  public void createUpdateNodeProcessTasks(
      Set<NodeDetails> servers, ServerType processType, Boolean isAdd) {
    SubTaskGroup subTaskGroup = getTaskExecutor().createSubTaskGroup("UpdateNodeProcess", executor);
    for (NodeDetails server : servers) {
      UpdateNodeProcess updateNodeProcess =
          getUpdateTaskProcess(server.nodeName, processType, isAdd);
      // Add it to the task list.
      subTaskGroup.addSubTask(updateNodeProcess);
    }
    subTaskGroup.setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
  }

  /**
   * Update the given node's process state in Yugaware DB,
   *
   * @param nodeName : name of the node where the process state is to be updated.
   * @param processType : process type: master or tserver.
   * @param isAdd : boolean signifying if the process is being added or removed.
   * @return The subtask group.
   */
  public SubTaskGroup createUpdateNodeProcessTask(
      String nodeName, ServerType processType, Boolean isAdd) {
    SubTaskGroup subTaskGroup = getTaskExecutor().createSubTaskGroup("UpdateNodeProcess", executor);
    UpdateNodeProcess updateNodeProcess = getUpdateTaskProcess(nodeName, processType, isAdd);
    // Add it to the task list.
    subTaskGroup.addSubTask(updateNodeProcess);
    // Add the task list to the task queue.
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Creates a task list to start the masters and adds it to the task queue.
   *
   * @param nodes : a collection of nodes that need masters to be spawned.
   * @return The subtask group.
   */
  public SubTaskGroup createStartMasterTasks(Collection<NodeDetails> nodes) {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("AnsibleClusterServerCtl", executor);
    for (NodeDetails node : nodes) {
      AnsibleClusterServerCtl.Params params = new AnsibleClusterServerCtl.Params();
      // Add the node name.
      params.nodeName = node.nodeName;
      // Add the universe uuid.
      params.universeUUID = taskParams().universeUUID;
      // Add the az uuid.
      params.azUuid = node.azUuid;
      // The service and the command we want to run.
      params.process = "master";
      params.command = "start";
      params.placementUuid = node.placementUuid;
      // Set the InstanceType
      params.instanceType = node.cloudInfo.instance_type;
      // Create the Ansible task to get the server info.
      AnsibleClusterServerCtl task = createTask(AnsibleClusterServerCtl.class);
      task.initialize(params);
      // Add it to the task list.
      subTaskGroup.addSubTask(task);
    }
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Creates a task list to stop the masters of the cluster and adds it to the task queue.
   *
   * @param nodes set of nodes to be stopped as master
   */
  public SubTaskGroup createStopMasterTasks(Collection<NodeDetails> nodes) {
    return createStopServerTasks(nodes, "master", false);
  }

  /**
   * Creates a task list to stop the yb-controller process on cluster's node and adds it to the
   * queue.
   *
   * @param nodes set of nodes on which yb-controller has to be stopped
   */
  public SubTaskGroup createStopYbControllerTasks(Collection<NodeDetails> nodes) {
    return createStopServerTasks(nodes, "controller", false);
  }
  /**
   * Creates a task list to stop the tservers of the cluster and adds it to the task queue.
   *
   * @param nodes set of nodes to be stopped as master
   */
  public SubTaskGroup createStopServerTasks(
      Collection<NodeDetails> nodes, String serverType, boolean isForceDelete) {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("AnsibleClusterServerCtl", executor);
    for (NodeDetails node : nodes) {
      AnsibleClusterServerCtl.Params params = new AnsibleClusterServerCtl.Params();
      // Add the node name.
      params.nodeName = node.nodeName;
      // Add the universe uuid.
      params.universeUUID = taskParams().universeUUID;
      // Add the az uuid.
      params.azUuid = node.azUuid;
      // The service and the command we want to run.
      params.process = serverType;
      params.command = "stop";
      // Set the InstanceType
      params.instanceType = node.cloudInfo.instance_type;
      params.isForceDelete = isForceDelete;
      // Create the Ansible task to get the server info.
      AnsibleClusterServerCtl task = createTask(AnsibleClusterServerCtl.class);
      task.initialize(params);
      // Add it to the task list.
      subTaskGroup.addSubTask(task);
    }
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createTableBackupTask(BackupTableParams taskParams) {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("BackupTable", executor, taskParams.ignoreErrors);
    BackupTable task = createTask(BackupTable.class);
    task.initialize(taskParams);
    task.setUserTaskUUID(userTaskUUID);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  // Todo: This code is mostly copied from the createBackup task. Use these method in createBackup.
  public BackupTableParams getBackupTableParams(
      BackupRequestParams backupRequestParams, Set<String> tablesToBackup) {
    BackupTableParams backupTableParams = new BackupTableParams(backupRequestParams);
    List<BackupTableParams> backupTableParamsList = new ArrayList<>();
    HashMap<String, BackupTableParams> keyspaceMap = new HashMap<>();
    // Todo: add comments. Backup the whole keyspace.
    Universe universe = Universe.getOrBadRequest(backupRequestParams.universeUUID);
    String universeMasterAddresses = universe.getMasterAddresses(true /* mastersQueryable */);
    String universeCertificate = universe.getCertificateNodetoNode();
    try (YBClient client = ybService.getClient(universeMasterAddresses, universeCertificate)) {
      ListTablesResponse listTablesResponse =
          client.getTablesList(
              null /* nameFilter */, true /* excludeSystemTables */, null /* namespace */);
      List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tableInfoList =
          listTablesResponse.getTableInfoList();
      if (!backupTableParams.isFullBackup) {
        for (BackupRequestParams.KeyspaceTable keyspaceTable :
            backupRequestParams.keyspaceTableList) {
          BackupTableParams backupParams =
              new BackupTableParams(backupRequestParams, keyspaceTable.keyspace);
          if (CollectionUtils.isNotEmpty(keyspaceTable.tableUUIDList)) {
            Set<UUID> tableSet = new HashSet<>(keyspaceTable.tableUUIDList);
            for (UUID tableUUID : tableSet) {
              GetTableSchemaResponse tableSchema =
                  client.getTableSchemaByUUID(tableUUID.toString().replace("-", ""));
              // If table is not REDIS or YCQL, ignore.
              if (tableSchema.getTableType().equals(TableType.PGSQL_TABLE_TYPE)
                  || !tableSchema.getTableType().equals(backupRequestParams.backupType)
                  || tableSchema.getTableType().equals(TableType.TRANSACTION_STATUS_TABLE_TYPE)
                  || !keyspaceTable.keyspace.equals(tableSchema.getNamespace())) {
                log.info(
                    "Skipping backup of table with UUID: "
                        + tableUUID
                        + " and keyspace: "
                        + keyspaceTable.keyspace);
                continue;
              }
              backupParams.tableNameList.add(tableSchema.getTableName());
              backupParams.tableUUIDList.add(tableUUID);
              log.info(
                  "Queuing backup for table {}:{}",
                  tableSchema.getNamespace(),
                  tableSchema.getTableName());
              if (tablesToBackup != null) {
                tablesToBackup.add(
                    String.format("%s:%s", tableSchema.getNamespace(), tableSchema.getTableName()));
              }
            }
            backupTableParamsList.add(backupParams);
          } else {
            for (MasterDdlOuterClass.ListTablesResponsePB.TableInfo table : tableInfoList) {
              TableType tableType = table.getTableType();
              String tableKeySpace = table.getNamespace().getName();
              String tableUUIDString = table.getId().toStringUtf8();
              String tableName = table.getName();
              UUID tableUUID = getUUIDRepresentation(tableUUIDString);
              if (!tableType.equals(backupRequestParams.backupType)
                  || tableType.equals(TableType.TRANSACTION_STATUS_TABLE_TYPE)
                  || table.getRelationType().equals(MasterTypes.RelationType.INDEX_TABLE_RELATION)
                  || !keyspaceTable.keyspace.equals(tableKeySpace)) {
                log.info(
                    "Skipping keyspace/universe backup of table "
                        + tableUUID
                        + ". Expected keyspace is "
                        + keyspaceTable.keyspace
                        + "; actual keyspace is "
                        + tableKeySpace);
                continue;
              }

              if (tableType.equals(TableType.PGSQL_TABLE_TYPE)) {
                if (!keyspaceMap.containsKey(tableKeySpace)) {
                  keyspaceMap.put(tableKeySpace, backupParams);
                  backupTableParamsList.add(backupParams);
                  if (tablesToBackup != null) {
                    tablesToBackup.add(String.format("%s:%s", tableKeySpace, table.getName()));
                  }
                }
              } else if (tableType.equals(TableType.YQL_TABLE_TYPE)
                  || tableType.equals(TableType.REDIS_TABLE_TYPE)) {
                if (!keyspaceMap.containsKey(tableKeySpace)) {
                  keyspaceMap.put(tableKeySpace, backupParams);
                  backupTableParamsList.add(backupParams);
                }
                BackupTableParams currentBackup = keyspaceMap.get(tableKeySpace);
                currentBackup.tableNameList.add(tableName);
                currentBackup.tableUUIDList.add(tableUUID);
                if (tablesToBackup != null) {
                  tablesToBackup.add(String.format("%s:%s", tableKeySpace, table.getName()));
                }
              } else {
                log.error(
                    "Unrecognized table type {} for {}:{}", tableType, tableKeySpace, tableName);
              }
              log.info("Queuing backup for table {}:{}", tableKeySpace, tableName);
            }
          }
        }
      } else {
        for (MasterDdlOuterClass.ListTablesResponsePB.TableInfo table : tableInfoList) {
          TableType tableType = table.getTableType();
          String tableKeySpace = table.getNamespace().getName();
          String tableUUIDString = table.getId().toStringUtf8();
          String tableName = table.getName();
          UUID tableUUID = getUUIDRepresentation(tableUUIDString);
          if (!tableType.equals(backupRequestParams.backupType)
              || tableType.equals(TableType.TRANSACTION_STATUS_TABLE_TYPE)
              || table.getRelationType().equals(MasterTypes.RelationType.INDEX_TABLE_RELATION)) {
            log.info("Skipping backup of table " + tableUUID);
            continue;
          }
          if (tableType.equals(TableType.PGSQL_TABLE_TYPE)
              && SYSTEM_PLATFORM_DB.equals(tableKeySpace)) {
            log.info("Skipping " + SYSTEM_PLATFORM_DB + " database");
            continue;
          }

          if (tableType.equals(TableType.PGSQL_TABLE_TYPE)) {
            if (!keyspaceMap.containsKey(tableKeySpace)) {
              BackupTableParams backupParams =
                  new BackupTableParams(backupRequestParams, tableKeySpace);
              keyspaceMap.put(tableKeySpace, backupParams);
              backupTableParamsList.add(backupParams);
              if (tablesToBackup != null) {
                tablesToBackup.add(String.format("%s:%s", tableKeySpace, table.getName()));
              }
            }
          } else if (tableType.equals(TableType.YQL_TABLE_TYPE)
              || tableType.equals(TableType.REDIS_TABLE_TYPE)) {
            if (!keyspaceMap.containsKey(tableKeySpace)) {
              BackupTableParams backupParams =
                  new BackupTableParams(backupRequestParams, tableKeySpace);
              keyspaceMap.put(tableKeySpace, backupParams);
              backupTableParamsList.add(backupParams);
            }
            BackupTableParams currentBackup = keyspaceMap.get(tableKeySpace);
            currentBackup.tableNameList.add(tableName);
            currentBackup.tableUUIDList.add(tableUUID);
            if (tablesToBackup != null) {
              tablesToBackup.add(String.format("%s:%s", tableKeySpace, table.getName()));
            }
          } else {
            log.error("Unrecognized table type {} for {}:{}", tableType, tableKeySpace, tableName);
          }
          log.info("Queuing backup for table {}:{}", tableKeySpace, tableName);
        }
      }
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    }

    if (backupTableParamsList.isEmpty()) {
      throw new RuntimeException("Invalid Keyspaces or no tables to backup");
    }
    backupTableParams.backupList = backupTableParamsList;
    return backupTableParams;
  }

  protected Backup createAllBackupSubtasks(
      BackupRequestParams backupRequestParams, SubTaskGroupType subTaskGroupType) {
    return createAllBackupSubtasks(backupRequestParams, subTaskGroupType, null, false);
  }

  protected Backup createAllBackupSubtasks(
      BackupRequestParams backupRequestParams,
      SubTaskGroupType subTaskGroupType,
      Set<String> tablesToBackup,
      boolean ybcBackup) {
    BackupTableParams backupTableParams = getBackupTableParams(backupRequestParams, tablesToBackup);

    if (backupRequestParams.alterLoadBalancer) {
      createLoadBalancerStateChangeTask(false).setSubTaskGroupType(subTaskGroupType);
    }

    // Create the backup object.
    Backup backup =
        Backup.create(
            backupRequestParams.customerUUID,
            backupTableParams,
            ybcBackup
                ? Backup.BackupCategory.YB_CONTROLLER
                : Backup.BackupCategory.YB_BACKUP_SCRIPT,
            Backup.BackupVersion.V2);
    backup.setTaskUUID(userTaskUUID);
    backupTableParams.backupUuid = backup.backupUUID;

    for (BackupTableParams backupParams : backupTableParams.backupList) {
      createEncryptedUniverseKeyBackupTask(backupParams).setSubTaskGroupType(subTaskGroupType);
    }
    if (ybcBackup) {
      createTableBackupTaskYbc(backupTableParams).setSubTaskGroupType(subTaskGroupType);
    } else {
      createTableBackupTaskYb(backupTableParams).setSubTaskGroupType(subTaskGroupType);
    }

    if (backupRequestParams.alterLoadBalancer) {
      createLoadBalancerStateChangeTask(true).setSubTaskGroupType(subTaskGroupType);
    }

    return backup;
  }

  protected void createAllRestoreSubtasks(
      RestoreBackupParams restoreBackupParams, SubTaskGroupType subTaskGroupType, boolean isYbc) {
    if (restoreBackupParams.alterLoadBalancer) {
      createLoadBalancerStateChangeTask(false).setSubTaskGroupType(subTaskGroupType);
    }

    if (restoreBackupParams.backupStorageInfoList != null) {
      for (RestoreBackupParams.BackupStorageInfo backupStorageInfo :
          restoreBackupParams.backupStorageInfoList) {
        // If KMS is enabled, it needs to restore the keys first.
        if (KmsConfig.get(restoreBackupParams.kmsConfigUUID) != null) {
          RestoreBackupParams restoreKeyParams =
              new RestoreBackupParams(
                  restoreBackupParams,
                  backupStorageInfo,
                  RestoreBackupParams.ActionType.RESTORE_KEYS);
          if (isYbc) {
            createEncryptedUniverseKeyRestoreTaskYbc(restoreKeyParams)
                .setSubTaskGroupType(subTaskGroupType);
          } else {
            createRestoreBackupTask(restoreKeyParams).setSubTaskGroupType(subTaskGroupType);
            createEncryptedUniverseKeyRestoreTaskYb(restoreKeyParams)
                .setSubTaskGroupType(subTaskGroupType);
          }
        }
        // Restore the data.
        RestoreBackupParams restoreDataParams =
            new RestoreBackupParams(
                restoreBackupParams, backupStorageInfo, RestoreBackupParams.ActionType.RESTORE);

        if (isYbc) {
          createRestoreBackupYbcTask(restoreDataParams).setSubTaskGroupType(subTaskGroupType);
        } else {
          createRestoreBackupTask(restoreDataParams).setSubTaskGroupType(subTaskGroupType);
        }
      }
    }

    if (restoreBackupParams.alterLoadBalancer) {
      createLoadBalancerStateChangeTask(true).setSubTaskGroupType(subTaskGroupType);
    }
  }

  public SubTaskGroup createTableBackupTaskYb(BackupTableParams taskParams) {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("BackupTableYb", executor, taskParams.ignoreErrors);
    BackupTableYb task = createTask(BackupTableYb.class);
    task.initialize(taskParams);
    task.setUserTaskUUID(userTaskUUID);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createTableBackupTaskYbc(BackupTableParams tableParams) {
    SubTaskGroup subTaskGroup = getTaskExecutor().createSubTaskGroup("BackupTableYbc", executor);
    BackupTableYbc task = createTask(BackupTableYbc.class);
    task.initialize(tableParams);
    task.setUserTaskUUID(userTaskUUID);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createRestoreBackupTask(RestoreBackupParams taskParams) {
    SubTaskGroup subTaskGroup = getTaskExecutor().createSubTaskGroup("RestoreBackupYb", executor);
    RestoreBackupYb task = createTask(RestoreBackupYb.class);
    task.initialize(taskParams);
    task.setUserTaskUUID(userTaskUUID);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createRestoreBackupYbcTask(RestoreBackupParams taskParams) {
    SubTaskGroup subTaskGroup = getTaskExecutor().createSubTaskGroup("RestoreBackupYbc", executor);
    RestoreBackupYbc task = createTask(RestoreBackupYbc.class);
    task.initialize(taskParams);
    task.setUserTaskUUID(userTaskUUID);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createDeleteBackupTasks(List<Backup> backups, UUID customerUUID) {
    SubTaskGroup subTaskGroup = getTaskExecutor().createSubTaskGroup("DeleteBackup", executor);
    for (Backup backup : backups) {
      DeleteBackup.Params params = new DeleteBackup.Params();
      params.backupUUID = backup.backupUUID;
      params.customerUUID = customerUUID;
      DeleteBackup task = createTask(DeleteBackup.class);
      task.initialize(params);
      subTaskGroup.addSubTask(task);
    }
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createDeleteBackupYbTasks(List<Backup> backups, UUID customerUUID) {
    SubTaskGroup subTaskGroup = getTaskExecutor().createSubTaskGroup("DeleteBackupYb", executor);
    for (Backup backup : backups) {
      DeleteBackupYb.Params params = new DeleteBackupYb.Params();
      params.backupUUID = backup.backupUUID;
      params.customerUUID = customerUUID;
      DeleteBackupYb task = createTask(DeleteBackupYb.class);
      task.initialize(params);
      subTaskGroup.addSubTask(task);
    }
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createEncryptedUniverseKeyBackupTask() {
    return createEncryptedUniverseKeyBackupTask((BackupTableParams) taskParams());
  }

  public SubTaskGroup createEncryptedUniverseKeyBackupTask(BackupTableParams params) {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("BackupUniverseKeys", executor);
    BackupUniverseKeys task = createTask(BackupUniverseKeys.class);
    task.initialize(params);
    task.setUserTaskUUID(userTaskUUID);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createEncryptedUniverseKeyRestoreTask(BackupTableParams params) {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("RestoreUniverseKeys", executor);
    RestoreUniverseKeys task = createTask(RestoreUniverseKeys.class);
    task.initialize(params);
    task.setUserTaskUUID(userTaskUUID);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createEncryptedUniverseKeyRestoreTaskYb(RestoreBackupParams params) {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("RestoreUniverseKeysYb", executor);
    RestoreUniverseKeysYb task = createTask(RestoreUniverseKeysYb.class);
    task.initialize(params);
    task.setUserTaskUUID(userTaskUUID);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createEncryptedUniverseKeyRestoreTaskYbc(RestoreBackupParams params) {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("RestoreUniverseKeysYbc", executor);
    RestoreUniverseKeysYbc task = createTask(RestoreUniverseKeysYbc.class);
    task.initialize(params);
    task.setUserTaskUUID(userTaskUUID);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Creates a task to upgrade desired ybc version on a universe.
   *
   * @param universeUUID universe on which ybc need to be upgraded
   * @param ybcVersion desired ybc version
   * @param validateOnlyMasterLeader flag to check only if master leader node's ybc is upgraded or
   *     not
   */
  public SubTaskGroup createUpgradeYbcTask(
      UUID universeUUID, String ybcVersion, boolean validateOnlyMasterLeader) {
    SubTaskGroup subTaskGroup = getTaskExecutor().createSubTaskGroup("UpgradeYbc", executor);
    UpgradeYbc task = createTask(UpgradeYbc.class);
    UpgradeYbc.Params params = new UpgradeYbc.Params();
    params.universeUUID = universeUUID;
    params.ybcVersion = ybcVersion;
    params.validateOnlyMasterLeader = validateOnlyMasterLeader;
    task.initialize(params);
    task.setUserTaskUUID(userTaskUUID);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Creates a task list to manipulate the DNS record available for this universe.
   *
   * @param eventType the type of manipulation to do on the DNS records.
   * @param isForceDelete if this is a delete operation, set this to true to ignore errors
   * @param intent universe information.
   * @return subtask group
   */
  public SubTaskGroup createDnsManipulationTask(
      DnsManager.DnsCommandType eventType,
      boolean isForceDelete,
      UniverseDefinitionTaskParams.UserIntent intent) {
    SubTaskGroup subTaskGroup = getTaskExecutor().createSubTaskGroup("UpdateDnsEntry", executor);
    Provider p = Provider.getOrBadRequest(UUID.fromString(intent.provider));
    if (!p.getCloudCode().isHostedZoneEnabled()) {
      return subTaskGroup;
    }
    // TODO: shared constant with javascript land?
    String hostedZoneId = p.getHostedZoneId();
    if (hostedZoneId == null || hostedZoneId.isEmpty()) {
      return subTaskGroup;
    }
    ManipulateDnsRecordTask.Params params = new ManipulateDnsRecordTask.Params();
    params.universeUUID = taskParams().universeUUID;
    params.type = eventType;
    params.providerUUID = UUID.fromString(intent.provider);
    params.hostedZoneId = hostedZoneId;
    params.domainNamePrefix =
        String.format("%s.%s", intent.universeName, Customer.get(p.customerUUID).code);
    params.isForceDelete = isForceDelete;
    // Create the task to update DNS entries.
    ManipulateDnsRecordTask task = createTask(ManipulateDnsRecordTask.class);
    task.initialize(params);
    // Add it to the task list.
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Creates a task list to update the placement information by making a call to the master leader
   * and adds it to the task queue.
   *
   * @param blacklistNodes list of nodes which are being removed.
   */
  public SubTaskGroup createPlacementInfoTask(Collection<NodeDetails> blacklistNodes) {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("UpdatePlacementInfo", executor);
    UpdatePlacementInfo.Params params = new UpdatePlacementInfo.Params();
    // Add the universe uuid.
    params.universeUUID = taskParams().universeUUID;
    // Set the blacklist nodes if any are passed in.
    if (blacklistNodes != null && !blacklistNodes.isEmpty()) {
      Set<String> blacklistNodeNames = new HashSet<>();
      for (NodeDetails node : blacklistNodes) {
        blacklistNodeNames.add(node.nodeName);
      }
      params.blacklistNodes = blacklistNodeNames;
    }
    // Create the task to update placement info.
    UpdatePlacementInfo task = createTask(UpdatePlacementInfo.class);
    task.initialize(params);
    task.setUserTaskUUID(userTaskUUID);
    // Add it to the task list.
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Creates a task to move the data out of blacklisted servers.
   *
   * @return the created task group.
   */
  public SubTaskGroup createWaitForDataMoveTask() {
    SubTaskGroup subTaskGroup = getTaskExecutor().createSubTaskGroup("WaitForDataMove", executor);
    WaitForDataMove.Params params = new WaitForDataMove.Params();
    params.universeUUID = taskParams().universeUUID;
    // Create the task.
    WaitForDataMove waitForMove = createTask(WaitForDataMove.class);
    waitForMove.initialize(params);
    // Add it to the task list.
    subTaskGroup.addSubTask(waitForMove);
    // Add the task list to the task queue.
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createWaitForLeaderBlacklistCompletionTask(int waitTimeMs) {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("WaitForLeaderBlacklistCompletion", executor);
    WaitForLeaderBlacklistCompletion.Params params = new WaitForLeaderBlacklistCompletion.Params();
    params.universeUUID = taskParams().universeUUID;
    params.waitTimeMs = waitTimeMs;
    // Create the task.
    WaitForLeaderBlacklistCompletion leaderBlacklistCompletion =
        createTask(WaitForLeaderBlacklistCompletion.class);
    leaderBlacklistCompletion.initialize(params);
    // Add it to the task list.
    subTaskGroup.addSubTask(leaderBlacklistCompletion);
    // Add the task list to the task queue.
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /** Creates a task to wait for leaders to be on preferred regions only. */
  public void createWaitForLeadersOnPreferredOnlyTask() {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("WaitForLeadersOnPreferredOnly", executor);
    WaitForLeadersOnPreferredOnly.Params params = new WaitForLeadersOnPreferredOnly.Params();
    params.universeUUID = taskParams().universeUUID;
    // Create the task.
    WaitForLeadersOnPreferredOnly waitForLeadersOnPreferredOnly =
        createTask(WaitForLeadersOnPreferredOnly.class);
    waitForLeadersOnPreferredOnly.initialize(params);
    // Add it to the task list.
    subTaskGroup.addSubTask(waitForLeadersOnPreferredOnly);
    // Add the task list to the task queue.
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    // Set the subgroup task type.
    subTaskGroup.setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.WaitForDataMigration);
  }

  /**
   * Creates a task to move the data onto any lesser loaded servers.
   *
   * @return the created task group.
   */
  public SubTaskGroup createWaitForLoadBalanceTask() {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("WaitForLoadBalance", executor);
    WaitForLoadBalance.Params params = new WaitForLoadBalance.Params();
    params.universeUUID = taskParams().universeUUID;
    // Create the task.
    WaitForLoadBalance waitForLoadBalance = createTask(WaitForLoadBalance.class);
    waitForLoadBalance.initialize(params);
    // Add it to the task list.
    subTaskGroup.addSubTask(waitForLoadBalance);
    // Add the task list to the task queue.
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Creates a task to remove a node from blacklist on server.
   *
   * @param nodes The nodes that have to be removed from the blacklist.
   * @param isAdd true if the node are added to server blacklist, else removed.
   * @param isLeaderBlacklist true if we are leader blacklisting the node
   * @return the created task group.
   */
  public SubTaskGroup createModifyBlackListTask(
      Collection<NodeDetails> nodes, boolean isAdd, boolean isLeaderBlacklist) {
    if (isAdd) {
      return createModifyBlackListTask(nodes, null, isLeaderBlacklist);
    }
    return createModifyBlackListTask(null, nodes, isLeaderBlacklist);
  }

  /**
   * Creates a task to add/remove nodes from blacklist on server.
   *
   * @param addNodes The nodes that have to be added to the blacklist.
   * @param removeNodes The nodes that have to be removed from the blacklist.
   * @param isLeaderBlacklist true if we are leader blacklisting the node
   */
  public SubTaskGroup createModifyBlackListTask(
      Collection<NodeDetails> addNodes,
      Collection<NodeDetails> removeNodes,
      boolean isLeaderBlacklist) {
    SubTaskGroup subTaskGroup = getTaskExecutor().createSubTaskGroup("ModifyBlackList", executor);
    ModifyBlackList.Params params = new ModifyBlackList.Params();
    params.universeUUID = taskParams().universeUUID;
    params.addNodes = addNodes;
    params.removeNodes = removeNodes;
    params.isLeaderBlacklist = isLeaderBlacklist;
    // Create the task.
    ModifyBlackList modifyBlackList = createTask(ModifyBlackList.class);
    modifyBlackList.initialize(params);
    // Add it to the task list.
    subTaskGroup.addSubTask(modifyBlackList);
    // Add the task list to the task queue.
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  // Subtask to update gflags in memory.
  public SubTaskGroup createSetFlagInMemoryTasks(
      Collection<NodeDetails> nodes,
      ServerType serverType,
      boolean force,
      Map<String, String> gflags,
      boolean updateMasterAddrs) {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("InMemoryGFlagUpdate", executor);
    for (NodeDetails node : nodes) {
      // Create the task params.
      SetFlagInMemory.Params params = new SetFlagInMemory.Params();
      // Add the node name.
      params.nodeName = node.nodeName;
      // Add the universe uuid.
      params.universeUUID = taskParams().universeUUID;
      // The server type for the flag.
      params.serverType = serverType;
      // If the flags need to be force updated.
      params.force = force;
      // The flags to update.
      params.gflags = gflags;
      // If only master addresses need to be updated.
      params.updateMasterAddrs = updateMasterAddrs;

      // Create the task.
      SetFlagInMemory setFlag = createTask(SetFlagInMemory.class);
      setFlag.initialize(params);
      // Add it to the task list.
      subTaskGroup.addSubTask(setFlag);
    }
    // Add the task list to the task queue.
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Creates a task list to wait for a minimum number of tservers to heartbeat to the master leader.
   */
  public SubTaskGroup createWaitForTServerHeartBeatsTask() {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("WaitForTServerHeartBeats", executor);
    WaitForTServerHeartBeats task = createTask(WaitForTServerHeartBeats.class);
    WaitForTServerHeartBeats.Params params = new WaitForTServerHeartBeats.Params();
    params.universeUUID = taskParams().universeUUID;
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public void createConfigureUniverseTasks(Cluster primaryCluster) {
    // Wait for a Master Leader to be elected.
    createWaitForMasterLeaderTask().setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

    // Persist the placement info into the YB master leader.
    createPlacementInfoTask(null /* blacklistNodes */)
        .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

    // Manage encryption at rest
    SubTaskGroup manageEncryptionKeyTask = createManageEncryptionAtRestTask();
    if (manageEncryptionKeyTask != null) {
      manageEncryptionKeyTask.setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
    }

    // Wait for a master leader to hear from all the tservers.
    createWaitForTServerHeartBeatsTask().setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

    // Update the DNS entry for all the nodes once, using the primary cluster type.
    createDnsManipulationTask(DnsManager.DnsCommandType.Create, false, primaryCluster.userIntent)
        .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

    // Update the swamper target file.
    createSwamperTargetUpdateTask(false /* removeFile */);

    // Create alert definitions.
    createUnivCreateAlertDefinitionsTask().setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

    // Create default redis table.
    checkAndCreateRedisTableTask(primaryCluster);

    // Create read write test table tasks.
    checkAndCreateReadWriteTestTableTask(primaryCluster);

    // Change admin password for Admin user, as specified.
    checkAndCreateChangeAdminPasswordTask(primaryCluster);

    // Marks the update of this universe as a success only if all the tasks before it succeeded.
    createMarkUniverseUpdateSuccessTasks().setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
  }

  // Check if the node present in taskParams has a backing instance alive on the IaaS.
  public static boolean instanceExists(NodeTaskParams taskParams) {
    ImmutableMap.Builder<String, String> expectedTags = ImmutableMap.builder();
    Universe universe = Universe.getOrBadRequest(taskParams.universeUUID);
    NodeDetails node = universe.getNodeOrBadRequest(taskParams.getNodeName());
    Cluster cluster = universe.getCluster(node.placementUuid);
    if (cluster.userIntent.providerType != CloudType.onprem) {
      expectedTags.put("universe_uuid", taskParams.universeUUID.toString());
      if (taskParams.nodeUuid == null) {
        taskParams.nodeUuid = node.nodeUuid;
      }
      if (taskParams.nodeUuid != null) {
        expectedTags.put("node_uuid", taskParams.nodeUuid.toString());
      }
    }
    Optional<Boolean> optional = instanceExists(taskParams, expectedTags.build());
    if (!optional.isPresent()) {
      return false;
    }
    if (optional.get()) {
      return true;
    }
    // False means not matching the expected tags.
    throw new RuntimeException(
        String.format("Node %s already exist. Pick different universe name.", taskParams.nodeName));
  }

  // It returns 3 states - empty for not found, false for not matching and true for matching.
  public static Optional<Boolean> instanceExists(
      NodeTaskParams taskParams, Map<String, String> expectedTags) {
    NodeManager nodeManager = Play.current().injector().instanceOf(NodeManager.class);
    log.info("Expected tags: {}", expectedTags);
    ShellResponse response =
        nodeManager.nodeCommand(NodeManager.NodeCommandType.List, taskParams).processErrors();
    if (Strings.isNullOrEmpty(response.message)) {
      // Instance does not exist.
      return Optional.empty();
    }
    if (MapUtils.isEmpty(expectedTags)) {
      return Optional.of(true);
    }
    JsonNode jsonNode = Json.parse(response.message);
    if (jsonNode.isArray()) {
      jsonNode = jsonNode.get(0);
    }
    long matchCount =
        Streams.stream(jsonNode.fields())
            .filter(
                e -> {
                  String expectedTagValue = expectedTags.get(e.getKey());
                  if (expectedTagValue == null) {
                    return false;
                  }
                  log.info(
                      "Node: {}, Key: {}, Value: {}, Expected: {}",
                      taskParams.nodeName,
                      e.getKey(),
                      e.getValue(),
                      expectedTagValue);
                  return expectedTagValue.equals(e.getValue().asText());
                })
            .limit(expectedTags.size())
            .count();
    return Optional.of(matchCount == expectedTags.size());
  }

  // Perform preflight checks on the given node.
  public String performPreflightCheck(
      Cluster cluster,
      NodeDetails currentNode,
      @Nullable UUID rootCA,
      @Nullable UUID clientRootCA) {
    if (cluster.userIntent.providerType != com.yugabyte.yw.commissioner.Common.CloudType.onprem) {
      return null;
    }
    NodeTaskParams preflightTaskParams = new NodeTaskParams();
    UserIntent userIntent = cluster.userIntent;
    preflightTaskParams.nodeName = currentNode.nodeName;
    preflightTaskParams.nodeUuid = currentNode.nodeUuid;
    preflightTaskParams.deviceInfo = userIntent.deviceInfo;
    preflightTaskParams.azUuid = currentNode.azUuid;
    preflightTaskParams.universeUUID = taskParams().universeUUID;
    preflightTaskParams.rootCA = rootCA;
    preflightTaskParams.clientRootCA = clientRootCA;
    UniverseTaskParams.CommunicationPorts.exportToCommunicationPorts(
        preflightTaskParams.communicationPorts, currentNode);
    preflightTaskParams.extraDependencies.installNodeExporter =
        taskParams().extraDependencies.installNodeExporter;
    // Create the process to fetch information about the node from the cloud provider.
    NodeManager nodeManager = Play.current().injector().instanceOf(NodeManager.class);
    log.info("Running preflight checks for node {}.", preflightTaskParams.nodeName);
    ShellResponse response =
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Precheck, preflightTaskParams);
    if (response.code == 0) {
      JsonNode responseJson = Json.parse(response.message);
      for (JsonNode nodeContent : responseJson) {
        if (!nodeContent.isBoolean() || !nodeContent.asBoolean()) {
          String errString =
              "Failed preflight checks for node "
                  + preflightTaskParams.nodeName
                  + ":\n"
                  + response.message;
          log.error(errString);
          return response.message;
        }
      }
    }
    return null;
  }

  private boolean isServerAlive(NodeDetails node, ServerType server, String masterAddrs) {
    YBClientService ybService = Play.current().injector().instanceOf(YBClientService.class);

    Universe universe = Universe.getOrBadRequest(taskParams().universeUUID);
    String certificate = universe.getCertificateNodetoNode();
    YBClient client = ybService.getClient(masterAddrs, certificate);
    try {
      HostAndPort hp =
          HostAndPort.fromParts(
              node.cloudInfo.private_ip,
              server == ServerType.MASTER ? node.masterRpcPort : node.tserverRpcPort);
      return client.waitForServer(hp, 5000);
    } finally {
      ybService.closeClient(client, masterAddrs);
    }
  }

  public boolean isMasterAliveOnNode(NodeDetails node, String masterAddrs) {
    if (!node.isMaster) {
      return false;
    }
    return isServerAlive(node, ServerType.MASTER, masterAddrs);
  }

  public boolean isTserverAliveOnNode(NodeDetails node, String masterAddrs) {
    return isServerAlive(node, ServerType.TSERVER, masterAddrs);
  }

  // Helper API to update the db for the node with the given state.
  public void setNodeState(String nodeName, NodeDetails.NodeState state) {
    // Persist the desired node information into the DB.
    UniverseUpdater updater =
        nodeStateUpdater(
            taskParams().universeUUID, nodeName, NodeStatus.builder().nodeState(state).build());
    saveUniverseDetails(updater);
  }

  // Return list of nodeNames from the given set of node details.
  public String nodeNames(Collection<NodeDetails> nodes) {
    String nodeNames = "";
    for (NodeDetails node : nodes) {
      nodeNames += node.nodeName + ",";
    }
    return nodeNames.substring(0, nodeNames.length() - 1);
  }

  /** Disable the loadbalancer to not move data. Used during rolling upgrades. */
  public SubTaskGroup createLoadBalancerStateChangeTask(boolean enable) {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("LoadBalancerStateChange", executor);
    LoadBalancerStateChange.Params params = new LoadBalancerStateChange.Params();
    // Add the universe uuid.
    params.universeUUID = taskParams().universeUUID;
    params.enable = enable;
    LoadBalancerStateChange task = createTask(LoadBalancerStateChange.class);
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createResetUniverseVersionTask() {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("ResetUniverseVersion", executor);
    ResetUniverseVersion task = createTask(ResetUniverseVersion.class);
    task.initialize(taskParams());
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  // Update the Universe's 'backupInProgress' flag to new state.
  private void updateBackupState(boolean state) {
    UniverseUpdater updater =
        new UniverseUpdater() {
          @Override
          public void run(Universe universe) {
            UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
            universeDetails.backupInProgress = state;
            universe.setUniverseDetails(universeDetails);
          }
        };
    if (state) {
      // New state is to set backupInProgress to true.
      // This method increments universe version if HA is enabled.
      saveUniverseDetails(updater);
    } else {
      // New state is to set backupInProgress to false.
      // This method simply updates the backupInProgress without changing the universe version.
      // This is called at the end of backup to release the universe for other tasks.
      Universe.saveDetails(taskParams().universeUUID, updater, false);
    }
  }

  // Update the Universe's 'backupInProgress' flag to new state.
  // It throws exception if the universe is already being locked by another task.
  public void lockedUpdateBackupState(boolean newState) {
    checkNotNull(taskParams().universeUUID, "Universe UUID must be set.");
    if (Universe.getOrBadRequest(taskParams().universeUUID).getUniverseDetails().backupInProgress
        == newState) {
      if (newState) {
        throw new IllegalStateException("A backup for this universe is already in progress.");
      } else {
        return;
      }
    }
    updateBackupState(newState);
  }

  /**
   * Whether to increment the universe/cluster config version. Skip incrementing version if the task
   * updating the universe metadata is create/destroy/pause/resume universe. Also, skip incrementing
   * version if task must manually handle version incrementing (such as in the case of XCluster).
   *
   * @return true if we should increment the version, false otherwise
   */
  protected boolean shouldIncrementVersion(UUID universeUuid) {
    Optional<Universe> universe = Universe.maybeGet(universeUuid);
    if (!universe.isPresent()) {
      return false;
    }

    final VersionCheckMode mode =
        runtimeConfigFactory
            .forUniverse(universe.get())
            .getEnum(VersionCheckMode.class, "yb.universe_version_check_mode");

    if (mode == VersionCheckMode.NEVER) {
      return false;
    }

    if (mode == VersionCheckMode.HA_ONLY && !HighAvailabilityConfig.get().isPresent()) {
      return false;
    }

    // For create/destroy/pause/resume operations, do not attempt to bump up
    // the cluster config version on the leader master because the cluster
    // and the leader master may not be available at the time we are attempting to do this.
    if (userTaskUUID == null) {
      return false;
    }

    TaskInfo taskInfo = TaskInfo.get(userTaskUUID);
    if (taskInfo == null) {
      return false;
    }

    TaskType taskType = taskInfo.getTaskType();
    return !(taskType == TaskType.CreateUniverse
        || taskType == TaskType.CreateKubernetesUniverse
        || taskType == TaskType.DestroyUniverse
        || taskType == TaskType.DestroyKubernetesUniverse
        || taskType == TaskType.PauseUniverse
        || taskType == TaskType.ResumeUniverse
        || taskType == TaskType.CreateXClusterConfig
        || taskType == TaskType.EditXClusterConfig
        || taskType == TaskType.SyncXClusterConfig
        || taskType == TaskType.DeleteXClusterConfig);
  }

  // TODO: Use of synchronized in static scope! Looks suspicious.
  //  Use of transactions may be better.
  private static synchronized int getClusterConfigVersion(Universe universe) {
    final YBClientService ybService = Play.current().injector().instanceOf(YBClientService.class);
    final String hostPorts = universe.getMasterAddresses();
    final String certificate = universe.getCertificateNodetoNode();
    int version;
    YBClient client = ybService.getClient(hostPorts, certificate);
    try {
      version = client.getMasterClusterConfig().getConfig().getVersion();
    } catch (Exception e) {
      log.error("Error occurred retrieving cluster config version", e);
      throw new RuntimeException("Error incrementing cluster config version", e);
    } finally {
      ybService.closeClient(client, hostPorts);
    }
    return version;
  }

  // TODO: Use of synchronized in static scope! Looks suspicious.
  //  Use of transactions may be better.
  private static synchronized boolean versionsMatch(UUID universeUUID) {
    Universe universe = Universe.getOrBadRequest(universeUUID);
    final int clusterConfigVersion = UniverseTaskBase.getClusterConfigVersion(universe);

    // For backwards compatibility (see V56__Alter_Universe_Version.sql)
    if (universe.version == -1) {
      universe.version = clusterConfigVersion;
      log.info(
          "Updating version for universe {} from -1 to cluster config version {}",
          universeUUID,
          universe.version);
      universe.save();
    }

    return universe.version == clusterConfigVersion;
  }

  /**
   * checkUniverseVersion
   *
   * @param universeUUID
   *     <p>Check that the universe version in the Platform database matches the one in the cluster
   *     config on the yugabyte db master. A mismatch could indicate one of two issues: 1. Multiple
   *     Platform replicas in a HA config are operating on the universe and (async) replication has
   *     failed to sychronize Platform db state correctly across different Platforms. We want to
   *     flag this case. 2. Manual yb-admin operations on the cluster have bumped up the database
   *     cluster config version. This is not necessarily always a problem, so we choose to ignore
   *     this case for now. When we get to a point where manual yb-admin operations are never
   *     needed, we can consider flagging this case. For now, we will let the universe version on
   *     Platform and the cluster config version on the master diverge.
   * @param mode
   */
  private static void checkUniverseVersion(UUID universeUUID, VersionCheckMode mode) {
    if (mode == VersionCheckMode.NEVER) {
      return;
    }

    if (mode == VersionCheckMode.HA_ONLY && !HighAvailabilityConfig.get().isPresent()) {
      log.debug("Skipping cluster config version check for universe {}", universeUUID);
      return;
    }

    if (!versionsMatch(universeUUID)) {
      throw new RuntimeException("Universe version does not match cluster config version");
    }
  }

  protected void checkUniverseVersion() {
    UniverseTaskBase.checkUniverseVersion(
        taskParams().universeUUID,
        runtimeConfigFactory
            .forUniverse(getUniverse())
            .getEnum(VersionCheckMode.class, "yb.universe_version_check_mode"));
  }

  /** Increment the cluster config version */
  private static synchronized void incrementClusterConfigVersion(UUID universeUUID) {
    Universe universe = Universe.getOrBadRequest(universeUUID);
    YBClientService ybService = Play.current().injector().instanceOf(YBClientService.class);
    final String hostPorts = universe.getMasterAddresses();
    String certificate = universe.getCertificateNodetoNode();
    YBClient client = ybService.getClient(hostPorts, certificate);
    try {
      int version = universe.version;
      ModifyClusterConfigIncrementVersion modifyConfig =
          new ModifyClusterConfigIncrementVersion(client, version);
      int newVersion = modifyConfig.incrementVersion();
      log.info(
          "Updated cluster config version for universe {} from {} to {}",
          universeUUID,
          version,
          newVersion);
    } catch (Exception e) {
      log.error(
          "Error occurred incrementing cluster config version for universe " + universeUUID, e);
      throw new RuntimeException("Error incrementing cluster config version", e);
    } finally {
      ybService.closeClient(client, hostPorts);
    }
  }

  /**
   * Run universe updater and increment the cluster config version
   *
   * @param updater the universe updater to run
   * @return the updated universe
   */
  protected static Universe saveUniverseDetails(
      UUID universeUUID,
      boolean shouldIncrementVersion,
      UniverseUpdater updater,
      boolean checkExist) {
    Universe.UNIVERSE_KEY_LOCK.acquireLock(universeUUID);
    try {
      if (checkExist && !Universe.maybeGet(universeUUID).isPresent()) {
        return null;
      }
      if (shouldIncrementVersion) {
        incrementClusterConfigVersion(universeUUID);
      }
      return Universe.saveDetails(universeUUID, updater, shouldIncrementVersion);
    } finally {
      Universe.UNIVERSE_KEY_LOCK.releaseLock(universeUUID);
    }
  }

  protected Universe saveUniverseDetails(
      UUID universeUUID, UniverseUpdater updater, boolean checkExist) {
    return UniverseTaskBase.saveUniverseDetails(
        universeUUID, shouldIncrementVersion(universeUUID), updater, checkExist);
  }

  protected Universe saveUniverseDetails(UUID universeUUID, UniverseUpdater updater) {
    return UniverseTaskBase.saveUniverseDetails(
        universeUUID, shouldIncrementVersion(universeUUID), updater, false /* checkExist */);
  }

  protected Universe saveUniverseDetails(UniverseUpdater updater) {
    return saveUniverseDetails(taskParams().universeUUID, updater);
  }

  protected void preTaskActions() {
    Universe universe = Universe.getOrBadRequest(taskParams().universeUUID);
    preTaskActions(universe);
  }

  // Use this if it is already in transaction or the field changes are not yet written to the DB.
  protected void preTaskActions(Universe universe) {
    HealthChecker healthChecker = Play.current().injector().instanceOf(HealthChecker.class);
    UniverseDefinitionTaskParams details = universe.getUniverseDetails();
    if ((details != null) && details.updateInProgress) {
      log.debug("Cancelling any active health-checks for universe {}", universe.universeUUID);
      healthChecker.cancelHealthCheck(universe.universeUUID);
    }
  }

  protected SubTaskGroup createRebootTasks(List<NodeDetails> nodes) {
    SubTaskGroup subTaskGroup = getTaskExecutor().createSubTaskGroup("RebootServer", executor);
    for (NodeDetails node : nodes) {

      RebootServer.Params params = new RebootServer.Params();
      params.nodeName = node.nodeName;
      params.universeUUID = taskParams().universeUUID;
      params.azUuid = node.azUuid;

      RebootServer task = createTask(RebootServer.class);
      task.initialize(params);

      subTaskGroup.addSubTask(task);
      getRunnableTask().addSubTaskGroup(subTaskGroup);
    }
    return subTaskGroup;
  }

  public int getSleepTimeForProcess(ServerType processType) {
    return processType == ServerType.MASTER
        ? taskParams().sleepAfterMasterRestartMillis
        : taskParams().sleepAfterTServerRestartMillis;
  }

  // XCluster: All the xCluster related code resides in this section.
  // --------------------------------------------------------------------------------
  protected SubTaskGroup createDeleteReplicationTask(
      XClusterConfig xClusterConfig, boolean ignoreErrors) {
    SubTaskGroup subTaskGroup = getTaskExecutor().createSubTaskGroup("DeleteReplication", executor);
    DeleteReplication.Params deleteReplicationParams = new DeleteReplication.Params();
    deleteReplicationParams.universeUUID = xClusterConfig.targetUniverseUUID;
    deleteReplicationParams.xClusterConfig = xClusterConfig;
    deleteReplicationParams.ignoreErrors = ignoreErrors;

    DeleteReplication task = createTask(DeleteReplication.class);
    task.initialize(deleteReplicationParams);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  protected SubTaskGroup createDeleteBootstrapIdsTask(
      XClusterConfig xClusterConfig, boolean forceDelete) {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("DeleteBootstrapIds", executor);
    DeleteBootstrapIds.Params deleteBootstrapIdsParams = new DeleteBootstrapIds.Params();
    deleteBootstrapIdsParams.universeUUID = xClusterConfig.sourceUniverseUUID;
    deleteBootstrapIdsParams.xClusterConfig = xClusterConfig;
    deleteBootstrapIdsParams.forceDelete = forceDelete;

    DeleteBootstrapIds task = createTask(DeleteBootstrapIds.class);
    task.initialize(deleteBootstrapIdsParams);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  protected SubTaskGroup createDeleteXClusterConfigEntryTask(XClusterConfig xClusterConfig) {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("DeleteXClusterConfigEntry", executor);
    XClusterConfigTaskParams deleteXClusterConfigEntryParams = new XClusterConfigTaskParams();
    deleteXClusterConfigEntryParams.universeUUID = xClusterConfig.targetUniverseUUID;
    deleteXClusterConfigEntryParams.xClusterConfig = xClusterConfig;

    DeleteXClusterConfigEntry task = createTask(DeleteXClusterConfigEntry.class);
    task.initialize(deleteXClusterConfigEntryParams);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  protected SubTaskGroup createResetXClusterConfigEntryTask(XClusterConfig xClusterConfig) {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("ResetXClusterConfigEntry", executor);
    XClusterConfigTaskParams resetXClusterConfigEntryParams = new XClusterConfigTaskParams();
    resetXClusterConfigEntryParams.universeUUID = xClusterConfig.targetUniverseUUID;
    resetXClusterConfigEntryParams.xClusterConfig = xClusterConfig;

    ResetXClusterConfigEntry task = createTask(ResetXClusterConfigEntry.class);
    task.initialize(resetXClusterConfigEntryParams);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  protected SubTaskGroup createTransferXClusterCertsRemoveTasks(
      XClusterConfig xClusterConfig, File sourceRootCertDirPath, boolean ignoreErrors) {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("TransferXClusterCerts", executor);
    Universe targetUniverse = Universe.getOrBadRequest(xClusterConfig.targetUniverseUUID);
    Collection<NodeDetails> nodes = targetUniverse.getNodes();
    for (NodeDetails node : nodes) {
      TransferXClusterCerts.Params transferParams = new TransferXClusterCerts.Params();
      transferParams.universeUUID = targetUniverse.universeUUID;
      transferParams.nodeName = node.nodeName;
      transferParams.azUuid = node.azUuid;
      transferParams.action = TransferXClusterCerts.Params.Action.REMOVE;
      transferParams.replicationGroupName = xClusterConfig.getReplicationGroupName();
      transferParams.producerCertsDirOnTarget = sourceRootCertDirPath;
      transferParams.ignoreErrors = ignoreErrors;

      TransferXClusterCerts transferXClusterCertsTask = createTask(TransferXClusterCerts.class);
      transferXClusterCertsTask.initialize(transferParams);
      subTaskGroup.addSubTask(transferXClusterCertsTask);
    }
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  protected SubTaskGroup createTransferXClusterCertsRemoveTasks(
      XClusterConfig xClusterConfig, File sourceRootCertDirPath) {
    return createTransferXClusterCertsRemoveTasks(
        xClusterConfig, sourceRootCertDirPath, false /* ignoreErrors */);
  }

  protected SubTaskGroup createTransferXClusterCertsRemoveTasks(XClusterConfig xClusterConfig) {
    return createTransferXClusterCertsRemoveTasks(
        xClusterConfig,
        Universe.getOrBadRequest(xClusterConfig.targetUniverseUUID)
            .getUniverseDetails()
            .getSourceRootCertDirPath());
  }

  protected void createDeleteXClusterConfigSubtasks(
      XClusterConfig xClusterConfig, boolean keepEntry) {
    // Delete the replication CDC streams on the target universe.
    createDeleteReplicationTask(xClusterConfig, true /* ignoreErrors */)
        .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.DeleteXClusterReplication);

    // Delete bootstrap IDs created by bootstrap universe subtask.
    // forceDelete is true to prevent errors until the user can choose if they want forceDelete.
    createDeleteBootstrapIdsTask(xClusterConfig, true /* forceDelete */)
        .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.DeleteXClusterReplication);

    // If target universe is destroyed, ignore creating this subtask.
    if (xClusterConfig.targetUniverseUUID != null) {
      File sourceRootCertDirPath =
          Universe.getOrBadRequest(xClusterConfig.targetUniverseUUID)
              .getUniverseDetails()
              .getSourceRootCertDirPath();
      // Delete the source universe root cert from the target universe if it is transferred.
      if (sourceRootCertDirPath != null) {
        createTransferXClusterCertsRemoveTasks(
                xClusterConfig,
                sourceRootCertDirPath,
                xClusterConfig.status == XClusterConfig.XClusterConfigStatusType.DeletedUniverse)
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.DeleteXClusterReplication);
      }
    }

    if (keepEntry) {
      createResetXClusterConfigEntryTask(xClusterConfig)
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.DeleteXClusterReplication);
    } else {
      // Delete the xCluster config from DB.
      createDeleteXClusterConfigEntryTask(xClusterConfig)
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.DeleteXClusterReplication);
    }
  }

  protected void createDeleteXClusterConfigSubtasks(XClusterConfig xClusterConfig) {
    createDeleteXClusterConfigSubtasks(xClusterConfig, false /* keepEntry */);
  }

  /**
   * It updates the source master addresses on the target universe cluster config for all xCluster
   * configs on the source universe.
   */
  public void createXClusterConfigUpdateMasterAddressesTask() {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("XClusterConfigUpdateMasterAddresses", executor);
    List<XClusterConfig> xClusterConfigs =
        XClusterConfig.getBySourceUniverseUUID(taskParams().universeUUID)
            .stream()
            .filter(xClusterConfig -> !XClusterConfigTaskBase.isInMustDeleteStatus(xClusterConfig))
            .collect(Collectors.toList());
    Set<UUID> updatedTargetUniverses = new HashSet<>();
    for (XClusterConfig config : xClusterConfigs) {
      UUID targetUniverseUUID = config.targetUniverseUUID;
      // Each target universe needs to be updated only once, even though there could be several
      // xCluster configs between each source and target universe pair.
      if (updatedTargetUniverses.contains(targetUniverseUUID)) {
        continue;
      }
      updatedTargetUniverses.add(targetUniverseUUID);

      XClusterConfigUpdateMasterAddresses.Params params =
          new XClusterConfigUpdateMasterAddresses.Params();
      // Set the target universe UUID to be told the new master addresses.
      params.universeUUID = targetUniverseUUID;
      // Set the source universe UUID to get the new master addresses.
      params.sourceUniverseUuid = taskParams().universeUUID;

      XClusterConfigUpdateMasterAddresses task =
          createTask(XClusterConfigUpdateMasterAddresses.class);
      task.initialize(params);
      task.setUserTaskUUID(userTaskUUID);
      // Add it to the task list.
      subTaskGroup.addSubTask(task);
    }
    if (subTaskGroup.getSubTaskCount() > 0) {
      getRunnableTask().addSubTaskGroup(subTaskGroup);
    }
  }

  protected SubTaskGroup createTransferXClusterCertsCopyTasks(
      Collection<NodeDetails> nodes,
      String replicationGroupName,
      File certificate,
      File sourceRootCertDirPath) {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("TransferXClusterCerts", executor);
    for (NodeDetails node : nodes) {
      TransferXClusterCerts.Params transferParams = new TransferXClusterCerts.Params();
      transferParams.universeUUID = taskParams().universeUUID;
      transferParams.nodeName = node.nodeName;
      transferParams.azUuid = node.azUuid;
      transferParams.rootCertPath = certificate;
      transferParams.action = TransferXClusterCerts.Params.Action.COPY;
      transferParams.replicationGroupName = replicationGroupName;
      transferParams.producerCertsDirOnTarget = sourceRootCertDirPath;

      TransferXClusterCerts transferXClusterCertsTask = createTask(TransferXClusterCerts.class);
      transferXClusterCertsTask.initialize(transferParams);
      subTaskGroup.addSubTask(transferXClusterCertsTask);
    }
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  protected SubTaskGroup createXClusterInfoPersistTask(
      UniverseDefinitionTaskParams.XClusterInfo xClusterInfo) {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("XClusterInfoPersist", executor);
    XClusterInfoPersist.Params xClusterInfoPersistParams = new XClusterInfoPersist.Params();
    xClusterInfoPersistParams.universeUUID = taskParams().universeUUID;
    xClusterInfoPersistParams.xClusterInfo = xClusterInfo;

    XClusterInfoPersist task = createTask(XClusterInfoPersist.class);
    task.initialize(xClusterInfoPersistParams);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  protected SubTaskGroup createXClusterInfoPersistTask() {
    return createXClusterInfoPersistTask(getUniverse().getUniverseDetails().xClusterInfo);
  }

  protected void unlockXClusterUniverses(
      Set<UUID> lockedXClusterUniversesUuidSet, boolean ignoreErrors) {
    if (lockedXClusterUniversesUuidSet == null) {
      return;
    }
    Exception firstException = null;
    for (UUID universeUuid : lockedXClusterUniversesUuidSet) {
      try {
        // Unlock the universe.
        unlockUniverseForUpdate(universeUuid);
      } catch (Exception e) {
        // Log the error message, and continue to unlock as many universes as possible.
        log.error(
            "{} hit error : could not unlock universe {} that was locked because of "
                + "participating in an XCluster config: {}",
            getName(),
            universeUuid,
            e.getMessage());
        if (firstException == null) {
          firstException = e;
        }
      }
    }
    if (firstException != null) {
      if (!ignoreErrors) {
        throw new RuntimeException(firstException);
      } else {
        log.debug("Error ignored");
      }
    }
  }
  // --------------------------------------------------------------------------------
  // End of XCluster.
}
