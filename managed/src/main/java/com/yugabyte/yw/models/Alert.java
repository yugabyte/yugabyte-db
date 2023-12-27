// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static com.yugabyte.yw.models.helpers.CommonUtils.appendInClause;
import static com.yugabyte.yw.models.helpers.CommonUtils.appendNotInClause;
import static com.yugabyte.yw.models.helpers.CommonUtils.nowWithoutMillis;
import static com.yugabyte.yw.models.helpers.CommonUtils.setUniqueListValue;
import static com.yugabyte.yw.models.helpers.CommonUtils.setUniqueListValues;
import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.google.common.annotations.VisibleForTesting;
import com.yugabyte.yw.common.alerts.AlertLabelsProvider;
import com.yugabyte.yw.models.filters.AlertFilter;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import com.yugabyte.yw.models.paging.PagedQuery;
import com.yugabyte.yw.models.paging.PagedQuery.SortByIF;
import io.ebean.ExpressionList;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.PersistenceContextScope;
import io.ebean.annotation.Formula;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Arrays;
import java.util.Comparator;
import java.util.Date;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.persistence.CascadeType;
import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.EnumType;
import javax.persistence.Enumerated;
import javax.persistence.Id;
import javax.persistence.OneToMany;
import javax.persistence.Transient;
import javax.validation.constraints.NotNull;
import javax.validation.constraints.Size;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.experimental.Accessors;
import org.apache.commons.lang3.StringUtils;

@Entity
@Data
@Accessors(chain = true)
@EqualsAndHashCode(callSuper = false)
@ApiModel(description = "Alert definition. Used to send an alert notification.")
public class Alert extends Model implements AlertLabelsProvider {

  public enum State {
    ACTIVE("firing", true),
    ACKNOWLEDGED("acknowledged", true),
    SUSPENDED("suspended", true),
    RESOLVED("resolved", false);

    private final String action;
    private final boolean firing;

    State(String action, boolean firing) {
      this.action = action;
      this.firing = firing;
    }

    public String getAction() {
      return action;
    }

    public boolean isFiring() {
      return firing;
    }

    public static Set<State> getFiringStates() {
      return Arrays.stream(values()).filter(State::isFiring).collect(Collectors.toSet());
    }
  }

  public enum SortBy implements PagedQuery.SortByIF {
    uuid("uuid"),
    createTime("createTime"),
    severity("severityIndex"),
    name("name"),
    sourceName("sourceName"),
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

  @Id
  @Column(nullable = false, unique = true)
  @ApiModelProperty(value = "Alert UUID", accessMode = READ_ONLY)
  private UUID uuid;

  @NotNull
  @Column(nullable = false)
  @ApiModelProperty(value = "Customer UUID", accessMode = READ_ONLY)
  private UUID customerUUID;

  @NotNull
  @Column(nullable = false)
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ssZ")
  @ApiModelProperty(value = "Alert creation timestamp", accessMode = READ_ONLY)
  private Date createTime = nowWithoutMillis();

  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ssZ")
  @ApiModelProperty(value = "Timestamp at which the alert was acknowledged", accessMode = READ_ONLY)
  private Date acknowledgedTime;

  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ssZ")
  @ApiModelProperty(value = "Timestamp at which the alert was resolved", accessMode = READ_ONLY)
  private Date resolvedTime;

  @NotNull
  @Enumerated(EnumType.STRING)
  @ApiModelProperty(value = "Alert configuration severity", accessMode = READ_ONLY)
  private AlertConfiguration.Severity severity;

  @Transient
  @Formula(
      select =
          "(case"
              + " when severity = 'WARNING' then 1"
              + " when severity = 'SEVERE' then 2"
              + " else 0 end)")
  private Integer severityIndex;

  @NotNull
  @Size(min = 1, max = 1000)
  @ApiModelProperty(value = "The alert's name", accessMode = READ_ONLY)
  private String name;

  @NotNull
  @Size(min = 1)
  @Column(columnDefinition = "Text", nullable = false)
  @ApiModelProperty(value = "The alert's message text", accessMode = READ_ONLY)
  private String message;

  @NotNull
  @ApiModelProperty(value = "The source of the alert", accessMode = READ_ONLY)
  private String sourceName;

  @NotNull
  @ApiModelProperty(value = "The sourceUUID of the alert", accessMode = READ_ONLY)
  private UUID sourceUUID;

  @NotNull
  @Enumerated(EnumType.STRING)
  @ApiModelProperty(value = "The alert's state", accessMode = READ_ONLY)
  private State state = State.ACTIVE;

  @Transient
  @Formula(
      select =
          "(case"
              + " when state = 'ACTIVE' then 1"
              + " when state = 'ACKNOWLEDGED' then 2"
              + " when state = 'RESOLVED' then 3"
              + " else 0 end)")
  private Integer stateIndex;

