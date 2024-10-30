// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.filters.JobInstanceFilter;
import com.yugabyte.yw.models.paging.PagedQuery;
import com.yugabyte.yw.models.paging.PagedQuery.SortByIF;
import io.ebean.DB;
import io.ebean.ExpressionList;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.Query;
import io.ebean.annotation.WhenModified;
import jakarta.persistence.Column;
import jakarta.persistence.Entity;
import jakarta.persistence.EnumType;
import jakarta.persistence.Enumerated;
import jakarta.persistence.Id;
import java.time.Duration;
import java.time.Instant;
import java.time.temporal.ChronoUnit;
import java.util.Date;
import java.util.List;
import java.util.Optional;
import java.util.UUID;
import java.util.concurrent.Delayed;
import java.util.concurrent.TimeUnit;
import javax.annotation.Nullable;
import lombok.Getter;
import lombok.Setter;

@Getter
@Setter
@Entity
public class JobInstance extends Model implements Delayed {
  private static final Finder<UUID, JobInstance> finder =
      new Finder<UUID, JobInstance>(JobInstance.class) {};

  @Id private UUID uuid;

  @Column(nullable = false)
  private UUID jobScheduleUuid;

  @Column(nullable = false)
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  private Date startTime;

  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  private Date endTime;

  @Enumerated(EnumType.STRING)
  private State state = State.SCHEDULED;

  @WhenModified
  @Column(nullable = false)
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  private Date createdAt;

  public enum State {
    FAILED,
    RUNNING,
    SUCCESS,
    SCHEDULED,
    SKIPPED,
  }

  public enum SortBy implements PagedQuery.SortByIF {
    uuid("uuid"),
    jobScheduleUuid("jobScheduleUuid"),
    state("state"),
    createdAt("updatedAt"),
    @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
    startTime("startTime"),
    @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
    endTime("endTime");

    private final String sortField;

    SortBy(String sortField) {
      this.sortField = sortField;
    }

    public String getSortField() {
      return sortField;
    }

    @Override
    public SortByIF getOrderField() {
      return SortBy.uuid;
    }
  }

  public static JobInstance getOrBadRequest(UUID uuid) {
    return JobInstance.maybeGet(uuid)
        .orElseThrow(
            () ->
                new PlatformServiceException(BAD_REQUEST, "Cannot find node job instance " + uuid));
  }

  public static Optional<JobInstance> maybeGet(UUID uuid) {
    return Optional.ofNullable(finder.byId(uuid));
  }

  public static List<JobInstance> getAll(UUID jobScheduleUuid) {
    return finder.query().where().eq("jobScheduleUuid", jobScheduleUuid).findList();
  }

  public static List<JobInstance> getAll() {
    return finder.all();
  }

  public static int deleteExpired(Duration ttl) {
    Date expiryTime = Date.from(Instant.now().minus(ttl.toMinutes(), ChronoUnit.MINUTES));
    Query<JobSchedule> subQuery =
        DB.createQuery(JobSchedule.class)
            .where()
            .isNotNull("lastJobInstanceUuid")
            .raw("expired_instance.uuid = last_job_instance_uuid")
            .query();
    return (int)
        finder
            .query()
            .alias("expired_instance")
            .where()
            .isNotNull("endTime")
            .le("endTime", expiryTime)
            .notExists(subQuery)
            .findList()
            .stream()
            .filter(JobInstance::delete)
            .count();
  }

  public static int updateAllPending(State finalState) {
    return updateAllPending(finalState, null);
  }

  public static int updateAllPending(State finalState, @Nullable State expectedState) {
    ExpressionList<JobInstance> query =
        finder
            .update()
            .set("state", finalState)
            .set("endTime", new Date())
            .where()
            .isNull("endTime");
    if (expectedState != null) {
      query = query.eq("state", expectedState);
    }
    return query.update();
  }

  public static ExpressionList<JobInstance> createQuery(
      UUID scheduleUuid, JobInstanceFilter filter) {
    ExpressionList<JobInstance> query =
        DB.createQuery(JobInstance.class).where().eq("jobScheduleUuid", scheduleUuid);
    if (filter.getState() != null) {
      query.eq("state", filter.getState());
    }
    if (filter.getStartWindowSecs() > 0) {
      Instant now = Instant.now();
      query.ge("startTime", Date.from(now));
      query.le("startTime", Date.from(now.plus(filter.getStartWindowSecs(), ChronoUnit.SECONDS)));
    }
    return query;
  }

  @Override
  public int compareTo(Delayed delayed) {
    JobInstance other = (JobInstance) delayed;
    return getStartTime().compareTo(other.getStartTime());
  }

  @Override
  public long getDelay(TimeUnit unit) {
    return unit.convert(
        getStartTime().getTime() - System.currentTimeMillis(), TimeUnit.MILLISECONDS);
  }
}
