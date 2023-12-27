// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static com.yugabyte.yw.models.helpers.CommonUtils.appendInClause;
import static com.yugabyte.yw.models.helpers.CommonUtils.appendLikeClause;
import static com.yugabyte.yw.models.helpers.CommonUtils.performPagedQuery;
import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;
import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;
import static play.mvc.Http.Status.BAD_REQUEST;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.Sets;
import com.yugabyte.yw.common.BackupUtil;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.models.filters.BackupFilter;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.helpers.TimeUnit;
import com.yugabyte.yw.models.paging.BackupPagedApiResponse;
import com.yugabyte.yw.models.paging.BackupPagedQuery;
import com.yugabyte.yw.models.paging.BackupPagedResponse;
import com.yugabyte.yw.models.paging.PagedQuery;
import com.yugabyte.yw.models.paging.PagedQuery.SortByIF;
import com.yugabyte.yw.models.paging.PagedQuery.SortDirection;
import io.ebean.ExpressionList;
import io.ebean.Finder;
import io.ebean.Junction;
import io.ebean.Model;
import io.ebean.PersistenceContextScope;
import io.ebean.Query;
import io.ebean.annotation.CreatedTimestamp;
import io.ebean.annotation.DbJson;
import io.ebean.annotation.EnumValue;
import io.ebean.annotation.UpdatedTimestamp;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.Id;
import org.apache.commons.collections4.CollectionUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.api.Play;

@ApiModel(
    description =
        "A single backup. Includes the backup's status, expiration time, and configuration.")
@Entity
public class Backup extends Model {
  public static final Logger LOG = LoggerFactory.getLogger(Backup.class);
  SimpleDateFormat tsFormat = new SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss");

  public enum BackupState {
    @EnumValue("In Progress")
    InProgress,

    @EnumValue("Completed")
    Completed,

    @EnumValue("Failed")
    Failed,

    @EnumValue("Deleted")
    Deleted,

    @EnumValue("Skipped")
    Skipped,

    // Complete or partial failure to delete
    @EnumValue("FailedToDelete")
    FailedToDelete,

    @EnumValue("Stopped")
    Stopped,

    @EnumValue("DeleteInProgress")
    DeleteInProgress,

    @EnumValue("QueuedForDeletion")
    QueuedForDeletion
  }

  public enum BackupCategory {
    @EnumValue("YB_BACKUP_SCRIPT")
    YB_BACKUP_SCRIPT,

    @EnumValue("YB_CONTROLLER")
    YB_CONTROLLER
  }

  public enum BackupVersion {
    @EnumValue("V1")
    V1,

    @EnumValue("V2")
    V2
  }

  public enum StorageConfigType {
    @EnumValue("S3")
    S3,

    @EnumValue("NFS")
    NFS,

    @EnumValue("AZ")
    AZ,

    @EnumValue("GCS")
    GCS,

    @EnumValue("FILE")
    FILE;
  }

  public static final Set<BackupState> IN_PROGRESS_STATES =
      Sets.immutableEnumSet(
          BackupState.InProgress, BackupState.QueuedForDeletion, BackupState.DeleteInProgress);

  public enum SortBy implements PagedQuery.SortByIF {
    createTime("createTime");

    private final String sortField;

    SortBy(String sortField) {
      this.sortField = sortField;
    }

    public String getSortField() {
      return sortField;
    }

    @Override
    public SortByIF getOrderField() {
      return SortBy.createTime;
    }
  }

  @ApiModelProperty(value = "Backup UUID", accessMode = READ_ONLY)
  @Id
  public UUID backupUUID;

  @ApiModelProperty(value = "Customer UUID that owns this backup", accessMode = READ_WRITE)
  @Column(nullable = false)
  public UUID customerUUID;

  @ApiModelProperty(value = "Universe UUID that created this backup", accessMode = READ_WRITE)
  @Column(nullable = false)
  public UUID universeUUID;

  @ApiModelProperty(value = "Storage Config UUID that created this backup", accessMode = READ_WRITE)
  @Column(nullable = false)
  public UUID storageConfigUUID;

