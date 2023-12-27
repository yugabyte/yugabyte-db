// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static com.yugabyte.yw.commissioner.UserTaskDetails.createSubTask;
import static com.yugabyte.yw.models.helpers.CommonUtils.appendInClause;
import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.Sets;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskDetails;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.helpers.TaskType;
import io.ebean.ExpressionList;
import io.ebean.FetchGroup;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.Query;
import io.ebean.annotation.WhenCreated;
import io.ebean.annotation.DbJson;
import io.ebean.annotation.EnumValue;
import io.ebean.annotation.WhenModified;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Collection;
import java.util.Collections;
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
import javax.persistence.EnumType;
import javax.persistence.Enumerated;
import javax.persistence.Id;
import org.apache.commons.collections4.CollectionUtils;
import play.data.validation.Constraints;

@Entity
@ApiModel(description = "Task information")
public class TaskInfo extends Model {

  private static final FetchGroup<TaskInfo> GET_SUBTASKS_FG =
      FetchGroup.of(TaskInfo.class, "uuid, subTaskGroupType, taskState");

  public static final Set<State> COMPLETED_STATES =
      Sets.immutableEnumSet(State.Success, State.Failure, State.Aborted);

  public static final Set<State> ERROR_STATES = Sets.immutableEnumSet(State.Failure, State.Aborted);

  public static final Set<State> INCOMPLETE_STATES =
      Sets.immutableEnumSet(State.Created, State.Initializing, State.Running, State.Abort);

  /** These are the various states of the task and taskgroup. */
  public enum State {
    @EnumValue("Created")
    Created,

    @EnumValue("Initializing")
    Initializing,

    @EnumValue("Running")
    Running,

    @EnumValue("Success")
    Success,

    @EnumValue("Failure")
    Failure,

    @EnumValue("Unknown")
    Unknown,

    @EnumValue("Abort")
    Abort,

    @EnumValue("Aborted")
    Aborted
  }

  // The task UUID.
  @Id
  @ApiModelProperty(value = "Task UUID", accessMode = READ_ONLY)
  private UUID uuid;

  // The UUID of the parent task (if any; CustomerTasks have no parent)
  @ApiModelProperty(value = "Parent task UUID", accessMode = READ_ONLY)
  private UUID parentUuid;

  // The position within the parent task's taskQueue (-1 for a CustomerTask)
  @Column(columnDefinition = "integer default -1")
  @ApiModelProperty(
      value = "The task's position with its parent task's queue",
      accessMode = READ_ONLY)
  private Integer position = -1;

  // The task type.
  @Column(nullable = false)
  @Enumerated(EnumType.STRING)
  @ApiModelProperty(value = "Task type", accessMode = READ_ONLY)
  private final TaskType taskType;

  // The task state.
  @Column(nullable = false)
  @Enumerated(EnumType.STRING)
  @ApiModelProperty(value = "Task state", accessMode = READ_ONLY)
  private State taskState = State.Created;

  // The subtask group type (if it is a subtask)
  @Enumerated(EnumType.STRING)
  @ApiModelProperty(value = "Subtask type", accessMode = READ_ONLY)
  private UserTaskDetails.SubTaskGroupType subTaskGroupType;

  // The task creation time.
  @WhenCreated
  @ApiModelProperty(value = "Creation time", accessMode = READ_ONLY, example = "1624295239113")
  private Date createTime;

  // The task update time. Time of the latest update (including heartbeat updates) on this task.
  @WhenModified
  @ApiModelProperty(value = "Updated time", accessMode = READ_ONLY, example = "1624295239113")
  private Date updateTime;

  // The percentage completeness of the task, which is a number from 0 to 100.
  @Column(columnDefinition = "integer default 0")
  @ApiModelProperty(value = "Percentage complete", accessMode = READ_ONLY)
  private Integer percentDone = 0;

