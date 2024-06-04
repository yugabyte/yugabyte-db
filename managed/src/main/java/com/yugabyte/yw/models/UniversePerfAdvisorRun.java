package com.yugabyte.yw.models;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;

import com.fasterxml.jackson.annotation.JsonFormat;
import io.ebean.ExpressionList;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.PersistenceContextScope;
import io.ebean.annotation.EnumValue;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import io.swagger.annotations.ApiModelProperty.AccessMode;
import jakarta.persistence.Entity;
import jakarta.persistence.Id;
import java.util.Date;
import java.util.Optional;
import java.util.UUID;
import javax.validation.constraints.NotNull;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.experimental.Accessors;

@Entity
@Data
@Accessors(chain = true)
@EqualsAndHashCode(callSuper = false)
@ApiModel("Universe performance advisor status")
public class UniversePerfAdvisorRun extends Model {
  public enum State {
    @EnumValue("PENDING")
    PENDING,
    @EnumValue("RUNNING")
    RUNNING,
    @EnumValue("COMPLETED")
    COMPLETED,
    @EnumValue("FAILED")
    FAILED
  }

  private static final Finder<UUID, UniversePerfAdvisorRun> find =
      new Finder<UUID, UniversePerfAdvisorRun>(UniversePerfAdvisorRun.class) {};

  @Id
  @NotNull
  @ApiModelProperty(value = "Perf advisor run UUID", accessMode = READ_ONLY)
  private UUID uuid;

  @NotNull
  @ApiModelProperty(value = "Customer UUID", accessMode = READ_ONLY)
  private UUID customerUUID;

  @NotNull
  @ApiModelProperty(value = "Universe UUID", accessMode = READ_ONLY)
  private UUID universeUUID;

  @ApiModelProperty(value = "State", accessMode = AccessMode.READ_ONLY)
  @NotNull
  private State state;

  @ApiModelProperty(
      value = "Time perf advisor run was scheduled",
      example = "2022-12-12T13:07:18Z",
      accessMode = AccessMode.READ_ONLY)
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  private Date scheduleTime;

  @ApiModelProperty(
      value = "Time perf advisor run was started",
      example = "2022-12-12T13:07:18Z",
      accessMode = AccessMode.READ_ONLY)
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  private Date startTime;

  @ApiModelProperty(
      value = "Time perf advisor run was finished",
      example = "2022-12-12T13:07:18Z",
      accessMode = AccessMode.READ_ONLY)
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  private Date endTime;

  @ApiModelProperty(value = "Scheduled or manual run", accessMode = AccessMode.READ_ONLY)
  private boolean manual;

  public static UniversePerfAdvisorRun create(
      UUID customerUUID, UUID universeUUID, boolean manual) {
    UniversePerfAdvisorRun run =
        new UniversePerfAdvisorRun()
            .setUuid(UUID.randomUUID())
            .setCustomerUUID(customerUUID)
            .setUniverseUUID(universeUUID)
            .setState(State.PENDING)
            .setScheduleTime(new Date())
            .setManual(manual);
    run.save();
    return run;
  }

  public static UniversePerfAdvisorRun get(UUID uuid) {
    return find.query()
        .setPersistenceContextScope(PersistenceContextScope.QUERY)
        .where()
        .eq("uuid", uuid)
        .findOne();
  }

  public static int deleteOldRuns(UUID customerUUID, Date beforeDate) {
    return find.query()
        .where()
        .eq("customerUUID", customerUUID)
        .lt("scheduleTime", beforeDate)
        .delete();
  }

  public static Optional<UniversePerfAdvisorRun> getLastRun(
      UUID customerUUID, UUID universeUUID, boolean onlyScheduled) {
    ExpressionList<UniversePerfAdvisorRun> expr =
        find.query()
            .setPersistenceContextScope(PersistenceContextScope.QUERY)
            .where()
            .eq("customerUUID", customerUUID)
            .eq("universeUUID", universeUUID);
    if (onlyScheduled) {
      expr.eq("manual", false);
    }
    return expr.orderBy().desc("scheduleTime").setMaxRows(1).findOneOrEmpty();
  }
}
