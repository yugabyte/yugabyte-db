// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.pitr;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;
import static play.mvc.Http.Status.NOT_FOUND;
import static play.mvc.Http.Status.TOO_MANY_REQUESTS;

import api.v2.utils.NormalizedPaginationSpec;
import com.google.common.cache.CacheBuilder;
import com.google.common.cache.CacheLoader;
import com.google.common.cache.LoadingCache;
import com.google.common.util.concurrent.UncheckedExecutionException;
import com.google.inject.Singleton;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.tasks.DeletePitrConfig;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.backuprestore.BackupUtil;
import com.yugabyte.yw.common.backuprestore.BackupUtil.ApiType;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.common.services.config.YbClientConfig;
import com.yugabyte.yw.common.services.config.YbClientConfigFactory;
import com.yugabyte.yw.forms.CreatePitrConfigParams;
import com.yugabyte.yw.forms.RestoreSnapshotScheduleParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.SoftwareUpgradeState;
import com.yugabyte.yw.forms.UpdatePitrConfigParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.DrConfig;
import com.yugabyte.yw.models.PitrConfig;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.helpers.TaskType;
import io.ebean.PagedList;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ConcurrentMap;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.locks.ReentrantLock;
import java.util.function.Function;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.CommonTypes.TableType;
import org.yb.client.ListSnapshotSchedulesResponse;
import org.yb.client.SnapshotScheduleInfo;
import org.yb.client.YBClient;
import org.yb.master.CatalogEntityInfo.SysSnapshotEntryPB.State;

@Slf4j
@Singleton
public class PitrConfigHelper {

  public static final String PITR_COMPATIBLE_DB_VERSION = "2.14.0.0-b1";

  private final Commissioner commissioner;
  private final YBClientService ybClientService;
  private final YbClientConfigFactory ybClientConfigFactory;
  private final RuntimeConfGetter confGetter;
  private final LoadingCache<UUID, Map<UUID, SnapshotScheduleInfo>> snapshotSchedulesCache;
  private final ConcurrentMap<UUID, ReentrantLock> universeLocks = new ConcurrentHashMap<>();

  @Inject
  public PitrConfigHelper(
      Commissioner commissioner,
      YBClientService ybClientService,
      YbClientConfigFactory ybClientConfigFactory,
      RuntimeConfGetter confGetter) {
    this.commissioner = commissioner;
    this.ybClientService = ybClientService;
    this.ybClientConfigFactory = ybClientConfigFactory;
    this.confGetter = confGetter;
    this.snapshotSchedulesCache = buildSnapshotSchedulesCache();
  }

  private LoadingCache<UUID, Map<UUID, SnapshotScheduleInfo>> buildSnapshotSchedulesCache() {
    long ttlMs = confGetter.getGlobalConf(GlobalConfKeys.pitrListSnapshotSchedulesCacheTtlMs);
    int maxUniverses =
        confGetter.getGlobalConf(GlobalConfKeys.pitrListSnapshotSchedulesCacheMaxUniverses);
    return CacheBuilder.newBuilder()
        .expireAfterWrite(ttlMs, TimeUnit.MILLISECONDS)
        .maximumSize(maxUniverses)
        .build(
            new CacheLoader<>() {
              @Override
              public Map<UUID, SnapshotScheduleInfo> load(UUID universeUuid) {
                Universe universe = Universe.getOrNotFound(universeUuid);
                return fetchSnapshotSchedulesFromMaster(universe);
              }
            });
  }