  @ApiModelProperty(value = "Universe name that created this backup", accessMode = READ_WRITE)
  @Column
  public String universeName;

  @ApiModelProperty(value = "State of the backup", example = "DELETED", accessMode = READ_ONLY)
  @Column(nullable = false)
  public BackupState state;

  @ApiModelProperty(value = "Details of the backup", accessMode = READ_WRITE)
  @Column(columnDefinition = "TEXT", nullable = false)
  @DbJson
  private BackupTableParams backupInfo;

  @ApiModelProperty(value = "Backup UUID", accessMode = READ_ONLY)
  @Column(unique = true)
  public UUID taskUUID;

  @ApiModelProperty(
      value = "Schedule UUID, if this backup is part of a schedule",
      accessMode = READ_WRITE)
  @Column
  private UUID scheduleUUID;

  public UUID getScheduleUUID() {
    return scheduleUUID;
  }

  @ApiModelProperty(value = "Expiry time (unix timestamp) of the backup", accessMode = READ_WRITE)
  @Column
  // Unix timestamp at which backup will get deleted.
  private Date expiry;

  public Date getExpiry() {
    return expiry;
  }

  private void setExpiry(long timeBeforeDeleteFromPresent) {
    this.expiry = new Date(System.currentTimeMillis() + timeBeforeDeleteFromPresent);
  }

  public void updateExpiryTime(long timeBeforeDeleteFromPresent) {
    setExpiry(timeBeforeDeleteFromPresent);
    save();
  }

  @ApiModelProperty(value = "Time unit for backup expiry time", accessMode = READ_WRITE)
  @Column
  private TimeUnit expiryTimeUnit;

  public TimeUnit getExpiryTimeUnit() {
    return this.expiryTimeUnit;
  }

  public void setExpiryTimeUnit(TimeUnit expiryTimeUnit) {
    this.expiryTimeUnit = expiryTimeUnit;
  }

  public void updateExpiryTimeUnit(TimeUnit expiryTimeUnit) {
    setExpiryTimeUnit(expiryTimeUnit);
    save();
  }

  public void updateStorageConfigUUID(UUID storageConfigUUID) {
    this.storageConfigUUID = storageConfigUUID;
    this.backupInfo.storageConfigUUID = storageConfigUUID;
    save();
  }

  public void setBackupInfo(BackupTableParams params) {
    this.backupInfo = params;
  }

  public BackupTableParams getBackupInfo() {
    return this.backupInfo;
  }

  @CreatedTimestamp private Date createTime;

  public Date getCreateTime() {
    return createTime;
  }

  @UpdatedTimestamp private Date updateTime;

  public Date getUpdateTime() {
    return updateTime;
  }

  @ApiModelProperty(value = "Backup completion time", accessMode = READ_WRITE)
  @Column
  private Date completionTime;

  public void setCompletionTime(Date completionTime) {
    this.completionTime = completionTime;
  }

  public Date getCompletionTime() {
    return this.completionTime;
  }

  @ApiModelProperty(value = "Category of the backup")
  @Column(nullable = false)
  public BackupCategory category = BackupCategory.YB_BACKUP_SCRIPT;

  @ApiModelProperty(value = "Version of the backup in a category")
  @Column(nullable = false)
  public BackupVersion version = BackupVersion.V1;

  public static final Finder<UUID, Backup> find = new Finder<UUID, Backup>(Backup.class) {};