  // Details of the task, usually a JSON representation of the incoming task. This is used to
  // describe the details of the task that is being executed.
  @Constraints.Required
  @Column(columnDefinition = "TEXT default '{}'", nullable = false)
  @DbJson
  @ApiModelProperty(value = "Task details", accessMode = READ_ONLY, required = true)
  private JsonNode details;

  // Identifier of the process owning the task.
  @Constraints.Required
  @Column(nullable = false)
  @ApiModelProperty(
      value = "ID of the process that owns this task",
      accessMode = READ_ONLY,
      required = true)
  private String owner;

  public TaskInfo(TaskType taskType) {
    this.taskType = taskType;
  }

  public Date getCreationTime() {
    return createTime;
  }

  public Date getLastUpdateTime() {
    return updateTime;
  }

  public UUID getParentUUID() {
    return parentUuid;
  }

  public int getPercentDone() {
    return percentDone;
  }

  public int getPosition() {
    return position;
  }

  public UserTaskDetails.SubTaskGroupType getSubTaskGroupType() {
    return subTaskGroupType;
  }

  @JsonIgnore
  public JsonNode getTaskDetails() {
    return details;
  }

  @JsonIgnore
  public String getErrorMessage() {
    if (details == null || taskState == State.Success) {
      return null;
    }
    JsonNode node = details.get("errorString");
    if (node == null || node.isNull()) {
      return null;
    }
    return node.asText();
  }

  public State getTaskState() {
    return taskState;
  }

  public boolean hasCompleted() {
    return COMPLETED_STATES.contains(taskState);
  }

  public TaskType getTaskType() {
    return taskType;
  }

  public UUID getTaskUUID() {
    return uuid;
  }

  public void setTaskUUID(UUID taskUUID) {
    uuid = taskUUID;
  }

  public void setOwner(String owner) {
    this.owner = owner;
  }

  public void setParentUuid(UUID parentUuid) {
    this.parentUuid = parentUuid;
  }

  public void setPercentDone(int percentDone) {
    this.percentDone = percentDone;
  }

  public void setPosition(int position) {
    this.position = position;
  }

  public void setSubTaskGroupType(UserTaskDetails.SubTaskGroupType subTaskGroupType) {
    this.subTaskGroupType = subTaskGroupType;
  }

  public void setTaskState(State taskState) {
    this.taskState = taskState;
  }

  public void setTaskDetails(JsonNode details) {
    this.details = details;
  }

  public static final Finder<UUID, TaskInfo> find = new Finder<UUID, TaskInfo>(TaskInfo.class) {};

  @Deprecated
  public static TaskInfo get(UUID taskUUID) {
    // Return the instance details object.
    return find.byId(taskUUID);
  }

  public static Optional<TaskInfo> maybeGet(UUID taskUUID) {
    return Optional.ofNullable(get(taskUUID));
  }

  public static TaskInfo getOrBadRequest(UUID taskUUID) {
    TaskInfo taskInfo = get(taskUUID);
    if (taskInfo == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Invalid task info UUID: " + taskUUID);
    }
    return taskInfo;
  }

  public static List<TaskInfo> find(Collection<UUID> taskUUIDs) {
    // Return the instance details object.
    if (CollectionUtils.isEmpty(taskUUIDs)) {
      return Collections.emptyList();
    }
    Set<UUID> uniqueTaskUUIDs = new HashSet<>(taskUUIDs);
    ExpressionList<TaskInfo> query = find.query().where();
    appendInClause(query, "uuid", uniqueTaskUUIDs);
    return query.findList();
  }

  // Returns  partial object
  public List<TaskInfo> getSubTasks() {
    Query<TaskInfo> subTaskQuery =
        TaskInfo.find
            .query()
            .select(GET_SUBTASKS_FG)
            .where()
            .eq("parent_uuid", getTaskUUID())
            .orderBy("position asc");
    return subTaskQuery.findList();
  }