  public UUID createPitrConfig(
      UUID customerUUID,
      UUID universeUUID,
      String tableType,
      String keyspaceName,
      CreatePitrConfigParams taskParams) {
    // Validate customer UUID
    Customer customer = Customer.getOrBadRequest(customerUUID);

    // Validate universe UUID
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);
    if (universe.getUniverseDetails().universePaused) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot enable PITR when the universe is in paused state");
    } else if (universe.getUniverseDetails().updateInProgress) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot enable PITR when the universe is in locked state");
    }

    checkCompatibleYbVersion(
        universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion);

    if (!universe.getUniverseDetails().softwareUpgradeState.equals(SoftwareUpgradeState.Ready)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot enable PITR when the universe is not in ready state");
    }

    if (taskParams.retentionPeriodInSeconds <= 0L) {
      throw new PlatformServiceException(
          BAD_REQUEST, "PITR Config retention period cannot be less than 1 second");
    }

    if (taskParams.retentionPeriodInSeconds <= taskParams.intervalInSeconds) {
      throw new PlatformServiceException(
          BAD_REQUEST, "PITR Config interval cannot be less than retention period");
    }

    TableType type = BackupUtil.API_TYPE_TO_TABLE_TYPE_MAP.get(ApiType.valueOf(tableType));
    Optional<PitrConfig> pitrConfig = PitrConfig.maybeGet(universeUUID, type, keyspaceName);
    if (pitrConfig.isPresent()) {
      throw new PlatformServiceException(BAD_REQUEST, "PITR Config is already present");
    }

    BackupUtil.checkApiEnabled(type, universe.getUniverseDetails().getPrimaryCluster().userIntent);

    taskParams.setUniverseUUID(universeUUID);
    taskParams.customerUUID = customerUUID;
    taskParams.tableType = type;
    taskParams.keyspaceName = keyspaceName;
    UUID taskUUID = commissioner.submit(TaskType.CreatePitrConfig, taskParams);
    CustomerTask.create(
        customer,
        universeUUID,
        taskUUID,
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.CreatePitrConfig,
        universe.getName());
    return taskUUID;
  }

  public UUID restorePitrConfig(
      UUID customerUUID, UUID universeUUID, RestoreSnapshotScheduleParams taskParams) {
    log.info("Received restore PITR config request");

    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);

    checkCompatibleYbVersion(
        universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion);
    if (universe.getUniverseDetails().universePaused) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot perform PITR when the universe is in paused state");
    } else if (universe.getUniverseDetails().updateInProgress) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot perform PITR when the universe is in locked state");
    }

    if (!universe.getUniverseDetails().softwareUpgradeState.equals(SoftwareUpgradeState.Ready)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot perform PITR when the universe is not in ready state");
    }

    if (taskParams.restoreTimeInMillis <= 0L
        || taskParams.restoreTimeInMillis > System.currentTimeMillis()) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Time to restore specified is either negative or in the future");
    }
    PitrConfig.getOrBadRequest(taskParams.pitrConfigUUID);
    ListSnapshotSchedulesResponse scheduleResp;
    List<SnapshotScheduleInfo> scheduleInfoList;
    try (YBClient client = ybClientService.getUniverseClient(universe)) {
      scheduleResp = client.listSnapshotSchedules(taskParams.pitrConfigUUID);
      scheduleInfoList = scheduleResp.getSnapshotScheduleInfoList();
    } catch (Exception ex) {
      log.error(ex.getMessage());
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, ex.getMessage());
    }

    if (scheduleInfoList == null || scheduleInfoList.size() != 1) {
      throw new PlatformServiceException(BAD_REQUEST, "Snapshot schedule is invalid");
    }

    taskParams.setUniverseUUID(universeUUID);
    UUID taskUUID = commissioner.submit(TaskType.RestoreSnapshotSchedule, taskParams);
    CustomerTask.create(
        customer,
        universeUUID,
        taskUUID,
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.RestoreSnapshotSchedule,
        universe.getName());
    return taskUUID;
  }

  public UUID updatePitrConfig(
      UUID customerUUID,
      UUID universeUUID,
      UUID pitrConfigUUID,
      UpdatePitrConfigParams taskParams) {
    // Validate customer UUID
    Customer customer = Customer.getOrBadRequest(customerUUID);
    // Validate universe UUID
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);
    if (universe.getUniverseDetails().universePaused) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot update PITR when the universe is in paused state");
    } else if (universe.getUniverseDetails().updateInProgress) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot update PITR when the universe is in locked state");
    }

    if (!universe.getUniverseDetails().softwareUpgradeState.equals(SoftwareUpgradeState.Ready)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot update PITR when the universe is not in ready state");
    }

    PitrConfig pitrConfig = PitrConfig.getOrBadRequest(pitrConfigUUID);

    checkCompatibleYbVersion(
        universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion);

    if (taskParams.retentionPeriodInSeconds <= 0L) {
      throw new PlatformServiceException(
          BAD_REQUEST, "PITR Config retention period cannot be less than 1 second");
    }

    if (taskParams.retentionPeriodInSeconds <= taskParams.intervalInSeconds) {
      throw new PlatformServiceException(
          BAD_REQUEST, "PITR Config interval cannot be less than retention period");
    }

    if (taskParams.retentionPeriodInSeconds == pitrConfig.getRetentionPeriod()
        && taskParams.intervalInSeconds == pitrConfig.getScheduleInterval()) {
      throw new PlatformServiceException(BAD_REQUEST, "Nothing to update in the PITR config");
    }

    taskParams.setUniverseUUID(universeUUID);
    taskParams.customerUUID = customerUUID;
    taskParams.pitrConfigUUID = pitrConfig.getUuid();
    UUID taskUUID = commissioner.submit(TaskType.UpdatePitrConfig, taskParams);
    CustomerTask.create(
        customer,
        universeUUID,
        taskUUID,
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.UpdatePitrConfig,
        universe.getName());
    return taskUUID;
  }

  public UUID deletePitrConfig(UUID customerUUID, UUID universeUUID, UUID pitrConfigUUID) {
    // Validate customer UUID
    Customer customer = Customer.getOrBadRequest(customerUUID);
    // Validate universe UUID
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);
    checkCompatibleYbVersion(
        universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion);
    if (universe.getUniverseDetails().universePaused) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot delete PITR config when the universe is in paused state");
    } else if (universe.getUniverseDetails().updateInProgress) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot delete PITR config when the universe is in locked state");
    }
    PitrConfig pitrConfig = PitrConfig.getOrBadRequest(pitrConfigUUID);

    if (pitrConfig.isUsedForXCluster()) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "This PITR config is used for transactional xCluster and cannot be deleted; "
              + "to delete you need to first delete the related xCluster config");
    }

    DeletePitrConfig.Params deletePitrConfigParams = new DeletePitrConfig.Params();
    deletePitrConfigParams.setUniverseUUID(universeUUID);
    deletePitrConfigParams.pitrConfigUuid = pitrConfig.getUuid();

    UUID taskUUID = commissioner.submit(TaskType.DeletePitrConfig, deletePitrConfigParams);
    CustomerTask.create(
        customer,
        universeUUID,
        taskUUID,
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.DeletePitrConfig,
        universe.getName());
    return taskUUID;
  }

  public void checkCompatibleYbVersion(String ybVersion) {
    if (Util.compareYbVersions(ybVersion, PITR_COMPATIBLE_DB_VERSION, true) < 0) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "PITR feature not supported on universe DB version lower than "
              + PITR_COMPATIBLE_DB_VERSION);
    }
  }

  public List<PitrConfig> listPitrConfigs(Customer customer, Universe universe) {
    Universe.getOrBadRequest(universe.getUniverseUUID(), customer);
    checkCompatibleYbVersion(
        universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion);
    List<PitrConfig> pitrConfigs = PitrConfig.getByUniverseUUID(universe.getUniverseUUID());
    enrichPitrConfigs(universe, pitrConfigs);
    return pitrConfigs;
  }

  public PagedList<PitrConfig> listPitrConfigsPaged(
      Universe universe, NormalizedPaginationSpec normalized) {
    return listPitrConfigsPaged(universe, null, normalized);
  }

  public PagedList<PitrConfig> listPitrConfigsPaged(
      Universe universe, UUID drConfigUuid, NormalizedPaginationSpec normalized) {
    checkCompatibleYbVersion(
        universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion);
    if (drConfigUuid != null) {
      Customer customer = Customer.get(universe.getCustomerId());
      validateDrConfigOnUniverse(customer, universe.getUniverseUUID(), drConfigUuid);
    }
    PagedList<PitrConfig> paged =
        PitrConfig.getPagedListForUniverse(universe.getUniverseUUID(), drConfigUuid, normalized);
    enrichPitrConfigs(universe, paged.getList());
    return paged;
  }

  private void validateDrConfigOnUniverse(Customer customer, UUID universeUuid, UUID drConfigUuid) {
    DrConfig drConfig =
        DrConfig.maybeGet(drConfigUuid)
            .orElseThrow(
                () ->
                    new PlatformServiceException(
                        NOT_FOUND, "Cannot find drConfig with uuid " + drConfigUuid));
    drConfig
        .getXClusterConfigs()
        .forEach(
            xClusterConfig ->
                XClusterConfig.checkXClusterConfigInCustomer(
                    xClusterConfig,
                    customer,
                    NOT_FOUND,
                    "Could not find XCluster config %s for customer %s"));
    boolean onUniverse =
        drConfig.getXClusterConfigs().stream()
            .anyMatch(
                xClusterConfig ->
                    universeUuid.equals(xClusterConfig.getSourceUniverseUUID())
                        || universeUuid.equals(xClusterConfig.getTargetUniverseUUID()));
    if (!onUniverse) {
      throw new PlatformServiceException(
          NOT_FOUND,
          String.format("DR config %s not found on universe %s", drConfigUuid, universeUuid));
    }
  }

  private void enrichPitrConfigs(Universe universe, List<PitrConfig> pitrConfigs) {
    if (pitrConfigs.isEmpty()) {
      return;
    }

    if (universe.getUniverseDetails().universePaused) {
      applyUnknownState(pitrConfigs);
      return;
    }

    Map<UUID, SnapshotScheduleInfo> scheduleByUuid = getSnapshotSchedulesByUuid(universe);
    long currentTimeMillis = System.currentTimeMillis();

    for (PitrConfig pitrConfig : pitrConfigs) {
      SnapshotScheduleInfo snapshotScheduleInfo = scheduleByUuid.get(pitrConfig.getUuid());

      if (snapshotScheduleInfo == null) {
        applyUnknownState(pitrConfig, currentTimeMillis);
        continue;
      }

      boolean pitrStatus =
          BackupUtil.allSnapshotsSuccessful(snapshotScheduleInfo.getSnapshotInfoList());
      long minTimeInMillis =
          BackupUtil.getMinRecoveryTimeForSchedule(
              snapshotScheduleInfo.getSnapshotInfoList(), pitrConfig);
      pitrConfig.setMinRecoverTimeInMillis(minTimeInMillis);
      pitrConfig.setMaxRecoverTimeInMillis(currentTimeMillis);
      pitrConfig.setState(pitrStatus ? State.COMPLETE : State.FAILED);
    }
  }

  private Map<UUID, SnapshotScheduleInfo> getSnapshotSchedulesByUuid(Universe universe) {
    UUID universeUuid = universe.getUniverseUUID();
    Map<UUID, SnapshotScheduleInfo> cached = snapshotSchedulesCache.getIfPresent(universeUuid);

    if (cached != null) {
      return cached;
    }

    return loadSnapshotSchedulesOnCacheMiss(universeUuid);
  }

  /** Throttled refresh when {@link #snapshotSchedulesCache} has no entry for the universe. */
  private Map<UUID, SnapshotScheduleInfo> loadSnapshotSchedulesOnCacheMiss(UUID universeUuid) {
    ReentrantLock lock =
        universeLocks.computeIfAbsent(universeUuid, ignored -> new ReentrantLock());

    if (!lock.tryLock()) {
      Map<UUID, SnapshotScheduleInfo> cached = snapshotSchedulesCache.getIfPresent(universeUuid);

      if (cached != null) {
        return cached;
      }

      throw new PlatformServiceException(
          TOO_MANY_REQUESTS,
          "Another PITR config list request is already in progress for this universe."
              + " Please retry shortly.");
    }

    try {
      return snapshotSchedulesCache.get(universeUuid);
    } catch (ExecutionException | UncheckedExecutionException e) {
      Throwable cause = e.getCause();
      if (cause instanceof PlatformServiceException) {
        throw (PlatformServiceException) cause;
      }
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
    } finally {
      lock.unlock();
    }
  }

  private Map<UUID, SnapshotScheduleInfo> fetchSnapshotSchedulesFromMaster(Universe universe) {
    int timeoutMs = confGetter.getGlobalConf(GlobalConfKeys.pitrListApiTimeoutMs);
    YbClientConfig config =
        ybClientConfigFactory.create(
            universe.getMasterAddresses(), universe.getCertificateNodetoNode(), timeoutMs);

    try (YBClient client = ybClientService.getClientWithConfig(config)) {
      ListSnapshotSchedulesResponse scheduleResp = client.listSnapshotSchedules(null);
      if (scheduleResp.hasError()) {
        log.warn(
            "listSnapshotSchedules returned error for universe {} : {}",
            universe.getUniverseUUID(),
            scheduleResp.errorMessage());
        return Map.of();
      }

      List<SnapshotScheduleInfo> scheduleInfoList = scheduleResp.getSnapshotScheduleInfoList();
      if (scheduleInfoList == null || scheduleInfoList.isEmpty()) {
        return Map.of();
      }

      return scheduleInfoList.stream()
          .collect(
              Collectors.toMap(
                  SnapshotScheduleInfo::getSnapshotScheduleUUID,
                  Function.identity(),
                  (first, second) -> first));
    } catch (Exception e) {
      log.error("Failed to list snapshot schedules for universe {}", universe.getUniverseUUID(), e);
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
    }
  }

  private void applyUnknownState(List<PitrConfig> pitrConfigs) {
    pitrConfigs.forEach(p -> applyUnknownState(p, System.currentTimeMillis()));
  }

  private void applyUnknownState(PitrConfig pitrConfig, long currentTimeMillis) {
    pitrConfig.setState(State.UNKNOWN);
    pitrConfig.setMinRecoverTimeInMillis(currentTimeMillis);
    pitrConfig.setMaxRecoverTimeInMillis(currentTimeMillis);
  }
}