  public static Backup create(
      UUID customerUUID, BackupTableParams params, BackupCategory category, BackupVersion version) {
    Backup backup = new Backup();
    backup.backupUUID = UUID.randomUUID();
    backup.customerUUID = customerUUID;
    backup.universeUUID = params.universeUUID;
    backup.storageConfigUUID = params.storageConfigUUID;
    Universe universe = Universe.maybeGet(params.universeUUID).orElse(null);
    if (universe != null) {
      backup.universeName = universe.name;
      if (universe.getUniverseDetails().encryptionAtRestConfig.kmsConfigUUID != null) {
        params.kmsConfigUUID = universe.getUniverseDetails().encryptionAtRestConfig.kmsConfigUUID;
      }
    }
    backup.state = BackupState.InProgress;
    backup.category = category;
    backup.version = version;
    if (params.scheduleUUID != null) {
      backup.scheduleUUID = params.scheduleUUID;
    }
    if (params.timeBeforeDelete != 0L) {
      backup.expiry = new Date(System.currentTimeMillis() + params.timeBeforeDelete);
      backup.setExpiryTimeUnit(params.expiryTimeUnit);
    }
    if (params.backupList != null) {
      params.backupUuid = backup.backupUUID;
      // In event of universe backup
      for (BackupTableParams childBackup : params.backupList) {
        childBackup.backupUuid = backup.backupUUID;
        childBackup.backupParamsIdentifier = UUID.randomUUID();
        if (childBackup.storageLocation == null) {
          BackupUtil.updateDefaultStorageLocation(childBackup, customerUUID, version);
        }
      }
    } else if (params.storageLocation == null) {
      params.backupUuid = backup.backupUUID;
      // We would derive the storage location based on the parameters
      BackupUtil.updateDefaultStorageLocation(params, customerUUID, version);
    }
    CustomerConfig storageConfig = CustomerConfig.get(customerUUID, params.storageConfigUUID);
    if (storageConfig != null) {
      params.storageConfigType = StorageConfigType.valueOf(storageConfig.name);
    }
    backup.setBackupInfo(params);
    backup.save();
    return backup;
  }

  public static Backup create(UUID customerUUID, BackupTableParams params) {
    return create(customerUUID, params, BackupCategory.YB_BACKUP_SCRIPT, BackupVersion.V1);
  }

  @JsonIgnore
  public List<BackupTableParams> getBackupParamsCollection() {
    BackupTableParams backupParams = getBackupInfo();
    if (CollectionUtils.isNotEmpty(backupParams.backupList)) {
      return backupParams.backupList;
    } else {
      return ImmutableList.of(backupParams);
    }
  }

  // We need to set the taskUUID right after commissioner task is submitted.

  /**
   * @param taskUUID to set if none set previously
   * @return true if the call ends up setting task uuid.
   */
  public synchronized boolean setTaskUUID(UUID taskUUID) {
    if (this.taskUUID == null) {
      this.taskUUID = taskUUID;
      save();
      return true;
    }
    return false;
  }

  public void updateBackupInfo(BackupTableParams params) {
    this.backupInfo = params;
    save();
  }

  public static List<Backup> fetchByUniverseUUID(UUID customerUUID, UUID universeUUID) {
    List<Backup> backupList =
        find.query()
            .where()
            .eq("customer_uuid", customerUUID)
            .orderBy("create_time desc")
            .findList();
    return backupList
        .stream()
        .filter(backup -> backup.getBackupInfo().universeUUID.equals(universeUUID))
        .collect(Collectors.toList());
  }

  public static List<Backup> fetchBackupToDeleteByUniverseUUID(
      UUID customerUUID, UUID universeUUID) {
    return fetchByUniverseUUID(customerUUID, universeUUID)
        .stream()
        .filter(b -> !Backup.IN_PROGRESS_STATES.contains(b.state))
        .collect(Collectors.toList());
  }

  public static BackupPagedApiResponse pagedList(BackupPagedQuery pagedQuery) {
    if (pagedQuery.getSortBy() == null) {
      pagedQuery.setSortBy(SortBy.createTime);
      pagedQuery.setDirection(SortDirection.DESC);
    }
    Query<Backup> query = createQueryByFilter(pagedQuery.getFilter()).query();
    BackupPagedResponse response = performPagedQuery(query, pagedQuery, BackupPagedResponse.class);
    BackupPagedApiResponse resp = createResponse(response);
    return resp;
  }

  @Deprecated
  public static Backup get(UUID customerUUID, UUID backupUUID) {
    return find.query().where().idEq(backupUUID).eq("customer_uuid", customerUUID).findOne();
  }

  public static Backup getOrBadRequest(UUID customerUUID, UUID backupUUID) {
    Backup backup = get(customerUUID, backupUUID);
    if (backup == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Invalid customer or backup UUID");
    }
    return backup;
  }