  public List<TaskInfo> getIncompleteSubTasks() {
    return TaskInfo.find
        .query()
        .select(GET_SUBTASKS_FG)
        .where()
        .eq("parent_uuid", getTaskUUID())
        .in("task_state", INCOMPLETE_STATES)
        .findList();
  }

  @Override
  public String toString() {
    StringBuilder sb = new StringBuilder();
    sb.append("taskType : ").append(taskType);
    sb.append(", ");
    sb.append("taskState: ").append(taskState);
    return sb.toString();
  }

  /**
   * Retrieve the UserTaskDetails for the task mapped to this TaskInfo object. Should only be called
   * on the user-level parent task, since only that task will have subtasks. Nothing will break if
   * called on a SubTask, it just won't give you much useful information.
   *
   * @return UserTaskDetails object for this TaskInfo, including info on the state on each of the
   *     subTaskGroups.
   */
  public UserTaskDetails getUserTaskDetails() {
    UserTaskDetails taskDetails = new UserTaskDetails();
    List<TaskInfo> result = getSubTasks();
    Map<SubTaskGroupType, SubTaskDetails> userTasksMap = new HashMap<>();
    boolean customerTaskFailure = TaskInfo.ERROR_STATES.contains(taskState);
    for (TaskInfo taskInfo : result) {
      SubTaskGroupType subTaskGroupType = taskInfo.getSubTaskGroupType();
      if (subTaskGroupType == SubTaskGroupType.Invalid) {
        continue;
      }
      SubTaskDetails subTask = userTasksMap.get(subTaskGroupType);
      if (subTask == null) {
        subTask = createSubTask(subTaskGroupType);
        taskDetails.add(subTask);
      } else {
        if (TaskInfo.ERROR_STATES.contains(subTask.getState())) {
          // If error is set, report the error.
          continue;
        }
        if (State.Running.equals(subTask.getState())) {
          // If no error but at least one them is running, report running.
          continue;
        }
      }
      State subTaskState = taskInfo.getTaskState();
      if (State.Created.equals(subTaskState)) {
        subTask.setState(customerTaskFailure ? State.Unknown : State.Created);
      } else if (!State.Success.equals(subTaskState)) {
        subTask.setState(subTaskState);
      }
      userTasksMap.put(subTaskGroupType, subTask);
    }
    return taskDetails;
  }

  /**
   * Returns the aggregate percentage completion across all the subtasks.
   *
   * @return a number between 0.0 and 100.0.
   */
  public double getPercentCompleted() {
    int numSubtasks = TaskInfo.find.query().where().eq("parent_uuid", getTaskUUID()).findCount();
    if (numSubtasks == 0) {
      if (TaskInfo.COMPLETED_STATES.contains(getTaskState())) {
        return 100.0;
      }
      return 0.0;
    }
    int numSubtasksCompleted =
        TaskInfo.find
            .query()
            .where()
            .eq("parent_uuid", getTaskUUID())
            .eq("task_state", TaskInfo.State.Success)
            .findCount();
    return numSubtasksCompleted * 100.0 / numSubtasks;
  }

  public static List<TaskInfo> findDuplicateDeleteBackupTasks(UUID customerUUID, UUID backupUUID) {
    return TaskInfo.find
        .query()
        .where()
        .eq("task_type", TaskType.DeleteBackup)
        .ne("task_state", State.Failure)
        .ne("task_state", State.Aborted)
        .eq("details->>'customerUUID'", customerUUID.toString())
        .eq("details->>'backupUUID'", backupUUID.toString())
        .findList();
  }

  public static List<TaskInfo> findIncompleteDeleteBackupTasks(UUID customerUUID, UUID backupUUID) {
    return TaskInfo.find
        .query()
        .where()
        .in("task_type", TaskType.DeleteBackup, TaskType.DeleteBackupYb)
        .in("task_state", INCOMPLETE_STATES)
        .eq("details->>'customerUUID'", customerUUID.toString())
        .eq("details->>'backupUUID'", backupUUID.toString())
        .findList();
  }
}
