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
import io.ebean.FetchGroup;
import io.ebean.Finder;
import io.ebean.Junction;
import io.ebean.Model;
import io.ebean.PersistenceContextScope;
import io.ebean.Query;
import io.ebean.annotation.DbJson;
import io.ebean.annotation.Formula;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Collections;
import java.util.Date;
import java.util.HashMap;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.TreeSet;
import java.util.UUID;
import javax.persistence.Entity;
import javax.persistence.EnumType;
import javax.persistence.Enumerated;
import javax.persistence.Id;
import javax.persistence.Transient;
import javax.validation.Valid;
import javax.validation.constraints.Min;
import javax.validation.constraints.NotNull;
import javax.validation.constraints.Size;
import lombok.Builder;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.Value;
import lombok.experimental.Accessors;
import org.apache.commons.collections4.CollectionUtils;

@Entity
@Data
@Accessors(chain = true)
@EqualsAndHashCode(callSuper = false)
@ApiModel(description = "Alert configuration")
public class AlertConfiguration extends Model {

  private static final String RAW_FIELDS =
      "uuid, customerUUID, name, description, createTime, "
          + "targetType, target, thresholds, thresholdUnit, template, durationSec, active, "
          + "destinationUUID, defaultDestination, maintenanceWindowUuids";

  @ApiModel
  public enum SortBy implements PagedQuery.SortByIF {
    uuid("uuid"),
    name("name"),
    active("active"),
    targetType("targetType"),
    target("targetName"),
    createTime("createTime"),
    template("template"),
    severity("severityIndex"),
    destination("destinationName"),
    alertCount("alertCount");

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

  @ApiModel
  public enum TargetType {
    PLATFORM,
    UNIVERSE
  }

  @ApiModel
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
  @ApiModelProperty(value = "Configuration UUID", accessMode = READ_ONLY)
  private UUID uuid;

  @NotNull
  @ApiModelProperty(value = "Customer UUID", accessMode = READ_ONLY)
  private UUID customerUUID;

  @NotNull
  @Size(min = 1, max = 1000)
  @ApiModelProperty(value = "Name", accessMode = READ_WRITE)
  private String name;

  @NotNull
  @ApiModelProperty(value = "Description", accessMode = READ_WRITE)
  private String description;

  @NotNull
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  @ApiModelProperty(
      value = "Creation time",
      accessMode = READ_ONLY,
      example = "2022-12-12T13:07:18Z")
  private Date createTime;

  @NotNull
  @Enumerated(EnumType.STRING)
  @ApiModelProperty(value = "Target type", accessMode = READ_WRITE)
  private TargetType targetType;

  @NotNull
  @DbJson
  @ApiModelProperty(value = "Target", accessMode = READ_WRITE)
  private AlertConfigurationTarget target;

  @NotNull
  @Valid
  @Size(min = 1)
  @DbJson
  @ApiModelProperty(value = "Thresholds", accessMode = READ_WRITE)
  @EqualsAndHashCode.Exclude
  private Map<Severity, AlertConfigurationThreshold> thresholds;

  @NotNull
  @Enumerated(EnumType.STRING)
  @ApiModelProperty(value = "Threshold unit", accessMode = READ_WRITE)
  private Unit thresholdUnit;

  @NotNull
  @Enumerated(EnumType.STRING)
  @ApiModelProperty(value = "Template name", accessMode = READ_WRITE)
  private AlertTemplate template;

  @NotNull
  @Min(0)
  @ApiModelProperty(
      value = "Duration in seconds, while condition is met to raise an alert",
      accessMode = READ_WRITE)
  private Integer durationSec = 0;

  @NotNull
  @ApiModelProperty(value = "Is configured alerts raised or not", accessMode = READ_WRITE)
  private boolean active = true;

  @ApiModelProperty(value = "Alert destination UUID", accessMode = READ_WRITE)
  private UUID destinationUUID;

  @NotNull
  @ApiModelProperty(value = "Is default destination used for this config", accessMode = READ_WRITE)
  private boolean defaultDestination;

