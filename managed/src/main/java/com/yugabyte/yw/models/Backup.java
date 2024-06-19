// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static com.yugabyte.yw.models.helpers.CommonUtils.appendInClause;
import static com.yugabyte.yw.models.helpers.CommonUtils.appendLikeClause;
import static com.yugabyte.yw.models.helpers.CommonUtils.performPagedQuery;
import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;
import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMultimap;
import com.google.common.collect.Multimap;
import com.google.common.collect.Sets;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.backuprestore.BackupUtil;
import com.yugabyte.yw.common.concurrent.KeyLock;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.filters.BackupFilter;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.helpers.TimeUnit;
import com.yugabyte.yw.models.helpers.TransactionUtil;
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
import io.ebean.annotation.DbJson;
import io.ebean.annotation.EnumValue;
import io.ebean.annotation.WhenCreated;
import io.ebean.annotation.WhenModified;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import jakarta.persistence.Column;
import jakarta.persistence.Entity;
import jakarta.persistence.Id;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Date;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.Getter;
import lombok.Setter;
import org.apache.commons.collections4.CollectionUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.CommonTypes.TableType;

@ApiModel(
    description =
        "A single backup. Includes the backup's status, expiration time, and configuration.")
@Entity
@Getter
@Setter
public class Backup extends Model {
  public static final Logger LOG = LoggerFactory.getLogger(Backup.class);

  // This is a key lock for Backup by UUID.
  public static final KeyLock<UUID> BACKUP_KEY_LOCK = new KeyLock<UUID>();

  public enum BackupState {
    @EnumValue("In Progress")
    InProgress,

    @EnumValue("Completed")
    Completed,

    @EnumValue("Failed")
    Failed,

    // This state is no longer used in Backup V2 APIs.
    @EnumValue("Deleted")
    Deleted,

    @EnumValue("Skipped")
    Skipped,

    // Complete or partial failure to delete
    @EnumValue("FailedToDelete")
    FailedToDelete,

    @EnumValue("Stopping")
    Stopping,

    @EnumValue("Stopped")
    Stopped,

    @EnumValue("QueuedForDeletion")
    QueuedForDeletion,

    @EnumValue("QueuedForForcedDeletion")
    QueuedForForcedDeletion,

    @EnumValue("DeleteInProgress")
    DeleteInProgress;
  }

  private static final Multimap<BackupState, BackupState> ALLOWED_TRANSITIONS =
      ImmutableMultimap.<BackupState, BackupState>builder()
          .put(BackupState.InProgress, BackupState.Completed)
          .put(BackupState.InProgress, BackupState.Failed)
          .put(BackupState.Completed, BackupState.Deleted)
          .put(BackupState.FailedToDelete, BackupState.Deleted)
          .put(BackupState.Failed, BackupState.Deleted)
          .put(BackupState.Stopped, BackupState.Deleted)
          .put(BackupState.InProgress, BackupState.Skipped)
          .put(BackupState.InProgress, BackupState.FailedToDelete)
          .put(BackupState.QueuedForDeletion, BackupState.FailedToDelete)
          .put(BackupState.DeleteInProgress, BackupState.FailedToDelete)
          .put(BackupState.Failed, BackupState.FailedToDelete)
          .put(BackupState.Completed, BackupState.FailedToDelete)
          .put(BackupState.InProgress, BackupState.Stopping)
          .put(BackupState.Failed, BackupState.Stopping)
          .put(BackupState.Completed, BackupState.Stopping)
          .put(BackupState.InProgress, BackupState.Stopped)
          .put(BackupState.Stopping, BackupState.Stopped)
          .put(BackupState.Failed, BackupState.Stopped)
          .put(BackupState.Completed, BackupState.Stopped)
          .put(BackupState.Failed, BackupState.QueuedForDeletion)
          .put(BackupState.Stopped, BackupState.QueuedForDeletion)
          .put(BackupState.Stopped, BackupState.InProgress)
          .put(BackupState.Stopping, BackupState.QueuedForDeletion)
          .put(BackupState.InProgress, BackupState.QueuedForDeletion)
          .put(BackupState.Completed, BackupState.QueuedForDeletion)
          .put(BackupState.Skipped, BackupState.QueuedForDeletion)
          .put(BackupState.FailedToDelete, BackupState.QueuedForDeletion)
          .put(BackupState.Deleted, BackupState.QueuedForDeletion)
          .put(BackupState.QueuedForDeletion, BackupState.DeleteInProgress)
          .put(BackupState.QueuedForDeletion, BackupState.QueuedForForcedDeletion)
          .put(BackupState.QueuedForForcedDeletion, BackupState.FailedToDelete)
          .put(BackupState.QueuedForForcedDeletion, BackupState.DeleteInProgress)
          .build();

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
  public static final List<BackupState> COMPLETED_STATES =
      Arrays.asList(
          BackupState.Completed, BackupState.Failed, BackupState.Stopped, BackupState.Skipped);

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
  private UUID backupUUID;