  @NotNull
  @ApiModelProperty(value = "Alert definition UUID", accessMode = READ_ONLY)
  private UUID definitionUuid;

  @NotNull
  @ApiModelProperty(value = "Alert configuration UUID", accessMode = READ_ONLY)
  private UUID configurationUuid;

  @NotNull
  @Enumerated(EnumType.STRING)
  @ApiModelProperty(value = "Alert configuration type", accessMode = READ_ONLY)
  private AlertConfiguration.TargetType configurationType;

  @OneToMany(mappedBy = "alert", cascade = CascadeType.ALL, orphanRemoval = true)
  private List<AlertLabel> labels;

  @ApiModelProperty(value = "Time of the last notification attempt", accessMode = READ_ONLY)
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ssZ")
  private Date notificationAttemptTime;

  @ApiModelProperty(value = "Time of the next notification attempt", accessMode = READ_ONLY)
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ssZ")
  private Date nextNotificationTime = nowWithoutMillis();

  @ApiModelProperty(value = "Count of failures to send a notification", accessMode = READ_ONLY)
  @Column(nullable = false)
  private int notificationsFailed = 0;

  @Enumerated(EnumType.STRING)
  @ApiModelProperty(value = "Alert state in the last-sent notification", accessMode = READ_ONLY)
  private State notifiedState;

  private static final Finder<UUID, Alert> find = new Finder<UUID, Alert>(Alert.class) {};

  @VisibleForTesting
  public Alert setUuid(UUID uuid) {
    this.uuid = uuid;
    this.labels.forEach(label -> label.setAlert(this));
    return this;
  }

  public Alert generateUUID() {
    return setUuid(UUID.randomUUID());
  }

  @JsonIgnore
  public boolean isNew() {
    return uuid == null;
  }

  public String getLabelValue(KnownAlertLabels knownLabel) {
    return getLabelValue(knownLabel.labelName());
  }

  public String getLabelValue(String name) {
    // TODO Remove once notifications sent through AlertManager
    if (KnownAlertLabels.ALERT_STATE.labelName().equals(name)) {
      return state.getAction();
    }
    return labels
        .stream()
        .filter(label -> name.equals(label.getName()))
        .map(AlertLabel::getValue)
        .findFirst()
        .orElse(null);
  }

  public Alert setLabel(KnownAlertLabels label, String value) {
    return setLabel(label.labelName(), value);
  }

  public Alert setLabel(String name, String value) {
    AlertLabel toAdd = new AlertLabel(this, name, value);
    this.labels = setUniqueListValue(labels, toAdd);
    return this;
  }

  public Alert setLabels(List<AlertLabel> labels) {
    this.labels = setUniqueListValues(this.labels, labels);
    this.labels.forEach(label -> label.setAlert(this));
    return this;
  }

  public List<AlertLabel> getLabels() {
    return labels
        .stream()
        .sorted(Comparator.comparing(AlertLabel::getName))
        .collect(Collectors.toList());
  }

  public static ExpressionList<Alert> createQueryByFilter(AlertFilter filter) {
    ExpressionList<Alert> query =
        find.query()
            .setPersistenceContextScope(PersistenceContextScope.QUERY)
            .fetch("labels")
            .where();
    appendInClause(query, "uuid", filter.getUuids());
    appendNotInClause(query, "uuid", filter.getExcludeUuids());
    if (filter.getCustomerUuid() != null) {
      query.eq("customerUUID", filter.getCustomerUuid());
    }
    appendInClause(query, "state", filter.getStates());
    appendInClause(query, "definitionUuid", filter.getDefinitionUuids());
    if (filter.getLabel() != null) {
      query
          .eq("labels.key.name", filter.getLabel().getName())
          .eq("labels.value", filter.getLabel().getValue());
    }
    if (filter.getConfigurationUuid() != null) {
      query.eq("configurationUuid", filter.getConfigurationUuid());
    }
    if (!StringUtils.isEmpty(filter.getSourceName())) {
      query.like("sourceName", filter.getSourceName() + "%");
    }
    appendInClause(query, "sourceUUID", filter.getSourceUUIDs());
    appendInClause(query, "severity", filter.getSeverities());
    appendInClause(query, "configurationType", filter.getConfigurationTypes());

    if (filter.getNotificationPending() != null) {
      if (filter.getNotificationPending()) {
        query.isNotNull("nextNotificationTime").le("nextNotificationTime", new Date());
      } else {
        query.or().isNull("nextNotificationTime").gt("nextNotificationTime", new Date()).endOr();
      }
    }
    if (filter.getResolvedDateBefore() != null) {
      query.le("resolvedTime", filter.getResolvedDateBefore());
    }
    return query;
  }
}
