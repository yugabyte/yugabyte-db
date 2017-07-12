// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import java.util.Date;
import java.util.UUID;

import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.EnumType;
import javax.persistence.Enumerated;
import javax.persistence.Id;

import com.avaje.ebean.Model;
import com.avaje.ebean.annotation.CreatedTimestamp;
import com.avaje.ebean.annotation.DbJson;
import com.avaje.ebean.annotation.EnumValue;
import com.avaje.ebean.annotation.UpdatedTimestamp;
import com.fasterxml.jackson.databind.JsonNode;

import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.models.helpers.TaskType;
import play.data.validation.Constraints;

@Entity
public class TaskInfo extends Model {

  /**
   * These are the various states of the task and taskgroup.
   */
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
  }

  // The task UUID.
  @Id
  private UUID uuid;

  // The UUID of the parent task (if any; CustomerTasks have no parent)
  private UUID parentUuid;

  // The position within the parent task's taskQueue (-1 for a CustomerTask)
  @Column(columnDefinition = "integer default -1")
  private Integer position = -1;

  // The task type.
  @Column(nullable = false)
  @Enumerated(EnumType.STRING)
  private TaskType taskType;

  // The task state.
  @Column(nullable = false)
  @Enumerated(EnumType.STRING)
  private State taskState = State.Created;

  // The subtask group type (if it is a subtask)
  @Enumerated(EnumType.STRING)
  private UserTaskDetails.SubTaskGroupType subTaskGroupType;

  // The task creation time.
  @CreatedTimestamp
  private Date createTime;

  // The task update time. Time of the latest update (including heartbeat updates) on this task.
  @UpdatedTimestamp
  private Date updateTime;

  // The percentage completeness of the task, which is a number from 0 to 100.
  @Column(columnDefinition = "integer default 0")
  private Integer percentDone = 0;

  // Details of the task, usually a JSON representation of the incoming task. This is used to
  // describe the details of the task that is being executed.
  @Constraints.Required
  @Column(nullable = false)
  @DbJson
  private JsonNode details;

  // Identifier of the process owning the task.
  @Constraints.Required
  @Column(nullable = false)
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

  public JsonNode getTaskDetails() {
    return details;
  }

  public State getTaskState() {
    return taskState;
  }

  public TaskType getTaskType() {
    return taskType;
  }

  public UUID getTaskUUID() {
    return uuid;
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

  public static final Find<UUID, TaskInfo> find = new Find<UUID, TaskInfo>(){};

  public static TaskInfo get(UUID taskUUID) {
    // Return the instance details object.
    return find.byId(taskUUID);
  }

  @Override
  public String toString() {
    StringBuilder sb = new StringBuilder();
    sb.append("taskType : " + taskType);
    sb.append(", ");
    sb.append("taskState: " + taskState);
    return sb.toString();
  }
}