  public static Optional<Backup> maybeGet(UUID customerUUID, UUID backupUUID) {
    return Optional.ofNullable(get(customerUUID, backupUUID));
  }

  public static List<Backup> fetchAllBackupsByTaskUUID(UUID taskUUID) {
    return Backup.find.query().where().eq("task_uuid", taskUUID).findList();
  }

  public static Map<Customer, List<Backup>> getExpiredBackups() {
    // Get current timestamp.
    Date now = new Date();
    List<Backup> expiredBackups =
        Backup.find.query().where().lt("expiry", now).notIn("state", IN_PROGRESS_STATES).findList();

    Map<UUID, List<Backup>> expiredBackupsByCustomerUUID = new HashMap<>();
    for (Backup backup : expiredBackups) {
      expiredBackupsByCustomerUUID.putIfAbsent(backup.customerUUID, new ArrayList<>());
      expiredBackupsByCustomerUUID.get(backup.customerUUID).add(backup);
    }

    Map<Customer, List<Backup>> ret = new HashMap<>();
    expiredBackupsByCustomerUUID.forEach(
        (customerUUID, backups) -> {
          Customer customer = Customer.get(customerUUID);
          List<Backup> backupList =
              backups
                  .stream()
                  .filter(backup -> !Universe.isUniversePaused(backup.getBackupInfo().universeUUID))
                  .collect(Collectors.toList());
          ret.put(customer, backupList);
        });
    return ret;
  }

  public synchronized void transitionState(BackupState newState) {
    // Need updated backup state as multiple threads can access backup object.
    this.refresh();
    if (this.state == newState) {
      LOG.error("Skipping state transition as no change in previous and new state");
    } else if ((this.state == BackupState.InProgress)
        || (this.state != BackupState.DeleteInProgress && newState == BackupState.QueuedForDeletion)
        || (this.state == BackupState.QueuedForDeletion && newState == BackupState.DeleteInProgress)
        || (this.state == BackupState.QueuedForDeletion && newState == BackupState.FailedToDelete)
        || (this.state == BackupState.DeleteInProgress && newState == BackupState.FailedToDelete)
        || (this.state == BackupState.Completed && newState == BackupState.Stopped)
        || (this.state == BackupState.Failed && newState == BackupState.Stopped)) {
      LOG.debug("New Backup API: transitioned from {} to {}", this.state, newState);
      this.state = newState;
      save();
    } else if ((this.state == BackupState.InProgress && this.state != newState)
        || (this.state == BackupState.Completed && newState == BackupState.Deleted)
        || (this.state == BackupState.Completed && newState == BackupState.FailedToDelete)
        || (this.state == BackupState.Failed && newState == BackupState.Deleted)
        || (this.state == BackupState.Failed && newState == BackupState.FailedToDelete)) {
      LOG.debug("Old Backup API: transitioned from {} to {}", this.state, newState);
      this.state = newState;
      save();
    } else {
      LOG.error("Ignored INVALID STATE TRANSITION  {} -> {}", state, newState);
    }
  }

  public void setBackupSizeInBackupList(int idx, long backupSize) {
    int backupListLen = this.backupInfo.backupList.size();
    if (idx >= backupListLen) {
      LOG.error("Index {} not present in backup list of length {}", idx, backupListLen);
      return;
    }
    this.backupInfo.backupList.get(idx).backupSizeInBytes = backupSize;
    this.save();
  }

  public void setPerRegionLocations(int idx, List<BackupUtil.RegionLocations> perRegionLocations) {
    if (idx == -1) {
      this.backupInfo.regionLocations = perRegionLocations;
      this.save();
      return;
    }
    int backupListLen = this.backupInfo.backupList.size();
    if (idx >= backupListLen) {
      LOG.error("Index {} not present in backup list of length {}", idx, backupListLen);
      return;
    }
    this.backupInfo.backupList.get(idx).regionLocations = perRegionLocations;
    this.save();
  }

  public void setTotalBackupSize(long backupSize) {
    this.backupInfo.backupSizeInBytes = backupSize;
    this.save();
  }