  @DbJson
  @ApiModelProperty(
      value = "Maintenance window UUIDs, applied to this alert config",
      accessMode = READ_ONLY)
  private Set<UUID> maintenanceWindowUuids;

  @DbJson
  @ApiModelProperty(value = "Labels", accessMode = READ_WRITE)
  @EqualsAndHashCode.Exclude
  private Map<String, String> labels;

  private static final String ALERT_COUNT_JOIN =
      "left join "
          + "(select _ac.uuid, count(*) as alert_count "
          + "from alert_configuration _ac "
          + "left join alert _a on _ac.uuid = _a.configuration_uuid "
          + "where _a.state in ('ACTIVE', 'ACKNOWLEDGED') group by _ac.uuid"
          + ") as _ac "
          + "on _ac.uuid = ${ta}.uuid";

  @Transient
  @Formula(select = "alert_count", join = ALERT_COUNT_JOIN)
  @EqualsAndHashCode.Exclude
  Double alertCount;

  private static final String TARGET_INDEX_JOIN =
      "left join "
          + "(select uuid, universe_name "
          + "from ("
          + "select uuid, "
          + "universe_name, "
          + "rank() OVER (PARTITION BY uuid ORDER BY universe_name asc) as rank "
          + "from ("
          + "select uuid, universe_names.name as universe_name "
          + "from ("
          + "(select uuid, replace(universe_uuid::text, '\"', '')::uuid as universe_uuid "
          + "from ("
          + "select uuid, json_array_elements(target::json->'uuids') as universe_uuid "
          + "from alert_configuration "
          + "where nullif(target::json->>'uuids', '') is not null"
          + ") as tmp"
          + ") as targets "
          + "left join universe on targets.universe_uuid = universe.universe_uuid"
          + ") as universe_names"
          + ") as ranked_universe_names"
          + ") as sorted_universe_names"
          + ") as _un "
          + "on _un.uuid = ${ta}.uuid";

  @Transient
  @Formula(
      select =
          "(case"
              + " when target like '%\"all\":true%' then 'ALL'"
              + " else _un.universe_name end)",
      join = TARGET_INDEX_JOIN)
  @EqualsAndHashCode.Exclude
  @JsonIgnore
  private String targetName;

  @Transient
  @Formula(
      select =
          "(case"
              + " when thresholds like '%SEVERE%' then 2"
              + " when thresholds like '%WARNING%' then 1"
              + " else 0 end)")
  @EqualsAndHashCode.Exclude
  @JsonIgnore
  private Integer severityIndex;

  @Transient
  @Formula(
      select =
          "(case"
              + " when ${ta}.default_destination = true then 'Use default'"
              + " when _ad.name is not null then _ad.name"
              + " else 'No destination' end)",
      join = "left join alert_destination as _ad on _ad.uuid = ${ta}.destination_uuid")
  @EqualsAndHashCode.Exclude
  @JsonIgnore
  private String destinationName;

  private static final Finder<UUID, AlertConfiguration> find =
      new Finder<UUID, AlertConfiguration>(AlertConfiguration.class) {};

  public static ExpressionList<AlertConfiguration> createQueryByFilter(
      AlertConfigurationFilter filter) {
    return createQueryByFilter(filter, QuerySettings.builder().build());
  }

