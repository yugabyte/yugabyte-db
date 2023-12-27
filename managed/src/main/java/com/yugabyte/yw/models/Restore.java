// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static com.yugabyte.yw.models.helpers.CommonUtils.appendInClause;
import static com.yugabyte.yw.models.helpers.CommonUtils.appendLikeClause;
import static com.yugabyte.yw.models.helpers.CommonUtils.performPagedQuery;

import com.google.common.collect.ImmutableMultimap;
import com.google.common.collect.ImmutableSet;
import com.google.common.collect.Multimap;
import com.yugabyte.yw.common.backuprestore.BackupUtil;
import com.yugabyte.yw.forms.RestoreBackupParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.RestoreResp.RestoreRespBuilder;
import com.yugabyte.yw.models.filters.RestoreFilter;
import com.yugabyte.yw.models.paging.PagedQuery;
import com.yugabyte.yw.models.paging.PagedQuery.SortByIF;
import com.yugabyte.yw.models.paging.PagedQuery.SortDirection;
import com.yugabyte.yw.models.paging.RestorePagedApiResponse;
import com.yugabyte.yw.models.paging.RestorePagedQuery;
import com.yugabyte.yw.models.paging.RestorePagedResponse;
import io.ebean.ExpressionList;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.PersistenceContextScope;
import io.ebean.Query;
import io.ebean.annotation.CreatedTimestamp;
import io.ebean.annotation.EnumValue;
import io.ebean.annotation.UpdatedTimestamp;
import io.swagger.annotations.ApiModel;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.HashSet;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.EnumType;
import javax.persistence.Enumerated;
import javax.persistence.Id;
import lombok.Getter;
import lombok.Setter;
import org.apache.commons.collections4.CollectionUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.CommonTypes.TableType;

@ApiModel(description = "Universe level restores")
@Entity
@Getter
@Setter
public class Restore extends Model {
  public static final Logger LOG = LoggerFactory.getLogger(Restore.class);
  public static final String BACKUP_UNIVERSE_UUID =
      "[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}";
  public static final Pattern PATTERN = Pattern.compile(BACKUP_UNIVERSE_UUID);
  public static final String BACKUP_CREATED_DATE =
      "[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}";
  public static final Pattern PATTERN_DATE = Pattern.compile(BACKUP_CREATED_DATE);

  public static final Finder<UUID, Restore> find = new Finder<UUID, Restore>(Restore.class) {};

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

  public enum State {
    @EnumValue("Created")
    Created(TaskInfo.State.Created),

    @EnumValue("In Progress")
    InProgress(TaskInfo.State.Initializing, TaskInfo.State.Running),

    @EnumValue("Completed")
    Completed(TaskInfo.State.Success),

    @EnumValue("Failed")
    Failed(TaskInfo.State.Failure, TaskInfo.State.Unknown, TaskInfo.State.Abort),

    @EnumValue("Aborted")
    Aborted(TaskInfo.State.Aborted);

    private final TaskInfo.State[] allowedStates;

    State(TaskInfo.State... allowedStates) {
      this.allowedStates = allowedStates;
    }

    public ImmutableSet<TaskInfo.State> allowedStates() {
      return ImmutableSet.copyOf(allowedStates);
    }
  }

  @Id private UUID restoreUUID;

  @Column(nullable = false)
  private UUID customerUUID;

  @Column(nullable = false)
  private UUID universeUUID;

  @Column private UUID sourceUniverseUUID;

  @Column private UUID storageConfigUUID;

  @Column private String sourceUniverseName;

  @Column(nullable = false)
  @Enumerated(EnumType.STRING)
  private State state;

  @Column(unique = true)
  private UUID taskUUID;

  @Column private long restoreSizeInBytes = 0L;

  @Column(nullable = false)
  private boolean alterLoadBalancer = true;

  @CreatedTimestamp private Date createTime;

  @UpdatedTimestamp private Date updateTime;

  @Column private Date backupCreatedOnDate;

  @Column private TableType backupType;

  private static final Multimap<State, State> ALLOWED_TRANSITIONS =
      ImmutableMultimap.<State, State>builder()
          .put(State.Created, State.InProgress)
          .put(State.Created, State.Completed)
          .put(State.Created, State.Failed)
          .put(State.Created, State.Aborted)
          .put(State.InProgress, State.Completed)
          .put(State.InProgress, State.Failed)
          .put(State.InProgress, State.Aborted)
          .put(State.Aborted, State.InProgress)
          .put(State.Aborted, State.Completed)
          .put(State.Aborted, State.Failed)
          .build();

