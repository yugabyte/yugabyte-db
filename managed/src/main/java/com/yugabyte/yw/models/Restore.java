// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import com.yugabyte.yw.common.BackupUtil;
import static com.yugabyte.yw.models.helpers.CommonUtils.appendInClause;
import static com.yugabyte.yw.models.helpers.CommonUtils.appendLikeClause;
import static com.yugabyte.yw.models.helpers.CommonUtils.performPagedQuery;
import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;

import com.google.common.collect.ImmutableSet;
import com.google.common.collect.ImmutableMultimap;
import com.google.common.collect.Multimap;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.RestoreBackupParams.BackupStorageInfo;
import com.yugabyte.yw.forms.RestoreBackupParams;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.RestoreResp;
import com.yugabyte.yw.models.RestoreResp.RestoreRespBuilder;
import com.yugabyte.yw.models.paging.PagedQuery;
import com.yugabyte.yw.models.paging.PagedQuery.SortByIF;
import com.yugabyte.yw.models.paging.PagedQuery.SortDirection;
import com.yugabyte.yw.forms.filters.RestoreApiFilter;
import com.yugabyte.yw.forms.paging.RestorePagedApiQuery;
import com.yugabyte.yw.models.paging.RestorePagedQuery;
import com.yugabyte.yw.models.filters.RestoreFilter;
import com.yugabyte.yw.models.paging.RestorePagedResponse;
import com.yugabyte.yw.models.paging.RestorePagedApiResponse;
import java.util.Optional;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import com.yugabyte.yw.models.RestoreKeyspace;
import com.yugabyte.yw.models.TaskInfo;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.databind.JsonNode;
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
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.Id;
import javax.persistence.EnumType;
import javax.persistence.Enumerated;
import org.apache.commons.collections.CollectionUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.api.Play;
import play.libs.Json;

@ApiModel(description = "Universe level restores")
@Entity
public class Restore extends Model {
  public static final Logger LOG = LoggerFactory.getLogger(Restore.class);
  public static final String BACKUP_UNIVERSE_UUID =
      "[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}";
  public static final Pattern PATTERN = Pattern.compile(BACKUP_UNIVERSE_UUID);
  SimpleDateFormat tsFormat = new SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss");

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

    @EnumValue("Success")
    Success(TaskInfo.State.Success),

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

  @ApiModelProperty(value = "Restore UUID", accessMode = READ_ONLY)
  @Id
  public UUID restoreUUID;

  @ApiModelProperty(value = "Customer UUID that owns this restore", accessMode = READ_ONLY)
  @Column(nullable = false)
  public UUID customerUUID;

  @ApiModelProperty(value = "Universe UUID where the restore takes place", accessMode = READ_ONLY)
  @Column(nullable = false)
  public UUID universeUUID;

  @ApiModelProperty(
      value = "Source universe UUID that created the backup for the restore",
      accessMode = READ_ONLY)
  @Column
  public UUID sourceUniverseUUID;

  @ApiModelProperty(value = "Storage Config UUID that created the backup", accessMode = READ_ONLY)
  @Column
  public UUID storageConfigUUID;

  @ApiModelProperty(value = "Universe name that created the backup", accessMode = READ_ONLY)
  @Column
  public String sourceUniverseName;

  @Column(nullable = false)
  @Enumerated(EnumType.STRING)
  @ApiModelProperty(value = "State of the restore", accessMode = READ_ONLY)
  private State state;

  public State getState() {
    return state;
  }

  public void setState(State state) {
    this.state = state;
  }

  @ApiModelProperty(value = "Restore task UUID", accessMode = READ_ONLY)
  @Column(unique = true)
  public UUID taskUUID;

  public UUID getTaskUUID() {
    return taskUUID;
  }

  public void setTaskUUID(UUID uuid) {
    this.taskUUID = uuid;
  }

  @ApiModelProperty(value = "Restore size in bytes", accessMode = READ_ONLY)
  @Column
  public long restoreSizeInBytes = 0L;

  @CreatedTimestamp private Date createTime;

  public Date getCreateTime() {
    return createTime;
  }

  public void setCreateTime(Date createTime) {
    this.createTime = createTime;
  }

  @UpdatedTimestamp private Date updateTime;

  public Date getUpdateTime() {
    return updateTime;
  }

  public void setUpdateTime(Date updateTime) {
    this.updateTime = updateTime;
  }

  private static final Multimap<State, State> ALLOWED_TRANSITIONS =
      ImmutableMultimap.<State, State>builder()
          .put(State.Created, State.InProgress)
          .put(State.Created, State.Success)
          .put(State.Created, State.Failed)
          .put(State.Created, State.Aborted)
          .put(State.InProgress, State.Success)
          .put(State.InProgress, State.Failed)
          .put(State.InProgress, State.Aborted)
          .put(State.Aborted, State.InProgress)
          .put(State.Aborted, State.Success)
          .put(State.Aborted, State.Failed)
          .build();

  public static List<TaskInfo> restoreSubtasksList = new ArrayList<>();