  public static ExpressionList<AlertConfiguration> createQueryByFilter(
      AlertConfigurationFilter filter, QuerySettings querySettings) {
    Query<AlertConfiguration> query = find.query();

    String fetchFields = RAW_FIELDS;
    if (querySettings.isQueryTargetIndex()) {
      fetchFields += ", targetName";
    }
    if (querySettings.isQueryDestinationIndex()) {
      fetchFields += ", destinationName";
    }
    if (querySettings.isQueryCount()) {
      fetchFields += ", alertCount";
    }
    FetchGroup<AlertConfiguration> fetchGroup =
        FetchGroup.of(AlertConfiguration.class).select(fetchFields).build();
    query.select(fetchGroup);

    ExpressionList<AlertConfiguration> expression =
        query.setPersistenceContextScope(PersistenceContextScope.QUERY).where();
    appendInClause(expression, "uuid", filter.getUuids());
    if (filter.getCustomerUuid() != null) {
      expression.eq("customerUUID", filter.getCustomerUuid());
    }
    if (filter.getName() != null) {
      expression.ilike("name", "%" + filter.getName() + "%");
    }
    if (filter.getActive() != null) {
      expression.eq("active", filter.getActive());
    }
    if (filter.getTargetType() != null) {
      expression.eq("targetType", filter.getTargetType());
    }
    if (filter.getTarget() != null) {
      AlertConfigurationTarget filterTarget = filter.getTarget();
      Junction<AlertConfiguration> orExpr = expression.or();
      if (CollectionUtils.isNotEmpty(filterTarget.getUuids())) {
        // All target always match particular UUID target
        orExpr.like("target", "%\"all\":true%");
        for (UUID target : filterTarget.getUuids()) {
          orExpr.like("target", "%\"uuids\":%\"" + target + "\"%");
        }
      } else {
        if (filterTarget.isAll()) {
          orExpr.like("target", "%\"all\":true%");
        } else {
          orExpr.not().like("target", "%\"all\":true%");
        }
      }
      expression.endOr();
    }

    appendInClause(expression, "template", filter.getTemplatesStr());
    if (filter.getDestinationType() != null) {
      switch (filter.getDestinationType()) {
        case NO_DESTINATION:
          expression.eq("defaultDestination", false).isNull("destinationUUID");
          break;
        case DEFAULT_DESTINATION:
          expression.eq("defaultDestination", true);
          break;
        case SELECTED_DESTINATION:
          expression.isNotNull("destinationUUID");
          break;
      }
    }
    if (filter.getDestinationUuid() != null) {
      expression.eq("destinationUUID", filter.getDestinationUuid());
    }
    if (filter.getSeverity() != null) {
      expression.like("thresholds", "%\"" + filter.getSeverity().name() + "\"%");
    }
    if (filter.getSuspended() != null) {
      if (filter.getSuspended()) {
        expression.isNotNull("maintenanceWindowUuids");
      } else {
        expression.isNull("maintenanceWindowUuids");
      }
    }
    return expression;
  }

  public AlertConfiguration generateUUID() {
    this.uuid = UUID.randomUUID();
    return this;
  }

  @JsonIgnore
  public boolean isNew() {
    return uuid == null;
  }

  @JsonIgnore
  public Set<UUID> getMaintenanceWindowUuidsSet() {
    if (maintenanceWindowUuids == null) {
      return Collections.emptySet();
    }
    return new TreeSet<>(maintenanceWindowUuids);
  }

  public AlertConfiguration addMaintenanceWindowUuid(UUID maintenanceWindowUuid) {
    if (this.maintenanceWindowUuids == null) {
      maintenanceWindowUuids = new TreeSet<>();
    }
    maintenanceWindowUuids.add(maintenanceWindowUuid);
    return this;
  }

  public AlertConfiguration removeMaintenanceWindowUuid(UUID maintenanceWindowUuid) {
    if (this.maintenanceWindowUuids == null) {
      return this;
    }
    maintenanceWindowUuids.remove(maintenanceWindowUuid);
    if (maintenanceWindowUuids.isEmpty()) {
      // To make it easier to query for empty list in DB
      this.maintenanceWindowUuids = null;
    }
    return this;
  }

  @JsonIgnore
  @EqualsAndHashCode.Include
  /*
   * This is required, because Ebean creates ModifyAwareMap instead of regular
   * HashMap while
   * reads object from DB. And equals with regular HashMap in just created config
   * fails.
   */
  public Map<Severity, AlertConfigurationThreshold> thresholdsHashMap() {
    if (thresholds == null) {
      return null;
    }
    return new HashMap<>(thresholds);
  }

  public Map<String, String> labelsHashMap() {
    if (labels == null) {
      return null;
    }
    return new HashMap<>(labels);
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

  @Value
  @Builder
  public static class QuerySettings {
    boolean queryCount;
    boolean queryTargetIndex;
    boolean queryDestinationIndex;
  }
}