  @ApiModelProperty(value = "Customer UUID that owns this backup", accessMode = READ_WRITE)
  @Column(nullable = false)
  private UUID customerUUID;

  @JsonProperty
  public UUID getCustomerUUID() {
    return customerUUID;
  }

  @JsonIgnore
  @ApiModelProperty(value = "Universe UUID that created this backup", accessMode = READ_WRITE)
  @Column(nullable = false)
  private UUID universeUUID;

  @ApiModelProperty(value = "Storage Config UUID that created this backup", accessMode = READ_WRITE)
  @Column(nullable = false)
  private UUID storageConfigUUID;

  @ApiModelProperty(value = "Base backup UUID", accessMode = READ_ONLY)
  @Column(nullable = false)
  private UUID baseBackupUUID;

  @ApiModelProperty(value = "Universe name that created this backup", accessMode = READ_WRITE)
  @Column
  private String universeName;

  @ApiModelProperty(value = "State of the backup", example = "DELETED", accessMode = READ_ONLY)
  @Column(nullable = false)
  private BackupState state;

  @ApiModelProperty(value = "Details of the backup", accessMode = READ_WRITE)
  @Column(columnDefinition = "TEXT", nullable = false)
  @DbJson
  private BackupTableParams backupInfo;

  @ApiModelProperty(value = "Backup UUID", accessMode = READ_ONLY)
  @Column(unique = true)
  private UUID taskUUID;

  @ApiModelProperty(
      value = "Schedule UUID, if this backup is part of a schedule",
      accessMode = READ_WRITE)
  @Column
  private UUID scheduleUUID;

  @ApiModelProperty(
      value = "Schedule Policy Name, if this backup is part of a schedule",
      accessMode = READ_WRITE)
  @Column
  private String scheduleName;

  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  @ApiModelProperty(
      value = "Expiry time (unix timestamp) of the backup",
      accessMode = READ_WRITE,
      example = "2022-12-12T13:07:18Z")
  @Column
  // Unix timestamp at which backup will get deleted.
  private Date expiry;

  @JsonIgnore
  private void setExpiry(long timeBeforeDeleteFromPresent) {
    this.expiry = new Date(System.currentTimeMillis() + timeBeforeDeleteFromPresent);
  }

  @JsonIgnore
  public void setExpiry(Date expiryTime) {
    this.expiry = expiryTime;
  }

  public void updateExpiryTime(long timeBeforeDeleteFromPresent) {
    setExpiry(timeBeforeDeleteFromPresent);
    save();
  }

  @ApiModelProperty(value = "Time unit for backup expiry time", accessMode = READ_WRITE)
  @Column
  private TimeUnit expiryTimeUnit;

  @ApiModelProperty(value = "Whether the backup has KMS history metadata", accessMode = READ_ONLY)
  @Column(name = "has_kms_history")
  private boolean hasKMSHistory;

  public void updateExpiryTimeUnit(TimeUnit expiryTimeUnit) {
    setExpiryTimeUnit(expiryTimeUnit);
    save();
  }