  public static List<TaskInfo> getRestoreSubtasks(List<TaskInfo> taskInfo) {
    if (restoreSubtasksList.size() != 0) {
      return restoreSubtasksList;
    }
    for (TaskInfo task : taskInfo) {
      if (task.getTaskType() == TaskType.RestoreBackupYb
          || task.getTaskType() == TaskType.RestoreBackupYbc) {
        restoreSubtasksList.add(task);
      }
    }
    return restoreSubtasksList;
  }

  public static State fetchStateFromTaskInfoState(TaskInfo.State state) {
    State restoreState = State.Created;
    for (State s : State.values()) {
      if (s.allowedStates().contains(state)) {
        restoreState = s;
      } else {
        LOG.error("{} is not a valid state of {} in Tasktype Restore", state, s);
      }
    }
    return restoreState;
  }

  public static Restore create(TaskInfo taskInfo) {
    Restore restore = new Restore();
    RestoreBackupParams taskDetails =
        Json.fromJson(taskInfo.getTaskDetails(), RestoreBackupParams.class);
    restore.restoreUUID = taskDetails.prefixUUID;
    restore.universeUUID = taskDetails.universeUUID;
    restore.customerUUID = taskDetails.customerUUID;
    restore.taskUUID = taskInfo.getTaskUUID();

    String storageLocation = taskDetails.backupStorageInfoList.get(0).storageLocation;

    Matcher matcher = PATTERN.matcher(storageLocation);
    if (matcher.find()) {
      restore.sourceUniverseUUID = UUID.fromString(matcher.group(0));
    }
    boolean isSourceUniversePresent = BackupUtil.checkIfUniverseExists(restore.sourceUniverseUUID);
    restore.sourceUniverseName =
        isSourceUniversePresent ? Universe.getOrBadRequest(restore.sourceUniverseUUID).name : "";
    restore.storageConfigUUID = taskDetails.storageConfigUUID;
    restore.setState(Restore.fetchStateFromTaskInfoState(taskInfo.getTaskState()));
    restore.setCreateTime(taskInfo.getCreationTime());
    restore.setUpdateTime(taskInfo.getLastUpdateTime());
    restore.save();
    return restore;
  }

  public void update(UUID taskUUID, TaskInfo.State finalState) {
    TaskInfo taskInfo = TaskInfo.getOrBadRequest(taskUUID);
    State newRestoreState = fetchStateFromTaskInfoState(finalState);
    if (getState().equals(newRestoreState)) {
      LOG.debug("Skipping state transition as restore is already in the {} state", getState());
    } else if (ALLOWED_TRANSITIONS.containsEntry(getState(), newRestoreState)) {
      LOG.debug("Restore state transitioned from {} to {}", getState(), newRestoreState);
      setState(newRestoreState);
    } else {
      LOG.error("Ignored INVALID STATE TRANSITION  {} -> {}", getState(), newRestoreState);
    }
    setUpdateTime(taskInfo.getLastUpdateTime());
    save();
  }

  public void updateTaskUUID(UUID taskUUID) {
    setTaskUUID(taskUUID);
    save();
  }

  public static void updateRestoreSizeForRestore(UUID restoreKeyspaceSubTaskUUID, long backupSize) {
    UUID restoreTaskUUID = TaskInfo.getOrBadRequest(restoreKeyspaceSubTaskUUID).getParentUUID();
    Restore restore = Restore.fetchByTaskUUID(restoreTaskUUID).get(0);
    UniverseDefinitionTaskParams universeTaskParams =
        Universe.getOrBadRequest(restore.universeUUID).getUniverseDetails();
    int replicationFactor = universeTaskParams.getPrimaryCluster().userIntent.replicationFactor;
    restore.restoreSizeInBytes += replicationFactor * backupSize;
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
    Restore restore = find.byId(restoreUUID);
    if (restore == null) {
      LOG.trace("Cannot find restore {}", restoreUUID);
      return Optional.empty();
    }
    return Optional.of(restore);
  }

  public static RestoreResp toRestoreResp(Restore restore) {

    String targetUniverseName =
        BackupUtil.checkIfUniverseExists(restore.universeUUID)
            ? Universe.getOrBadRequest(restore.universeUUID).name
            : "";
    Boolean isSourceUniversePresent = BackupUtil.checkIfUniverseExists(restore.sourceUniverseUUID);
    State state = restore.getState();
    List<RestoreKeyspace> restoreKeyspaceList =
        RestoreKeyspace.fetchRestoreKeyspaceFromRestoreUUID(restore.restoreUUID);
    RestoreRespBuilder builder =
        RestoreResp.builder()
            .restoreUUID(restore.restoreUUID)
            .creationTime(restore.getCreateTime())
            .updateTime(restore.getUpdateTime())
            .targetUniverseName(targetUniverseName)
            .sourceUniverseName(restore.sourceUniverseName)
            .customerUUID(restore.customerUUID)
            .universeUUID(restore.universeUUID)
            .state(state)
            .restoreSizeInBytes(restore.restoreSizeInBytes)
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
          "t0.source_universe_uuid not in" + "(select U.universe_uuid from universe U)";
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