  public static List<Backup> getInProgressAndCompleted(UUID customerUUID) {
    return find.query()
        .where()
        .eq("customer_uuid", customerUUID)
        .in("state", BackupState.InProgress, BackupState.Completed)
        .or()
        .eq("state", BackupState.Completed)
        .eq("state", BackupState.InProgress)
        .endOr()
        .findList();
  }

  public static List<Backup> findAllBackupsQueuedForDeletion(UUID customerUUID) {
    List<Backup> backupList =
        find.query()
            .where()
            .eq("customer_uuid", customerUUID)
            .eq("state", BackupState.QueuedForDeletion)
            .findList();
    return backupList;
  }

  public static List<Backup> findAllFinishedBackupsWithCustomerConfig(UUID customerConfigUUID) {
    List<Backup> backupList =
        find.query()
            .where()
            .or()
            .eq("state", BackupState.Failed)
            .eq("state", BackupState.Completed)
            .endOr()
            .findList();
    backupList =
        backupList
            .stream()
            .filter(b -> b.backupInfo.actionType == BackupTableParams.ActionType.CREATE)
            .filter(b -> b.getBackupInfo().storageConfigUUID.equals(customerConfigUUID))
            .collect(Collectors.toList());
    return backupList;
  }

  public static List<Backup> findAllBackupsQueuedForDeletionWithCustomerConfig(
      UUID customerConfigUUID, UUID customerUUID) {
    List<Backup> backupList = findAllBackupsQueuedForDeletion(customerUUID);
    backupList =
        backupList
            .stream()
            .filter(b -> b.getBackupInfo().storageConfigUUID.equals(customerConfigUUID))
            .collect(Collectors.toList());
    return backupList;
  }

  public static List<Backup> findAllNonProgressBackupsWithCustomerConfig(
      UUID customerConfigUUID, UUID customerUUID) {
    List<Backup> backupList =
        find.query()
            .where()
            .eq("customer_uuid", customerUUID)
            .notIn("state", IN_PROGRESS_STATES)
            .findList();
    backupList =
        backupList
            .stream()
            .filter(b -> b.getBackupInfo().storageConfigUUID.equals(customerConfigUUID))
            .collect(Collectors.toList());
    return backupList;
  }

  public static boolean findIfBackupsRunningWithCustomerConfig(UUID customerConfigUUID) {
    List<Backup> backupList = find.query().where().eq("state", BackupState.InProgress).findList();
    backupList =
        backupList
            .stream()
            .filter(b -> b.getBackupInfo().storageConfigUUID.equals(customerConfigUUID))
            .collect(Collectors.toList());
    return backupList.size() != 0;
  }

  public static Set<Universe> getAssociatedUniverses(UUID customerUUID, UUID configUUID) {
    Set<UUID> universeUUIDs = new HashSet<>();
    List<Backup> backupList = getInProgressAndCompleted(customerUUID);
    backupList =
        backupList
            .stream()
            .filter(
                b ->
                    b.getBackupInfo().storageConfigUUID.equals(configUUID)
                        && universeUUIDs.add(b.getBackupInfo().universeUUID))
            .collect(Collectors.toList());

    List<Schedule> scheduleList =
        Schedule.find
            .query()
            .where()
            .in("task_type", TaskType.BackupUniverse, TaskType.MultiTableBackup)
            .eq("status", "Active")
            .findList();
    scheduleList =
        scheduleList
            .stream()
            .filter(
                s ->
                    s.getTaskParams()
                            .path("storageConfigUUID")
                            .asText()
                            .equals(configUUID.toString())
                        && universeUUIDs.add(
                            UUID.fromString(s.getTaskParams().get("universeUUID").asText())))
            .collect(Collectors.toList());
    Set<Universe> universes = new HashSet<>();
    for (UUID universeUUID : universeUUIDs) {
      try {
        universes.add(Universe.getOrBadRequest(universeUUID));
      }
      // Backup is present but universe does not. We are ignoring such backups.
      catch (Exception e) {
      }
    }
    return universes;
  }