  public static Restore create(UUID taskUUID, RestoreBackupParams taskDetails) {
    Restore restore = new Restore();
    restore.setRestoreUUID(taskDetails.prefixUUID);
    restore.setUniverseUUID(taskDetails.getUniverseUUID());
    restore.setCustomerUUID(taskDetails.customerUUID);
    restore.setTaskUUID(taskUUID);
    String storageLocation = "";
    if (!CollectionUtils.isEmpty(taskDetails.backupStorageInfoList)) {
      storageLocation = taskDetails.backupStorageInfoList.get(0).storageLocation;
      restore.setBackupType(taskDetails.backupStorageInfoList.get(0).backupType);
      Matcher matcherDate = PATTERN_DATE.matcher(storageLocation);
      if (matcherDate.find()) {
        try {
          Date backupDate =
              new SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss").parse(matcherDate.group(0));
          LOG.debug("Setting backup created on time {}.", backupDate);
          restore.setBackupCreatedOnDate(backupDate);
        } catch (Exception e) {
          LOG.error("Ignoring incorrect date format in storage location");
        }
      }
    }
    Matcher matcher = PATTERN.matcher(storageLocation);
    if (matcher.find()) {
      restore.setSourceUniverseUUID(UUID.fromString(matcher.group(0)));
    }
    boolean isSourceUniversePresent =
        BackupUtil.checkIfUniverseExists(restore.getSourceUniverseUUID());
    restore.setSourceUniverseName(
        isSourceUniversePresent
            ? Universe.getOrBadRequest(restore.getSourceUniverseUUID()).getName()
            : "");
    restore.setStorageConfigUUID(taskDetails.storageConfigUUID);
    restore.setState(State.InProgress);
    restore.setUpdateTime(restore.getCreateTime());
    restore.setAlterLoadBalancer(taskDetails.alterLoadBalancer);
    restore.save();
    return restore;
  }

  public void update(UUID taskUUID, State finalState) {
    TaskInfo taskInfo = TaskInfo.getOrBadRequest(taskUUID);
    if (getState().equals(finalState)) {
      LOG.debug("Skipping state transition as restore is already in the {} state", getState());
    } else if (ALLOWED_TRANSITIONS.containsEntry(getState(), finalState)) {
      LOG.debug("Restore state transitioned from {} to {}", getState(), finalState);
      setState(finalState);
    } else {
      LOG.error("Ignored INVALID STATE TRANSITION  {} -> {}", getState(), finalState);
    }
    setUpdateTime(taskInfo.getUpdateTime());
    save();
  }

  public void updateTaskUUID(UUID taskUUID) {
    setTaskUUID(taskUUID);
    save();
  }

  public static void updateRestoreSizeForRestore(UUID restoreUUID, long backupSize) {
    if (restoreUUID == null || Objects.isNull(backupSize)) {
      return;
    }
    Optional<Restore> restoreIfPresent = Restore.fetchRestore(restoreUUID);
    if (!restoreIfPresent.isPresent()) {
      return;
    }
    Restore restore = restoreIfPresent.get();
    UniverseDefinitionTaskParams universeTaskParams =
        Universe.getOrBadRequest(restore.getUniverseUUID()).getUniverseDetails();
    int replicationFactor = universeTaskParams.getPrimaryCluster().userIntent.replicationFactor;
    restore.setRestoreSizeInBytes(restore.getRestoreSizeInBytes() + replicationFactor * backupSize);
    restore.save();
  }

  public static List<Restore> fetchByUniverseUUID(UUID customerUUID, UUID universeUUID) {
    List<Restore> restoreList =
        find.query()
            .where()
            .eq("customer_uuid", customerUUID)
            .eq("universe_uuid", universeUUID)
            .orderBy("create_time desc")
            .findList();
    return restoreList;
  }

  public static List<Restore> fetchByTaskUUID(UUID taskUUID) {
    List<Restore> restoreList = Restore.find.query().where().eq("task_uuid", taskUUID).findList();
    return restoreList;
  }

  public static Optional<Restore> fetchRestore(UUID restoreUUID) {
    if (restoreUUID == null) {
      return Optional.empty();
    }
    Restore restore = find.byId(restoreUUID);
    if (restore == null) {
      LOG.trace("Cannot find restore {}", restoreUUID);
      return Optional.empty();
    }
    return Optional.of(restore);
  }