  public void updateStorageConfigUUID(UUID storageConfigUUID) {
    this.setStorageConfigUUID(storageConfigUUID);
    this.backupInfo.storageConfigUUID = storageConfigUUID;
    save();
  }

  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  @ApiModelProperty(value = "Backup creation time", example = "2022-12-12T13:07:18Z")
  @WhenCreated
  private Date createTime;

  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  @ApiModelProperty(value = "Backup update time", example = "2022-12-12T13:07:18Z")
  @WhenModified
  private Date updateTime;

  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  @ApiModelProperty(
      value = "Backup completion time",
      accessMode = READ_ONLY,
      example = "2022-12-12T13:07:18Z")
  @Column
  private Date completionTime;

  @ApiModelProperty(value = "Category of the backup")
  @Column(nullable = false)
  private BackupCategory category = BackupCategory.YB_BACKUP_SCRIPT;

  @ApiModelProperty(value = "Version of the backup in a category")
  @Column(nullable = false)
  private BackupVersion version = BackupVersion.V1;

  @ApiModelProperty(value = "Retry count for backup deletion")
  @Column(nullable = false)
  private int retryCount;

  public static final Finder<UUID, Backup> find = new Finder<UUID, Backup>(Backup.class) {};

  public static Backup create(
      UUID customerUUID, BackupTableParams params, BackupCategory category, BackupVersion version) {
    Backup backup = new Backup();
    backup.setBackupUUID(UUID.randomUUID());
    Backup previousBackup = null;
    if (params.baseBackupUUID != null) {
      backup.setBaseBackupUUID(params.baseBackupUUID);
      previousBackup = getLastSuccessfulBackupInChain(customerUUID, params.baseBackupUUID);
    } else {
      backup.setBaseBackupUUID(backup.getBackupUUID());
    }
    backup.setCustomerUUID(customerUUID);
    backup.setUniverseUUID(params.getUniverseUUID());
    backup.setStorageConfigUUID(params.storageConfigUUID);
    Universe universe = Universe.maybeGet(params.getUniverseUUID()).orElse(null);
    if (universe != null) {
      backup.setUniverseName(universe.getName());
      if (universe.getUniverseDetails().encryptionAtRestConfig.kmsConfigUUID != null) {
        params.kmsConfigUUID = universe.getUniverseDetails().encryptionAtRestConfig.kmsConfigUUID;
      }
      backup.setHasKMSHistory(
          CollectionUtils.isNotEmpty(
              EncryptionAtRestUtil.getAllUniverseKeys(backup.getUniverseUUID())));
    }
    backup.setState(BackupState.InProgress);
    backup.setCategory(category);
    backup.setVersion(version);
    if (params.scheduleUUID != null) {
      backup.scheduleUUID = params.scheduleUUID;
      backup.scheduleName = params.scheduleName;
    }
    if (params.timeBeforeDelete != 0L) {
      backup.expiry = new Date(System.currentTimeMillis() + params.timeBeforeDelete);
      backup.setExpiryTimeUnit(params.expiryTimeUnit);
    }
    SimpleDateFormat tsFormat = new SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss");
    String backupLocationTS = tsFormat.format(new Date());
    if (params.backupList != null) {
      params.backupUuid = backup.getBackupUUID();
      params.baseBackupUUID = backup.getBaseBackupUUID();
      if (version.equals(BackupVersion.V1)
          || backup.getCategory().equals(BackupCategory.YB_BACKUP_SCRIPT)
          || previousBackup == null) {
        for (BackupTableParams childBackup : params.backupList) {
          childBackup.backupUuid = backup.getBackupUUID();
          childBackup.baseBackupUUID = backup.getBaseBackupUUID();
          childBackup.backupParamsIdentifier = UUID.randomUUID();
          if (childBackup.storageLocation == null) {
            BackupUtil.updateDefaultStorageLocation(
                childBackup,
                customerUUID,
                backup.getCategory(),
                backup.getVersion(),
                backupLocationTS);
          }
        }
      } else {
        // Only for incremental backup object creation
        populateChildParams(params, previousBackup, backupLocationTS);
      }
    } else if (params.storageLocation == null) {
      params.backupUuid = backup.getBackupUUID();
      params.baseBackupUUID = backup.getBaseBackupUUID();
      // We would derive the storage location based on the parameters
      BackupUtil.updateDefaultStorageLocation(
          params, customerUUID, backup.getCategory(), backup.getVersion(), backupLocationTS);
    }
    CustomerConfig storageConfig = CustomerConfig.get(customerUUID, params.storageConfigUUID);
    if (storageConfig != null) {
      params.storageConfigType = StorageConfigType.valueOf(storageConfig.getName());
    }
    backup.setBackupInfo(params);
    backup.save();
    return backup;
  }

