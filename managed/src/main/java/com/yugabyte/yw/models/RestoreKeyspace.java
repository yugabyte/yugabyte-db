// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.google.common.collect.ImmutableMultimap;
import com.google.common.collect.ImmutableSet;
import com.google.common.collect.Multimap;
import com.yugabyte.yw.common.backuprestore.BackupUtil;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.forms.RestoreBackupParams;
import io.ebean.ExpressionList;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.CreatedTimestamp;
import io.ebean.annotation.DbJson;
import io.ebean.annotation.EnumValue;
import io.ebean.annotation.UpdatedTimestamp;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Date;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.UUID;
import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.EnumType;
import javax.persistence.Enumerated;
import javax.persistence.Id;
import lombok.Getter;
import lombok.Setter;
import org.apache.commons.collections.CollectionUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@ApiModel(description = "Keyspace level restores")
@Entity
@Getter
@Setter
public class RestoreKeyspace extends Model {
  public static final Logger LOG = LoggerFactory.getLogger(RestoreKeyspace.class);
  public static final Finder<UUID, RestoreKeyspace> find =
      new Finder<UUID, RestoreKeyspace>(RestoreKeyspace.class) {};

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

  @ApiModelProperty(value = "Restore keyspace UUID", accessMode = READ_ONLY)
  @Id
  private UUID uuid;

  @ApiModelProperty(value = "Universe-level Restore UUID", accessMode = READ_ONLY)
  @Column(nullable = false)
  private UUID restoreUUID;

  @ApiModelProperty(value = "Restore Keyspace task UUID", accessMode = READ_ONLY)
  @Column(nullable = false)
  private UUID taskUUID;

  @ApiModelProperty(value = "Source keyspace name", accessMode = READ_ONLY)
  @Column
  private String sourceKeyspace;

  @ApiModelProperty(value = "Storage location name", accessMode = READ_ONLY)
  @Column
  private String storageLocation;

  @ApiModelProperty(value = "Target keyspace name", accessMode = READ_ONLY)
  @Column
  private String targetKeyspace;

  @ApiModelProperty(value = "Restored Table name List", accessMode = READ_ONLY)
  @Column(columnDefinition = "TEXT")
  @DbJson
  private List<String> tableNameList;

  @Enumerated(EnumType.STRING)
  @ApiModelProperty(value = "State of the keyspace restore", accessMode = READ_ONLY)
  private State state;

  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  @ApiModelProperty(value = "RestoreKeyspace task creation time", example = "2022-12-12T13:07:18Z")
  @CreatedTimestamp
  private Date createTime;

  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  @ApiModelProperty(
      value = "RestoreKeyspace task completion time",
      example = "2022-12-12T13:07:18Z")
  @UpdatedTimestamp
  private Date completeTime;

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
          .put(State.Failed, State.Aborted)
          .build();

  public static State fetchStateFromTaskInfoState(TaskInfo.State state) {
    State restoreState = State.Created;
    for (State s : State.values()) {
      if (s.allowedStates().contains(state)) {
        restoreState = s;
      } else {
        LOG.error("{} is not a valid state of {} in Tasktype RestoreKeyspace", state, s);
      }
    }
    return restoreState;
  }

  public static RestoreKeyspace create(UUID taskUUID, RestoreBackupParams taskDetails) {
    RestoreKeyspace restoreKeyspace = new RestoreKeyspace();
    restoreKeyspace.setUuid(UUID.randomUUID());

    restoreKeyspace.setRestoreUUID(taskDetails.prefixUUID);
    restoreKeyspace.setTaskUUID(taskUUID);

    if (!CollectionUtils.isEmpty(taskDetails.backupStorageInfoList)) {
      RestoreBackupParams.BackupStorageInfo storageInfo = taskDetails.backupStorageInfoList.get(0);
      restoreKeyspace.setStorageLocation(storageInfo.storageLocation);
      restoreKeyspace.setTargetKeyspace(storageInfo.keyspace);
      restoreKeyspace.setSourceKeyspace(
          BackupUtil.getKeyspaceFromStorageLocation(restoreKeyspace.getStorageLocation()));
      restoreKeyspace.setTableNameList(storageInfo.tableNameList);
    }
    restoreKeyspace.state = RestoreKeyspace.State.InProgress;
    restoreKeyspace.save();
    return restoreKeyspace;
  }

