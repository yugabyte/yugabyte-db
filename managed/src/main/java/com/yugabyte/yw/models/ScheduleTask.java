// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.yugabyte.yw.models.TaskInfo.State;
import io.ebean.Finder;
import io.ebean.Model;
import java.util.Date;
import java.util.List;
import java.util.Optional;
import java.util.UUID;
import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.Id;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@Entity
public class ScheduleTask extends Model {
  public static final Logger LOG = LoggerFactory.getLogger(ScheduleTask.class);

  @Id public UUID taskUUID;

  public UUID getTaskUUID() {
    return taskUUID;
  }

  @Column(nullable = false)
  private UUID scheduleUUID;

  public UUID getScheduleUUID() {
    return scheduleUUID;
  }

  @Column
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd HH:mm:ss")
  private Date completedTime;

  public Date getCompletedTime() {
    return completedTime;
  }

  @Column
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd HH:mm:ss")
  private Date scheduledTime;

  public Date getScheduledTime() {
    return scheduledTime;
  }

  public static final Finder<UUID, ScheduleTask> find =
      new Finder<UUID, ScheduleTask>(ScheduleTask.class) {};

  public static ScheduleTask create(UUID taskUUID, UUID scheduleUUID) {
    ScheduleTask task = new ScheduleTask();
    task.scheduleUUID = scheduleUUID;
    task.taskUUID = taskUUID;
    task.scheduledTime = new Date();
    task.save();
    return task;
  }

  public static ScheduleTask fetchByTaskUUID(UUID taskUUID) {
    return find.query().where().eq("task_uuid", taskUUID).findOne();
  }

  public static List<ScheduleTask> getAll() {
    return find.query().findList();
  }

  public static ScheduleTask getLastTask(UUID scheduleUUID) {
    List<ScheduleTask> tasks =
        find.query()
            .where()
            .eq("schedule_uuid", scheduleUUID)
            .orderBy("scheduled_time desc")
            .findList();
    if (tasks.isEmpty()) {
      return null;
    }
    return tasks.get(0);
  }

  public static Optional<ScheduleTask> getLastSuccessfulTask(UUID scheduleUUID) {
    return find.query()
        .where()
        .eq("schedule_uuid", scheduleUUID)
        .orderBy("scheduled_time desc")
        .findList()
        .stream()
        .filter(
            (task) ->
                TaskInfo.getOrBadRequest(task.getTaskUUID()).getTaskState().equals(State.Success))
        .findFirst();
  }

  public static List<ScheduleTask> getAllTasks(UUID scheduleUUID) {
    return find.query().where().eq("schedule_uuid", scheduleUUID).findList();
  }

  public void setCompletedTime() {
    this.completedTime = new Date();
    save();
  }
}
