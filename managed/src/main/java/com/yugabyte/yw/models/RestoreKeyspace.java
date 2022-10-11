// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.google.common.collect.ImmutableSet;
import com.google.common.collect.ImmutableMultimap;
import com.google.common.collect.Multimap;
import com.yugabyte.yw.common.BackupUtil;
import com.yugabyte.yw.forms.RestoreBackupParams;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Restore;
import io.ebean.ExpressionList;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.Query;
import io.ebean.annotation.CreatedTimestamp;
import io.ebean.annotation.DbJson;
import io.ebean.annotation.EnumValue;
import io.ebean.annotation.UpdatedTimestamp;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.List;
import java.util.UUID;
import java.util.Optional;
import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.Id;
import io.ebean.annotation.EnumValue;
import javax.persistence.EnumType;
import javax.persistence.Enumerated;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.api.Play;
import play.libs.Json;

@ApiModel(description = "Keyspace level restores")
@Entity
public class RestoreKeyspace extends Model {
  public static final Logger LOG = LoggerFactory.getLogger(RestoreKeyspace.class);
  SimpleDateFormat tsFormat = new SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss");
  public static final Finder<UUID, RestoreKeyspace> find =
      new Finder<UUID, RestoreKeyspace>(RestoreKeyspace.class) {};

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

  @ApiModelProperty(value = "Restore keyspace UUID", accessMode = READ_ONLY)
  @Id
  public UUID uuid;

  @ApiModelProperty(value = "Universe-level Restore UUID", accessMode = READ_ONLY)
  @Column(nullable = false)
  public UUID restoreUUID;

  @ApiModelProperty(value = "Restore Keyspace task UUID", accessMode = READ_ONLY)
  @Column(nullable = false)
  public UUID taskUUID;

  public UUID getTaskUUID() {
    return taskUUID;
  }

  public void setTaskUUID(UUID uuid) {
    this.taskUUID = uuid;
  }

  @ApiModelProperty(value = "Source keyspace name", accessMode = READ_ONLY)
  @Column
  public String sourceKeyspace;

  @ApiModelProperty(value = "Storage location name", accessMode = READ_ONLY)
  @Column
  public String storageLocation;

  @ApiModelProperty(value = "Target keyspace name", accessMode = READ_ONLY)
  @Column
  public String targetKeyspace;

  @Enumerated(EnumType.STRING)
  @ApiModelProperty(value = "State of the keyspace restore", accessMode = READ_ONLY)
  private State state;

  public State getState() {
    return state;
  }

  public void setState(State state) {
    this.state = state;
  }

  @CreatedTimestamp private Date createTime;

  public Date getCreateTime() {
    return createTime;
  }

  public void setCreateTime(Date createTime) {
    this.createTime = createTime;
  }

  @UpdatedTimestamp private Date completeTime;

  public Date getCompleteTime() {
    return completeTime;
  }

  public void setCompleteTime(Date completeTime) {
    this.completeTime = completeTime;
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

  public static RestoreKeyspace create(TaskInfo taskInfo) {
    RestoreKeyspace restoreKeyspace = new RestoreKeyspace();
    RestoreBackupParams taskDetails =
        Json.fromJson(taskInfo.getTaskDetails(), RestoreBackupParams.class);
    restoreKeyspace.uuid = UUID.randomUUID();

    UUID parentRestoreTaskUUID = taskInfo.getParentUUID();
    restoreKeyspace.restoreUUID = Restore.fetchByTaskUUID(parentRestoreTaskUUID).get(0).restoreUUID;
    restoreKeyspace.taskUUID = taskInfo.getTaskUUID();

    RestoreBackupParams.BackupStorageInfo storageInfo = taskDetails.backupStorageInfoList.get(0);
    restoreKeyspace.storageLocation = storageInfo.storageLocation;
    restoreKeyspace.targetKeyspace = storageInfo.keyspace;
    restoreKeyspace.sourceKeyspace =
        BackupUtil.getKeyspaceFromStorageLocation(restoreKeyspace.storageLocation);
    restoreKeyspace.state = RestoreKeyspace.State.InProgress;
    restoreKeyspace.save();
    return restoreKeyspace;
  }

  public long getBackupSizeFromStorageLocation() {
    long backupSize = getBackupSizeFromStorageLocation(storageLocation);
    return backupSize;
  }

  public long getBackupSizeFromStorageLocation(String storageLocation) {
    Optional<BackupTableParams> backupParams =
        Backup.findBackupParamsWithStorageLocation(storageLocation);
    if (backupParams.isPresent()) {
      return backupParams.get().backupSizeInBytes;
    }
    return 0L;
  }

  public static void update(Restore restore, TaskInfo.State parentState) {
    List<RestoreKeyspace> restoreKeyspaceList =
        fetchRestoreKeyspaceFromRestoreUUID(restore.restoreUUID);
    for (RestoreKeyspace restoreKeyspace : restoreKeyspaceList) {
      restoreKeyspace.update(restoreKeyspace.taskUUID, parentState);
    }
  }

  public void updateTaskUUID(UUID taskUUID) {
    setTaskUUID(taskUUID);
    save();
  }

  public void update(UUID taskUUID, TaskInfo.State state) {
    TaskInfo taskInfo = TaskInfo.getOrBadRequest(taskUUID);
    TaskInfo.State taskState = taskInfo.getTaskState();
    State newRestoreKeyspaceState =
        (state != null)
            ? fetchStateFromTaskInfoState(state)
            : fetchStateFromTaskInfoState(taskState);
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
    setCompleteTime(taskInfo.getLastUpdateTime());
    save();
  }

  public static Optional<RestoreKeyspace> fetchRestoreKeyspace(UUID restoreKeyspaceUUID) {
    RestoreKeyspace restoreKeyspace = find.byId(restoreKeyspaceUUID);
    if (restoreKeyspace == null) {
      LOG.trace("Cannot find restoreKeyspace {}", restoreKeyspaceUUID);
      return Optional.empty();
    }
    return Optional.of(restoreKeyspace);
  }

  public static Optional<RestoreKeyspace> fetchRestoreKeyspaceByRestoreIdAndKeyspaceName(
      UUID restoreUUID, String targetKeyspace) {
    ExpressionList<RestoreKeyspace> query =
        RestoreKeyspace.find
            .query()
            .where()
            .eq("restore_uuid", restoreUUID)
            .eq("target_keyspace", targetKeyspace);
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