  public long getBackupSizeFromStorageLocation() {
    if (getStorageLocation() == null) {
      return 0L;
    }
    long backupSize = getBackupSizeFromStorageLocation(getStorageLocation());
    return backupSize;
  }

  public long getBackupSizeFromStorageLocation(String storageLocation) {
    Optional<BackupTableParams> backupParams =
        Backup.findBackupParamsWithStorageLocation(storageLocation);
    if (backupParams.isPresent()) {
      if (!Objects.isNull(backupParams.get().backupSizeInBytes)) {
        return backupParams.get().backupSizeInBytes;
      }
    }
    return 0L;
  }

  public static void update(Restore restore, TaskInfo.State parentState) {
    List<RestoreKeyspace> restoreKeyspaceList =
        fetchRestoreKeyspaceFromRestoreUUID(restore.getRestoreUUID());
    State restoreKeyspaceState = fetchStateFromTaskInfoState(parentState);
    for (RestoreKeyspace restoreKeyspace : restoreKeyspaceList) {
      restoreKeyspace.update(restoreKeyspace.getTaskUUID(), restoreKeyspaceState);
    }
  }

  public void updateTaskUUID(UUID taskUUID) {
    setTaskUUID(taskUUID);
    save();
  }

  public void update(UUID taskUUID, State state) {
    TaskInfo taskInfo = TaskInfo.getOrBadRequest(taskUUID);
    TaskInfo.State taskState = taskInfo.getTaskState();
    State newRestoreKeyspaceState =
        (state != null) ? state : fetchStateFromTaskInfoState(taskState);
    if (getState().equals(newRestoreKeyspaceState)) {
      LOG.debug(
          "Skipping state transition as restore keyspace is already in the {} state", getState());
    } else if (ALLOWED_TRANSITIONS.containsEntry(getState(), newRestoreKeyspaceState)) {
      LOG.debug(
          "Restore Keyspace state transitioned from {} to {}", getState(), newRestoreKeyspaceState);
      setState(newRestoreKeyspaceState);
    } else {
      LOG.error("Ignored INVALID STATE TRANSITION  {} -> {}", getState(), newRestoreKeyspaceState);
    }
    setCompleteTime(taskInfo.getUpdateTime());
    save();
  }

  public static Optional<RestoreKeyspace> fetchRestoreKeyspace(UUID restoreKeyspaceUUID) {
    if (restoreKeyspaceUUID == null) {
      return Optional.empty();
    }
    RestoreKeyspace restoreKeyspace = find.byId(restoreKeyspaceUUID);
    if (restoreKeyspace == null) {
      LOG.trace("Cannot find restoreKeyspace {}", restoreKeyspaceUUID);
      return Optional.empty();
    }
    return Optional.of(restoreKeyspace);
  }

  public static Optional<RestoreKeyspace> fetchRestoreKeyspace(
      UUID restoreUUID, String targetKeyspace, String location) {
    ExpressionList<RestoreKeyspace> query =
        RestoreKeyspace.find
            .query()
            .where()
            .eq("restore_uuid", restoreUUID)
            .eq("target_keyspace", targetKeyspace)
            .eq("storage_location", location);
    List<RestoreKeyspace> restoreKeyspaceList = query.findList();
    if (restoreKeyspaceList.size() == 0) {
      LOG.trace(
          "Cannot find restoreKeyspace with restoreUUID {} and target keyspace {}",
          restoreUUID,
          targetKeyspace);
      return Optional.empty();
    }
    return Optional.of(restoreKeyspaceList.get(0));
  }

  public static List<RestoreKeyspace> fetchRestoreKeyspaceFromRestoreUUID(UUID restoreUUID) {
    ExpressionList<RestoreKeyspace> query =
        RestoreKeyspace.find.query().where().eq("restore_uuid", restoreUUID);
    return query.findList();
  }

  public static List<RestoreKeyspace> fetchByTaskUUID(UUID taskUUID) {
    List<RestoreKeyspace> restoreKeyspaceList =
        RestoreKeyspace.find.query().where().eq("task_uuid", taskUUID).findList();
    return restoreKeyspaceList;
  }
}
