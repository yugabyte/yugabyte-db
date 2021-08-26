/*
 * Copyright 2020 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.models;

import static com.yugabyte.yw.models.helpers.CommonUtils.appendInClause;
import static com.yugabyte.yw.models.helpers.CommonUtils.nowWithoutMillis;
import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;
import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.yugabyte.yw.common.AlertTemplate;
import com.yugabyte.yw.models.common.Unit;
import com.yugabyte.yw.models.filters.AlertConfigurationFilter;
import com.yugabyte.yw.models.paging.PagedQuery;
import com.yugabyte.yw.models.paging.PagedQuery.SortByIF;
import io.ebean.ExpressionList;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.DbJson;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Date;
import java.util.Map;
import java.util.Objects;
import java.util.UUID;
import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.EnumType;
import javax.persistence.Enumerated;
import javax.persistence.Id;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.experimental.Accessors;
import play.data.validation.Constraints;

@Entity
@Data
@Accessors(chain = true)
@EqualsAndHashCode(callSuper = false)
@ApiModel(description = "Alert configuration.")
public class AlertConfiguration extends Model {

  public enum SortBy implements PagedQuery.SortByIF {
    uuid("uuid"),
    name("name"),
    active("active"),
    targetType("targetType"),
    createTime("createTime"),
    template("template");

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

  public enum TargetType {
    PLATFORM,
    UNIVERSE
  }

  public enum Severity {
    SEVERE(2),
    WARNING(1);

    private final int priority;

    Severity(int priority) {
      this.priority = priority;
    }

    public int getPriority() {
      return priority;
    }
  }

  @Id
  @Column(nullable = false, unique = true)
  @ApiModelProperty(value = "Configuration uuid", accessMode = READ_ONLY)
  private UUID uuid;

  @Constraints.Required
  @Column(nullable = false)
  @ApiModelProperty(value = "Customer uuid", accessMode = READ_ONLY)
  private UUID customerUUID;

  @Constraints.Required
  @Column(columnDefinition = "Text", nullable = false)
  @ApiModelProperty(value = "Name", accessMode = READ_WRITE)
  private String name;

  @Constraints.Required
  @Column(columnDefinition = "Text")
  @ApiModelProperty(value = "Description", accessMode = READ_WRITE)
  private String description;

  @Constraints.Required
  @Column(nullable = false)
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd HH:mm:ss")
  @ApiModelProperty(value = "Creation time", accessMode = READ_ONLY)
  private Date createTime = nowWithoutMillis();

  @Constraints.Required
  @Enumerated(EnumType.STRING)
  @Column(nullable = false)
  @ApiModelProperty(value = "Target type", accessMode = READ_ONLY)
  private TargetType targetType;

  @Constraints.Required
  @DbJson
  @Column(columnDefinition = "Text", nullable = false)
  @ApiModelProperty(value = "Target", accessMode = READ_WRITE)
  private AlertConfigurationTarget target;

  @Constraints.Required
  @DbJson
  @Column(columnDefinition = "Text", nullable = false)
  @ApiModelProperty(value = "Thresholds", accessMode = READ_WRITE)
  private Map<Severity, AlertConfigurationThreshold> thresholds;

  @Constraints.Required
  @Enumerated(EnumType.STRING)
  @Column(nullable = false)
  @ApiModelProperty(value = "Thresholds unit", accessMode = READ_ONLY)
  private Unit thresholdUnit;

  @Constraints.Required
  @Column(columnDefinition = "Text", nullable = false)
  @Enumerated(EnumType.STRING)
  @ApiModelProperty(value = "Template name", accessMode = READ_ONLY)
  private AlertTemplate template;

  @Column(nullable = false)
  @ApiModelProperty(
      value = "Duration in seconds, while condition is met to raise an alert",
      accessMode = READ_WRITE)
  private Integer durationSec = 15;

  @ApiModelProperty(value = "Is configured alerts raised or not", accessMode = READ_WRITE)
  private boolean active = true;

  @ApiModelProperty(value = "Alert destination uuid", accessMode = READ_WRITE)
  private UUID destinationUUID;

  private static final Finder<UUID, AlertConfiguration> find =
      new Finder<UUID, AlertConfiguration>(AlertConfiguration.class) {};

  public static ExpressionList<AlertConfiguration> createQueryByFilter(
      AlertConfigurationFilter filter) {
    ExpressionList<AlertConfiguration> query = find.query().where();
    appendInClause(query, "uuid", filter.getUuids());
    if (filter.getCustomerUuid() != null) {
      query.eq("customerUUID", filter.getCustomerUuid());
    }
    if (filter.getName() != null) {
      query.eq("name", filter.getName());
    }
    if (filter.getActive() != null) {
      query.eq("active", filter.getActive());
    }
    if (filter.getTargetType() != null) {
      query.eq("targetType", filter.getTargetType());
    }
    if (filter.getTargetUuid() != null) {
      query.like("target", "%%" + filter.getTargetUuid() + "%%");
    }
    if (filter.getTemplate() != null) {
      query.eq("template", filter.getTemplate().name());
    }
    if (filter.getDestinationUuid() != null) {
      query.eq("destinationUUID", filter.getDestinationUuid());
    }
    return query;
  }

  public AlertConfiguration generateUUID() {
    this.uuid = UUID.randomUUID();
    return this;
  }

  @JsonIgnore
  public boolean isNew() {
    return uuid == null;
  }

  public boolean configEquals(AlertConfiguration other) {
    if (Objects.equals(getName(), other.getName())
        && Objects.equals(getDescription(), other.getDescription())
        && Objects.equals(getTemplate(), other.getTemplate())
        && Objects.equals(getDurationSec(), other.getDurationSec())
        && Objects.equals(getThresholds(), other.getThresholds())
        && Objects.equals(getThresholdUnit(), other.getThresholdUnit())
        && Objects.equals(isActive(), other.isActive())) {
      return true;
    }
    return false;
  }
}