  // For incremental backup requests, populate child params based on previous backup's params
  // If a previous sub-param consist of a keyspace-tables match with this request, assign the
  // same params identifier to this child param.
  private static void populateChildParams(
      BackupTableParams params, Backup previousBackup, String backupLocationTS) {
    BackupTableParams previousBackupInfo = previousBackup.getBackupInfo();
    List<BackupTableParams> paramsCollection = previousBackup.getBackupParamsCollection();
    for (BackupTableParams childParams : params.backupList) {
      childParams.backupUuid = params.backupUuid;
      childParams.baseBackupUUID = params.baseBackupUUID;
      if (!previousBackupInfo.backupType.equals(params.backupType)) {
        childParams.backupParamsIdentifier = UUID.randomUUID();
      } else {
        Optional<BackupTableParams> oParams =
            getMatchingParamsIncrementalBackup(paramsCollection, childParams, params.backupType);
        if (oParams.isPresent()) {
          BackupTableParams previousChildParams = oParams.get();
          if (previousChildParams.backupParamsIdentifier == null) {
            childParams.backupParamsIdentifier =
                previousChildParams.backupParamsIdentifier = UUID.randomUUID();
          } else {
            childParams.backupParamsIdentifier = previousChildParams.backupParamsIdentifier;
          }
        } else {
          childParams.backupParamsIdentifier = UUID.randomUUID();
        }
      }
      if (childParams.storageLocation == null) {
        BackupUtil.updateDefaultStorageLocation(
            childParams,
            params.customerUuid,
            BackupCategory.YB_CONTROLLER,
            BackupVersion.V2,
            backupLocationTS);
      }
    }
    if (previousBackup != null) {
      previousBackup.updateBackupInfo(previousBackupInfo);
    }
  }

  public static Backup create(UUID customerUUID, BackupTableParams params) {
    return create(customerUUID, params, BackupCategory.YB_BACKUP_SCRIPT, BackupVersion.V1);
  }

  public void updateBackupInfo(BackupTableParams params) {
    this.backupInfo = params;
    save();
  }

