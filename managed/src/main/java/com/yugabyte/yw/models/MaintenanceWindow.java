/*
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.models;

import static com.yugabyte.yw.models.helpers.CommonUtils.appendInClause;
import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;
import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.yugabyte.yw.forms.filters.AlertConfigurationApiFilter;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
import com.yugabyte.yw.models.filters.MaintenanceWindowFilter;
import com.yugabyte.yw.models.paging.PagedQuery.SortByIF;
import io.ebean.ExpressionList;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.PersistenceContextScope;
import io.ebean.annotation.DbJson;
import io.ebean.annotation.Formula;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import jakarta.persistence.Column;
import jakarta.persistence.Entity;
import jakarta.persistence.EnumType;
import jakarta.persistence.Enumerated;
import jakarta.persistence.Id;
import jakarta.persistence.Transient;
import java.util.Date;
import java.util.HashSet;
import java.util.Set;
import java.util.UUID;
import javax.validation.constraints.NotNull;
import javax.validation.constraints.Size;
import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.NoArgsConstructor;
import lombok.experimental.Accessors;

@Entity
@Data
@Accessors(chain = true)
@EqualsAndHashCode(callSuper = false)
@ApiModel(description = "Maintenance Window")
public class MaintenanceWindow extends Model {

  public enum SortBy implements SortByIF {
    uuid("uuid"),
    name("name"),
    @ApiModelProperty(value = "The create time of maintenance.", example = "2022-12-12T13:07:18Z")
    @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
    createTime("createTime"),
    @ApiModelProperty(value = "The start time of maintenance.", example = "2022-12-12T13:07:18Z")
    @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
    startTime("startTime"),
    endTime("endTime"),
    state("stateIndex");

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

  public enum State {
    FINISHED,
    ACTIVE,
    PENDING
  }

  @Builder
  @NoArgsConstructor
  @AllArgsConstructor
  @Data
  public static class SuppressHealthCheckNotificationsConfig {
    @ApiModelProperty(
        value =
            "Suppress health check notifications on all the universes (including future universes)")
    @Builder.Default
    private boolean suppressAllUniverses = false;

    @ApiModelProperty(value = "Set of universe uuids to suppress health check notifications on")
    @Builder.Default
    private Set<UUID> universeUUIDSet = new HashSet<>();

    public SuppressHealthCheckNotificationsConfig clone() {
      return SuppressHealthCheckNotificationsConfig.builder()
          .suppressAllUniverses(this.suppressAllUniverses)
          .universeUUIDSet(new HashSet<>(this.universeUUIDSet))
          .build();
    }
  }

  @Id
  @Column(nullable = false, unique = true)
  @ApiModelProperty(value = "Maintenance window UUID", accessMode = READ_ONLY)
  private UUID uuid;

  @NotNull
  @Column(nullable = false)
  @ApiModelProperty(value = "Customer UUID", accessMode = READ_ONLY)
  private UUID customerUUID;

  @NotNull
  @Size(min = 1, max = 1000)
  @Column(columnDefinition = "Text", nullable = false)
  @ApiModelProperty(value = "Name", accessMode = READ_WRITE)
  private String name;

  @NotNull
  @Size(min = 1)
  @Column(columnDefinition = "Text")
  @ApiModelProperty(value = "Description", accessMode = READ_WRITE)
  private String description;

  @NotNull
  @Column(nullable = false)
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  @ApiModelProperty(
      value = "Creation time",
      accessMode = READ_ONLY,
      example = "2022-12-12T13:07:18Z")
  private Date createTime;

  @NotNull
  @Column(nullable = false)
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  @ApiModelProperty(value = "Start time", accessMode = READ_WRITE, example = "2022-12-12T13:07:18Z")
  private Date startTime;

  @NotNull
  @Column(nullable = false)
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  @ApiModelProperty(value = "End time", accessMode = READ_WRITE, example = "2022-12-12T13:07:18Z")
  private Date endTime;

  @Formula(
      select =
          "(case"
              + " when end_time < current_timestamp then 'FINISHED'"
              + " when start_time > current_timestamp then 'PENDING'"
              + " else 'ACTIVE' end)")
  @Enumerated(EnumType.STRING)
  @EqualsAndHashCode.Exclude
  @ApiModelProperty(value = "State", accessMode = READ_ONLY)
  private State state;

  @Formula(
      select =
          "(case"
              + " when end_time < current_timestamp then 3"
              + " when start_time > current_timestamp then 1"
              + " else 2 end)")
  @Transient
  @EqualsAndHashCode.Exclude
  @JsonIgnore
  private int stateIndex;

  @NotNull
  @DbJson
  @Column(nullable = false)
  @ApiModelProperty(value = "Alert configuration filter", accessMode = READ_WRITE)
  private AlertConfigurationApiFilter alertConfigurationFilter;

  @Column(nullable = false)
  @JsonIgnore
  private boolean appliedToAlertConfigurations = false;

  @Column(nullable = true)
  @DbJson
  @ApiModelProperty(
      value =
          "WARNING: This is a preview API that could change. Whether to suppress health check"
              + " notifications for universes",
      accessMode = READ_WRITE)
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.23.0.0")
  private SuppressHealthCheckNotificationsConfig suppressHealthCheckNotificationsConfig;

  private static final Finder<UUID, MaintenanceWindow> find =
      new Finder<UUID, MaintenanceWindow>(MaintenanceWindow.class) {};

  public static ExpressionList<MaintenanceWindow> createQueryByFilter(
      MaintenanceWindowFilter filter) {
    ExpressionList<MaintenanceWindow> query =
        find.query().setPersistenceContextScope(PersistenceContextScope.QUERY).where();
    appendInClause(query, "uuid", filter.getUuids());
    appendInClause(query, "state", filter.getStates());
    if (filter.getEndTimeBefore() != null) {
      query.le("endTime", filter.getEndTimeBefore());
    }
    return query;
  }

  public MaintenanceWindow generateUUID() {
    this.uuid = UUID.randomUUID();
    return this;
  }

  @JsonIgnore
  public boolean isNew() {
    return uuid == null;
  }
}
