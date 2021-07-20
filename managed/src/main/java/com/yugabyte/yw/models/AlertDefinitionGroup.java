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

import com.fasterxml.jackson.annotation.JsonFormat;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.yugabyte.yw.common.AlertDefinitionTemplate;
import com.yugabyte.yw.models.common.Unit;
import com.yugabyte.yw.models.filters.AlertDefinitionGroupFilter;
import com.yugabyte.yw.models.paging.PagedQuery;
import io.ebean.ExpressionList;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.DbJson;
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
public class AlertDefinitionGroup extends Model {

  public enum SortBy implements PagedQuery.SortByIF {
    NAME("name"),
    ACTIVE("active"),
    TARGET_TYPE("targetType"),
    CREATE_TIME("createTime"),
    ALERT_COUNT("alertCount");

    private final String sortField;

    SortBy(String sortField) {
      this.sortField = sortField;
    }

    public String getSortField() {
      return sortField;
    }
  }

  public enum TargetType {
    CUSTOMER,
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
  private UUID uuid;

  @Constraints.Required
  @Column(nullable = false)
  private UUID customerUUID;

  @Constraints.Required
  @Column(columnDefinition = "Text", nullable = false)
  private String name;

  @Constraints.Required
  @Column(columnDefinition = "Text")
  private String description;

  @Constraints.Required
  @Column(nullable = false)
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd HH:mm:ss")
  private Date createTime = nowWithoutMillis();

  @Constraints.Required
  @Enumerated(EnumType.STRING)
  @Column(nullable = false)
  private TargetType targetType;

  @Constraints.Required
  @DbJson
  @Column(columnDefinition = "Text", nullable = false)
  private AlertDefinitionGroupTarget target;

  @Constraints.Required
  @DbJson
  @Column(columnDefinition = "Text", nullable = false)
  private Map<Severity, AlertDefinitionGroupThreshold> thresholds;

  @Constraints.Required
  @Enumerated(EnumType.STRING)
  @Column(nullable = false)
  private Unit thresholdUnit;

  @Constraints.Required
  @Column(columnDefinition = "Text", nullable = false)
  @Enumerated(EnumType.STRING)
  private AlertDefinitionTemplate template;

  @Column(nullable = false)
  private int durationSec = 15;

  private boolean active = true;

  private UUID routeUUID;

  private static final Finder<UUID, AlertDefinitionGroup> find =
      new Finder<UUID, AlertDefinitionGroup>(AlertDefinitionGroup.class) {};

  public static ExpressionList<AlertDefinitionGroup> createQueryByFilter(
      AlertDefinitionGroupFilter filter) {
    ExpressionList<AlertDefinitionGroup> query = find.query().where();
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
    if (filter.getRouteUuid() != null) {
      query.eq("routeUUID", filter.getRouteUuid());
    }
    return query;
  }

  public AlertDefinitionGroup generateUUID() {
    this.uuid = UUID.randomUUID();
    return this;
  }

  @JsonIgnore
  public boolean isNew() {
    return uuid == null;
  }

  public boolean configEquals(AlertDefinitionGroup other) {
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