  public static RestoreResp toRestoreResp(Restore restore) {
    String targetUniverseName =
        BackupUtil.checkIfUniverseExists(restore.getUniverseUUID())
            ? Universe.getOrBadRequest(restore.getUniverseUUID()).getName()
            : "";
    Boolean isSourceUniversePresent =
        BackupUtil.checkIfUniverseExists(restore.getSourceUniverseUUID());
    State state = restore.getState();
    List<RestoreKeyspace> restoreKeyspaceList =
        RestoreKeyspace.fetchRestoreKeyspaceFromRestoreUUID(restore.getRestoreUUID());
    RestoreRespBuilder builder =
        RestoreResp.builder()
            .restoreUUID(restore.getRestoreUUID())
            .createTime(restore.getCreateTime())
            .updateTime(restore.getUpdateTime())
            .targetUniverseName(targetUniverseName)
            .sourceUniverseName(restore.getSourceUniverseName())
            .customerUUID(restore.getCustomerUUID())
            .universeUUID(restore.getUniverseUUID())
            .sourceUniverseUUID(restore.getSourceUniverseUUID())
            .state(state)
            .backupType(restore.getBackupType())
            .backupCreatedOnDate(restore.getBackupCreatedOnDate())
            .restoreSizeInBytes(restore.getRestoreSizeInBytes())
            .restoreKeyspaceList(restoreKeyspaceList)
            .isSourceUniversePresent(isSourceUniversePresent);
    return builder.build();
  }

  public static RestorePagedApiResponse pagedList(RestorePagedQuery pagedQuery) {
    if (pagedQuery.getSortBy() == null) {
      pagedQuery.setSortBy(SortBy.createTime);
      pagedQuery.setDirection(SortDirection.DESC);
    }
    Query<Restore> query = createQueryByFilter(pagedQuery.getFilter()).query();
    RestorePagedResponse response =
        performPagedQuery(query, pagedQuery, RestorePagedResponse.class);
    RestorePagedApiResponse resp = createResponse(response);
    return resp;
  }

  public static ExpressionList<Restore> createQueryByFilter(RestoreFilter filter) {
    ExpressionList<Restore> query =
        find.query().setPersistenceContextScope(PersistenceContextScope.QUERY).where();

    query.eq("customer_uuid", filter.getCustomerUUID());
    if (!CollectionUtils.isEmpty(filter.getUniverseUUIDList())) {
      appendInClause(query, "universe_uuid", filter.getUniverseUUIDList());
    }
    if (!CollectionUtils.isEmpty(filter.getStorageConfigUUIDList())) {
      appendInClause(query, "storage_config_uuid", filter.getStorageConfigUUIDList());
    }
    if (!CollectionUtils.isEmpty(filter.getUniverseNameList())) {
      String universeName =
          "select t0.universe_uuid in"
              + "(select U.universe_uuid from universe U where U.name like any(?))";
      Set<String> newFilterUniverseNameList = new HashSet<String>();
      for (String name : filter.getUniverseNameList()) {
        newFilterUniverseNameList.add("%" + name + "%");
      }
      query.or().raw(universeName, newFilterUniverseNameList).endOr();
    }
    if (!CollectionUtils.isEmpty(filter.getSourceUniverseNameList())) {
      appendLikeClause(query, "source_universe_name", filter.getSourceUniverseNameList());
    }
    if (filter.getDateRangeStart() != null && filter.getDateRangeEnd() != null) {
      query.between("create_time", filter.getDateRangeStart(), filter.getDateRangeEnd());
    }
    if (!CollectionUtils.isEmpty(filter.getStates())) {
      appendInClause(query, "state", filter.getStates());
    }
    if (filter.isOnlyShowDeletedSourceUniverses()) {
      String sourceUniverseNotExists =
          "t0.source_universe_uuid not in (select U.universe_uuid from universe U)";
      query.raw(sourceUniverseNotExists);
    }
    return query;
  }

  public static RestorePagedApiResponse createResponse(RestorePagedResponse response) {

    List<Restore> restores = response.getEntities();
    List<RestoreResp> restoreList =
        restores.parallelStream().map(r -> toRestoreResp(r)).collect(Collectors.toList());
    return response.setData(restoreList, new RestorePagedApiResponse());
  }
}
