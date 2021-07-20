// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;
import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.YWServiceException;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.models.helpers.TaskType;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.DbJson;
import io.ebean.annotation.EnumValue;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.text.SimpleDateFormat;
import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.EnumType;
import javax.persistence.Enumerated;
import javax.persistence.Id;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;

@Entity
@ApiModel(description = "Scheduled backup")
public class Schedule extends Model {
  public static final Logger LOG = LoggerFactory.getLogger(Schedule.class);
  SimpleDateFormat tsFormat = new SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss");

  public enum State {
    @EnumValue("Active")
    Active,

    @EnumValue("Paused")
    Paused,

    @EnumValue("Stopped")
    Stopped,
  }

  private static final int MAX_FAIL_COUNT = 3;

  @Id
  @ApiModelProperty(value = "Schedule UUID", accessMode = READ_ONLY)
  public UUID scheduleUUID;

  public UUID getScheduleUUID() {
    return scheduleUUID;
  }

  @ApiModelProperty(value = "Customer uuid", accessMode = READ_ONLY)
  @Column(nullable = false)
  private UUID customerUUID;

  public UUID getCustomerUUID() {
    return customerUUID;
  }

  @ApiModelProperty(value = "Number of failed schedule", accessMode = READ_ONLY)
  @Column(nullable = false, columnDefinition = "integer default 0")
  private int failureCount;

  public int getFailureCount() {
    return failureCount;
  }

  @ApiModelProperty(value = "Frequency of the schedule", accessMode = READ_WRITE)
  @Column(nullable = false)
  private long frequency;

  public long getFrequency() {
    return frequency;
  }

  @ApiModelProperty(value = "Schedule task params", accessMode = READ_WRITE)
  @Column(nullable = false, columnDefinition = "TEXT")
  @DbJson
  private JsonNode taskParams;

  public JsonNode getTaskParams() {
    return taskParams;
  }

  @ApiModelProperty(value = "Type of the task to be schedules", accessMode = READ_WRITE)
  @Column(nullable = false)
  @Enumerated(EnumType.STRING)
  private TaskType taskType;

  public TaskType getTaskType() {
    return taskType;
  }

  @ApiModelProperty(value = "Status of the task", accessMode = READ_ONLY)
  @Column(nullable = false)
  @Enumerated(EnumType.STRING)
  private State status = State.Active;

  public State getStatus() {
    return status;
  }

  @Column
  @ApiModelProperty(value = "Cron expression for schedule")
  private String cronExpression;

  public String getCronExpression() {
    return cronExpression;
  }

  public void setCronExpression(String cronExpression) {
    this.cronExpression = cronExpression;
  }

  public static final Finder<UUID, Schedule> find = new Finder<UUID, Schedule>(Schedule.class) {};

  public static Schedule create(
      UUID customerUUID, ITaskParams params, TaskType taskType, long frequency) {
    return create(customerUUID, params, taskType, frequency, null);
  }

  public static Schedule create(
      UUID customerUUID,
      ITaskParams params,
      TaskType taskType,
      long frequency,
      String cronExpression) {
    Schedule schedule = new Schedule();
    schedule.scheduleUUID = UUID.randomUUID();
    schedule.customerUUID = customerUUID;
    schedule.failureCount = 0;
    schedule.taskType = taskType;
    schedule.taskParams = Json.toJson(params);
    schedule.frequency = frequency;
    schedule.status = State.Active;
    schedule.cronExpression = cronExpression;
    schedule.save();
    return schedule;
  }

  /** DEPRECATED: use {@link #getOrBadRequest()} */
  @Deprecated
  public static Schedule get(UUID scheduleUUID) {
    return find.query().where().idEq(scheduleUUID).findOne();
  }

  public static Schedule getOrBadRequest(UUID scheduleUUID) {
    Schedule schedule = get(scheduleUUID);
    if (schedule == null) {
      throw new YWServiceException(BAD_REQUEST, "Invalid Schedule UUID: " + scheduleUUID);
    }
    return schedule;
  }

  public static List<Schedule> getAll() {
    return find.query().findList();
  }

  public static List<Schedule> getAllActiveByCustomerUUID(UUID customerUUID) {
    return find.query().where().eq("customer_uuid", customerUUID).eq("status", "Active").findList();
  }

  public static List<Schedule> getAllActive() {
    return find.query().where().eq("status", "Active").findList();
  }

  public static boolean existsStorageConfig(UUID customerConfigUUID) {
    List<Schedule> scheduleList =
        find.query()
            .where()
            .or()
            .eq("task_type", TaskType.BackupUniverse)
            .eq("task_type", TaskType.MultiTableBackup)
            .endOr()
            .eq("status", "Active")
            .findList();
    // This should be safe to do since storageConfigUUID is a required constraint.
    scheduleList =
        scheduleList
            .stream()
            .filter(
                s ->
                    s.getTaskParams()
                        .path("storageConfigUUID")
                        .asText()
                        .equals(customerConfigUUID.toString()))
            .collect(Collectors.toList());
    return scheduleList.size() != 0;
  }

  public void setFailureCount(int count) {
    this.failureCount = count;
    if (count >= MAX_FAIL_COUNT) {
      this.status = State.Paused;
    }
    save();
  }

  public void resetSchedule() {
    this.status = State.Active;
    save();
  }

  public void stopSchedule() {
    this.status = State.Stopped;
    save();
  }
}