  public static List<Backup> fetchAllCompletedBackupsByScheduleUUID(
      UUID customerUUID, UUID scheduleUUID) {
    return find.query()
        .where()
        .eq("customer_uuid", customerUUID)
        .eq("schedule_uuid", scheduleUUID)
        .eq("state", BackupState.Completed)
        .findList();
  }

  public static ExpressionList<Backup> createQueryByFilter(BackupFilter filter) {
    ExpressionList<Backup> query =
        find.query().setPersistenceContextScope(PersistenceContextScope.QUERY).where();

    query.eq("customer_uuid", filter.getCustomerUUID());
    appendActionTypeClause(query);
    if (!CollectionUtils.isEmpty(filter.getScheduleUUIDList())) {
      appendInClause(query, "schedule_uuid", filter.getScheduleUUIDList());
    }
    if (!CollectionUtils.isEmpty(filter.getUniverseUUIDList())) {
      appendInClause(query, "universe_uuid", filter.getUniverseUUIDList());
    }
    if (!CollectionUtils.isEmpty(filter.getStorageConfigUUIDList())) {
      appendInClause(query, "storage_config_uuid", filter.getStorageConfigUUIDList());
    }
    if (!CollectionUtils.isEmpty(filter.getUniverseNameList())) {
      appendLikeClause(query, "universe_name", filter.getUniverseNameList());
    }
    if (filter.getDateRangeStart() != null && filter.getDateRangeEnd() != null) {
      query.between("create_time", filter.getDateRangeStart(), filter.getDateRangeEnd());
    }
    if (!CollectionUtils.isEmpty(filter.getStates())) {
      appendInClause(query, "state", filter.getStates());
    }
    if (!CollectionUtils.isEmpty(filter.getKeyspaceList())) {
      Junction<Backup> orExpr = query.or();
      String queryStringInner =
          "t0.backup_uuid in "
              + "(select B.backup_uuid from backup B,"
              + "json_array_elements(B.backup_info -> 'backupList') bl "
              + "where json_typeof(B.backup_info -> 'backupList')='array' "
              + "and bl ->> 'keyspace' = any(?)) ";
      String queryStringOuter =
          "t0.backup_uuid in "
              + "(select B.backup_uuid from backup B "
              + "where B.backup_info ->> 'keyspace' = any(?))";
      orExpr.raw(queryStringInner, filter.getKeyspaceList());
      orExpr.raw(queryStringOuter, filter.getKeyspaceList());
      query.endOr();
    }
    if (filter.isOnlyShowDeletedUniverses()) {
      String universeNotExists =
          "t0.universe_uuid not in" + "(select U.universe_uuid from universe U)";
      query.raw(universeNotExists);
    }
    if (filter.isOnlyShowDeletedConfigs()) {
      String configNotExists =
          "t0.storage_config_uuid not in" + "(select C.config_uuid from customer_config C)";
      query.raw(configNotExists);
    }
    return query;
  }

  public static <T> ExpressionList<T> appendActionTypeClause(ExpressionList<T> query) {
    String rawSql =
        "t0.backup_uuid in "
            + "(select B.backup_uuid from backup B "
            + "where coalesce(B.backup_info ->> 'actionType', 'CREATE') = ?)";
    query.raw(rawSql, "CREATE");
    return query;
  }

  public static BackupPagedApiResponse createResponse(BackupPagedResponse response) {

    CustomerConfigService customerConfigService =
        Play.current().injector().instanceOf(CustomerConfigService.class);
    List<Backup> backups = response.getEntities();
    List<BackupResp> backupList =
        backups
            .parallelStream()
            .map(b -> BackupUtil.toBackupResp(b, customerConfigService))
            .collect(Collectors.toList());
    BackupPagedApiResponse responseMin;
    try {
      responseMin = BackupPagedApiResponse.class.newInstance();
    } catch (Exception e) {
      throw new IllegalStateException(
          "Failed to create " + BackupPagedApiResponse.class.getSimpleName() + " instance", e);
    }
    responseMin.setEntities(backupList);
    responseMin.setHasPrev(response.isHasPrev());
    responseMin.setHasNext(response.isHasNext());
    responseMin.setTotalCount(response.getTotalCount());
    return responseMin;
  }
}