  // Find if Incremental backup's child param matches any of Previous backup's params.
  // If yes, we set params identifier same as previous backup.
  // For YSQL: keyspace name match.
  // For YCQL: keyspace name + any table UUID from previous backup param matches this
  // child param.
  private static Optional<BackupTableParams> getMatchingParamsIncrementalBackup(
      List<BackupTableParams> paramsCollection,
      BackupTableParams incrementalParam,
      TableType backupType) {
    return paramsCollection.parallelStream()
        .filter(
            backupParams ->
                incrementalParam.getKeyspace().equals(backupParams.getKeyspace())
                    && (backupType.equals(TableType.PGSQL_TABLE_TYPE)
                        ? true
                        : (CollectionUtils.containsAny(
                            backupParams.getTableUUIDList(), incrementalParam.getTableUUIDList()))))
        .findAny();
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

  /**
   * Return the BackupTableParam object from the list of params which matches the give
   * param-identifier UUID.
   *
   * @param params
   * @return Optional of BackupTableParams
   */
  @JsonIgnore
  public Optional<BackupTableParams> getParamsWithIdentifier(UUID paramsIdentifier) {
    Optional<BackupTableParams> oParams = Optional.empty();
    List<BackupTableParams> params = getBackupParamsCollection();
    if (CollectionUtils.isNotEmpty(params)) {
      oParams =
          params.parallelStream()
              .filter(bP -> bP.backupParamsIdentifier.equals(paramsIdentifier))
              .findAny();
    }
    return oParams;
  }

  public void onCompletion() {
    List<BackupTableParams> params = this.getBackupParamsCollection();
    this.backupInfo.backupSizeInBytes = params.stream().mapToLong(bI -> bI.backupSizeInBytes).sum();
    // Full chain size is same as total size for single backup.
    this.backupInfo.fullChainSizeInBytes = this.backupInfo.backupSizeInBytes;

    long totalTimeTaken = 0L;
    if (this.getCategory().equals(BackupCategory.YB_BACKUP_SCRIPT)) {
      totalTimeTaken = params.stream().mapToLong(bI -> bI.timeTakenPartial).sum();
    } else {
      totalTimeTaken = BackupUtil.getTimeTakenForParallelBackups(params);
    }
    this.completionTime = new Date(totalTimeTaken + this.createTime.getTime());
    if (this.backupInfo.timeBeforeDelete != 0L) {
      this.expiry = new Date(this.completionTime.getTime() + this.backupInfo.timeBeforeDelete);
    }
    this.setState(BackupState.Completed);
    this.save();
  }

  public void onPartialCompletion(int idx, long totalTimeTaken, long totalSizeInBytes) {
    this.backupInfo.backupList.get(idx).backupSizeInBytes = totalSizeInBytes;
    this.backupInfo.backupList.get(idx).timeTakenPartial = totalTimeTaken;
    this.save();
  }

  public interface BackupUpdater {
    void run(Backup backup);
  }

  public static void saveDetails(UUID customerUUID, UUID backupUUID, BackupUpdater updater) {
    BACKUP_KEY_LOCK.acquireLock(backupUUID);
    try {
      TransactionUtil.doInTxn(
          () -> {
            Backup backup = get(customerUUID, backupUUID);
            updater.run(backup);
            backup.save();
          },
          TransactionUtil.DEFAULT_RETRY_CONFIG);
    } finally {
      BACKUP_KEY_LOCK.releaseLock(backupUUID);
    }
  }

  public void unsetExpiry() {
    this.expiry = null;
    this.save();
  }

  public void onIncrementCompletion(Date incrementCreateDate) {
    Date newExpiryDate = new Date(incrementCreateDate.getTime() + this.backupInfo.timeBeforeDelete);
    if (this.getExpiry() != null && this.getExpiry().before(newExpiryDate)) {
      this.expiry = newExpiryDate;
    }
    this.backupInfo.fullChainSizeInBytes =
        fetchAllBackupsByBaseBackupUUID(this.customerUUID, this.getBaseBackupUUID()).stream()
            .filter(b -> b.getState() == BackupState.Completed)
            .mapToLong(b -> b.backupInfo.backupSizeInBytes)
            .sum();
    this.save();
  }

  public static List<Backup> fetchByUniverseUUID(UUID customerUUID, UUID universeUUID) {
    List<Backup> backupList =
        find.query()
            .where()
            .eq("customer_uuid", customerUUID)
            .orderBy("create_time desc")
            .findList();
    return backupList.stream()
        .filter(backup -> backup.getBackupInfo().getUniverseUUID().equals(universeUUID))
        .collect(Collectors.toList());
  }

  public static List<Backup> fetchBackupToDeleteByUniverseUUID(
      UUID customerUUID, UUID universeUUID) {
    return fetchByUniverseUUID(customerUUID, universeUUID).stream()
        .filter(b -> !Backup.IN_PROGRESS_STATES.contains(b.getState()))
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

  public static Optional<Backup> maybeGet(UUID backupUUID) {
    Backup backup = find.byId(backupUUID);
    if (backup == null) {
      LOG.trace("Cannot find backup {}", backupUUID);
      return Optional.empty();
    }
    return Optional.of(backup);
  }

  public static List<Backup> fetchAllBackupsByTaskUUID(UUID taskUUID) {
    return Backup.find.query().where().eq("task_uuid", taskUUID).findList();
  }

  public static Optional<Backup> fetchLatestByState(UUID customerUuid, BackupState state) {
    return Backup.find
        .query()
        .where()
        .eq("customer_uuid", customerUuid)
        .eq("state", state)
        .orderBy("create_time DESC")
        .findList()
        .stream()
        .findFirst();
  }

  public static Map<UUID, List<Backup>> getCompletedExpiredBackups() {
    Date now = new Date();
    List<Backup> expiredBackups =
        Backup.find.query().where().lt("expiry", now).in("state", COMPLETED_STATES).findList();
    Map<UUID, List<Backup>> expiredBackupsByCustomerUUID = new HashMap<>();
    for (Backup backup : expiredBackups) {
      if (!(Universe.isUniversePaused(backup.getBackupInfo().getUniverseUUID())
          || backup.isIncrementalBackup())) {
        List<Backup> backupList =
            expiredBackupsByCustomerUUID.getOrDefault(backup.getCustomerUUID(), new ArrayList<>());
        backupList.add(backup);
        expiredBackupsByCustomerUUID.put(backup.getCustomerUUID(), backupList);
      }
    }
    return expiredBackupsByCustomerUUID;
  }

  public synchronized void transitionState(BackupState newState) {
    // Need updated backup state as multiple threads can access backup object.
    this.refresh();
    if (this.getState().equals(newState)) {
      LOG.debug("Skipping state transition as backup is already in the {} state", this.getState());
    } else if (ALLOWED_TRANSITIONS.containsEntry(this.getState(), newState)) {
      LOG.debug("Backup state transitioned from {} to {}", this.getState(), newState);
      this.setState(newState);
      save();
    } else {
      LOG.error("Ignored INVALID STATE TRANSITION  {} -> {}", getState(), newState);
    }
  }

  public void setBackupSizeInBackupList(int idx, long backupSize) {
    int backupListLen = this.backupInfo.backupList.size();
    if (idx >= backupListLen) {
      LOG.error("Index {} not present in backup list of length {}", idx, backupListLen);
      return;
    }
    this.backupInfo.backupList.get(idx).backupSizeInBytes = backupSize;
  }

  public void setPerRegionLocations(int idx, List<BackupUtil.RegionLocations> perRegionLocations) {
    if (idx == -1) {
      this.backupInfo.regionLocations = perRegionLocations;
      return;
    }
    int backupListLen = this.backupInfo.backupList.size();
    if (idx >= backupListLen) {
      LOG.error("Index {} not present in backup list of length {}", idx, backupListLen);
      return;
    }
    this.backupInfo.backupList.get(idx).regionLocations = perRegionLocations;
  }

  public void setTotalBackupSize(long backupSize) {
    this.backupInfo.backupSizeInBytes = backupSize;
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
            .in("state", BackupState.QueuedForDeletion, BackupState.QueuedForForcedDeletion)
            .findList();
    return backupList;
  }

  public static List<Backup> findAllBackupWithState(UUID customerUUID, List<BackupState> states) {
    List<Backup> backupList =
        find.query().where().eq("customer_uuid", customerUUID).in("state", states).findList();
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
        backupList.stream()
            .filter(b -> b.backupInfo.actionType == BackupTableParams.ActionType.CREATE)
            .filter(b -> b.getBackupInfo().storageConfigUUID.equals(customerConfigUUID))
            .collect(Collectors.toList());
    return backupList;
  }

  public static List<Backup> findAllBackupsQueuedForDeletionWithCustomerConfig(
      UUID customerConfigUUID, UUID customerUUID) {
    List<Backup> backupList = findAllBackupsQueuedForDeletion(customerUUID);
    backupList =
        backupList.stream()
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
        backupList.stream()
            .filter(b -> b.getBackupInfo().storageConfigUUID.equals(customerConfigUUID))
            .collect(Collectors.toList());
    return backupList;
  }

  public static boolean findIfBackupsRunningWithCustomerConfig(UUID customerConfigUUID) {
    List<Backup> backupList = find.query().where().eq("state", BackupState.InProgress).findList();
    backupList =
        backupList.stream()
            .filter(b -> b.getBackupInfo().storageConfigUUID.equals(customerConfigUUID))
            .collect(Collectors.toList());
    return backupList.size() != 0;
  }

  public static Optional<BackupTableParams> findBackupParamsWithStorageLocation(
      String storageLocation) {
    List<Backup> backupList = find.query().findList();
    List<BackupTableParams> backupParams = new ArrayList<>();

    if (storageLocation == null) {
      return Optional.empty();
    }

    for (Backup b : backupList) {
      BackupTableParams backupInfo = b.getBackupInfo();
      if (CollectionUtils.isEmpty(backupInfo.backupList)) {
        BackupTableParams backupTableParams =
            storageLocation.equals(b.getBackupInfo().storageLocation) ? b.getBackupInfo() : null;
        if (backupTableParams != null) {
          backupParams.add(backupTableParams);
        }
      } else {
        Optional<BackupTableParams> backupTableParams =
            backupInfo.backupList.stream()
                .filter(bL -> storageLocation.equals(bL.storageLocation))
                .findFirst();
        if (backupTableParams.isPresent()) {
          backupParams.add(backupTableParams.get());
        }
      }
    }
    if (backupParams.size() == 0) {
      return Optional.empty();
    }
    return Optional.of(backupParams.get(0));
  }

  public static Set<Universe> getAssociatedUniverses(UUID customerUUID, UUID configUUID) {
    Set<UUID> universeUUIDs = new HashSet<>();
    List<Backup> backupList = getInProgressAndCompleted(customerUUID);
    backupList =
        backupList.stream()
            .filter(
                b ->
                    b.getBackupInfo().storageConfigUUID.equals(configUUID)
                        && universeUUIDs.add(b.getBackupInfo().getUniverseUUID()))
            .collect(Collectors.toList());

    List<Schedule> scheduleList =
        Schedule.find
            .query()
            .where()
            .in("task_type", TaskType.BackupUniverse, TaskType.MultiTableBackup)
            .eq("status", "Active")
            .findList();
    scheduleList =
        scheduleList.stream()
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

  public static List<Backup> fetchAllBackupsByBaseBackupUUID(
      UUID customerUUID, UUID baseBackupUUID) {
    List<Backup> backupChain =
        find.query()
            .where()
            .eq("customer_uuid", customerUUID)
            .eq("base_backup_uuid", baseBackupUUID)
            .orderBy()
            .desc("create_time")
            .findList();
    return backupChain;
  }

  /**
   * Get last backup in chain with state = 'Completed'.
   *
   * @param customerUUID
   * @param baseBackupUUID
   */
  public static Backup getLastSuccessfulBackupInChain(UUID customerUUID, UUID baseBackupUUID) {
    List<Backup> backupChain = fetchAllBackupsByBaseBackupUUID(customerUUID, baseBackupUUID);
    Optional<Backup> backup =
        backupChain.stream().filter(b -> b.getState().equals(BackupState.Completed)).findFirst();
    if (backup.isPresent()) {
      return backup.get();
    }
    return null;
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
    // Only non-incremental backups.
    query.raw("backup_uuid = base_backup_uuid");
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
    if (!CollectionUtils.isEmpty(filter.getBackupUUIDList())) {
      appendInClause(query, "backup_uuid", filter.getBackupUUIDList());
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
      String universeNotExists = "t0.universe_uuid not in (select U.universe_uuid from universe U)";
      query.raw(universeNotExists);
    }
    if (filter.isOnlyShowDeletedConfigs()) {
      String configNotExists =
          "t0.storage_config_uuid not in (select C.config_uuid from customer_config C)";
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
    List<Backup> backups = response.getEntities();
    List<BackupResp> backupList =
        backups.stream().map(BackupUtil::toBackupResp).collect(Collectors.toList());
    return response.setData(backupList, new BackupPagedApiResponse());
  }

  public boolean isIncrementalBackup() {
    return !this.isParentBackup();
  }

  public boolean isParentBackup() {
    return this.getBaseBackupUUID().equals(this.getBackupUUID());
  }
}
