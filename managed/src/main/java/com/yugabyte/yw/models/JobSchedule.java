// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.helpers.schedule.JobConfig;
import com.yugabyte.yw.models.helpers.schedule.JobConfig.JobConfigWrapper;
import com.yugabyte.yw.models.helpers.schedule.ScheduleConfig;
import io.ebean.DB;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.DbJson;
import io.ebean.annotation.WhenModified;
import jakarta.persistence.Column;
import jakarta.persistence.Entity;
import jakarta.persistence.EnumType;
import jakarta.persistence.Enumerated;
import jakarta.persistence.Id;
import jakarta.persistence.PreUpdate;
import java.io.IOException;
import java.time.Duration;
import java.time.Instant;
import java.time.temporal.ChronoUnit;
import java.util.Date;
import java.util.List;
import java.util.Optional;
import java.util.UUID;
import lombok.AccessLevel;
import lombok.Getter;
import lombok.Setter;

/** Schedule for a generic job. */
@Getter
@Setter
@Entity
public class JobSchedule extends Model {
  private static final Finder<UUID, JobSchedule> finder =
      new Finder<UUID, JobSchedule>(JobSchedule.class) {};

  @Id private UUID uuid;

  @Column(nullable = false)
  private UUID customerUuid;

  @Column(nullable = false)
  private String name;

  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  private Date lastStartTime;

  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  private Date lastEndTime;

  @Column(nullable = false)
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  private Date nextStartTime;

  private UUID lastJobInstanceUuid;

  @Column(nullable = false)
  private long failedCount;

  @Column(nullable = false)
  private long executionCount;

  @Column(nullable = false)
  @DbJson
  private ScheduleConfig scheduleConfig;

  @Getter(AccessLevel.NONE)
  @Setter(AccessLevel.NONE)
  @JsonProperty
  @Column(nullable = false)
  @DbJson
  private JobConfigWrapper jobConfig;

  @Enumerated(EnumType.STRING)
  private State state = State.INACTIVE;

  @WhenModified
  @Column(nullable = false)
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  private Date createdAt;

  @WhenModified
  @Column(nullable = false)
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  private Date updatedAt;

  public enum State {
    ACTIVE,
    INACTIVE
  }

  @PreUpdate
  public void preUpdate() throws IOException {
    setUpdatedAt(new Date());
  }

  @JsonIgnore
  @SuppressWarnings("unchecked")
  public <T extends JobConfig> T getJobConfig() {
    return (T) jobConfig.getConfig();
  }

  @JsonIgnore
  public void setJobConfig(JobConfig jobConfig) {
    this.jobConfig = new JobConfigWrapper(jobConfig);
  }

  public static JobSchedule getOrBadRequest(UUID uuid) {
    return JobSchedule.maybeGet(uuid)
        .orElseThrow(
            () ->
                new PlatformServiceException(BAD_REQUEST, "Cannot find node job schedule " + uuid));
  }

  public static Optional<JobSchedule> maybeGet(UUID uuid) {
    return Optional.ofNullable(finder.byId(uuid));
  }

  public static Optional<JobSchedule> maybeGet(UUID customerUuid, String name) {
    return Optional.ofNullable(
        finder.query().where().eq("customerUuid", customerUuid).eq("name", name).findOne());
  }

  public static List<UUID> getNextEnabled(Duration window) {
    Date nextTime = Date.from(Instant.now().plus(window.getSeconds(), ChronoUnit.SECONDS));
    return DB.createQuery(JobSchedule.class)
        .where()
        .le("nextStartTime", nextTime)
        .eq("schedule_config::jsonb->>'disabled'", "false")
        .findIds();
  }

  public static List<JobSchedule> getAll() {
    return finder.all();
  }

  public static List<JobSchedule> getAll(Class<? extends JobConfig> jobConfigClass) {
    return DB.createQuery(JobSchedule.class)
        .where()
        .eq("job_config::jsonb->>'classname'", jobConfigClass.getName())
        .findList();
  }

  public void updateScheduleConfig(ScheduleConfig scheduleConfig) {
    if (db().update(JobSchedule.class)
            .set("scheduleConfig", scheduleConfig)
            .set("updatedAt", new Date())
            .where()
            .eq("uuid", getUuid())
            .update()
        > 0) {
      refresh();
    }
  }
}
