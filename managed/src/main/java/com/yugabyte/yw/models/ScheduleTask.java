// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import com.avaje.ebean.*;
import com.avaje.ebean.annotation.DbJson;
import com.avaje.ebean.annotation.EnumValue;
import com.fasterxml.jackson.annotation.JsonFormat;
import com.fasterxml.jackson.databind.JsonNode;

import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.forms.ITaskParams;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;

import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.Id;
import javax.persistence.EnumType;
import javax.persistence.Enumerated;

import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;

import static java.lang.Math.abs;

@Entity
public class ScheduleTask extends Model {
  public static final Logger LOG = LoggerFactory.getLogger(ScheduleTask.class);
  SimpleDateFormat tsFormat = new SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss");

  @Id
  public UUID taskUUID;
  public UUID getTaskUUID() { return taskUUID; }

  @Column(nullable = false)
  private UUID scheduleUUID;
  public UUID getScheduleUUID() { return scheduleUUID; }

  @Column
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd HH:mm:ss")
  private Date completedTime;
  public Date getCompletedTime() { return completedTime; }
  
  @Column
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd HH:mm:ss")
  private Date scheduledTime;
  public Date getScheduledTime() { return scheduledTime; }

  public static final Find<UUID, ScheduleTask> find = new Find<UUID, ScheduleTask>(){};

  public static ScheduleTask create(UUID taskUUID, UUID scheduleUUID) {
    ScheduleTask task = new ScheduleTask();
    task.scheduleUUID = scheduleUUID;
    task.taskUUID = taskUUID;
    task.scheduledTime = new Date();
    task.save();
    return task;
  }

  public static ScheduleTask fetchByTaskUUID(UUID taskUUID) {
    return find.where().eq("task_uuid", taskUUID).findUnique();
  }

  public static List<ScheduleTask> getAll() {
    return find.findList();
  }

  public static ScheduleTask getLastTask(UUID scheduleUUID) {
    List<ScheduleTask> tasks = find.where().eq("schedule_uuid", scheduleUUID).orderBy("scheduled_time desc").findList();
    if (tasks.isEmpty()) {
      return null;
    }
    return tasks.get(0);
  }

  public void setCompletedTime() {
    this.completedTime = new Date();
    save();
  }
}
