// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.google.common.annotations.VisibleForTesting;
import com.yugabyte.yw.common.alerts.AlertLabelsProvider;
import com.yugabyte.yw.models.filters.AlertFilter;
import com.yugabyte.yw.models.helpers.KnownAlertCodes;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import com.yugabyte.yw.models.paging.PagedQuery;
import io.ebean.ExpressionList;
import io.ebean.Finder;
import io.ebean.Model;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.experimental.Accessors;

import javax.persistence.*;
import java.util.Comparator;
import java.util.Date;
import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;

import static com.yugabyte.yw.models.helpers.CommonUtils.*;
import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;
import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;

@Entity
@Data
@Accessors(chain = true)
@EqualsAndHashCode(callSuper = false)
@ApiModel(description = "Alert information. which is used to send alert notification.")
public class Alert extends Model implements AlertLabelsProvider {

  public enum State {
    CREATED("firing"),
    ACTIVE("firing"),
    ACKNOWLEDGED("acknowledged"),
    RESOLVED("resolved");

    private final String action;

    State(String action) {
      this.action = action;
    }

    public String getAction() {
      return action;
    }
  }

  public enum SortBy implements PagedQuery.SortByIF {
    CREATE_TIME("createTime"),
    SEVERITY("severity");

    private final String sortField;

    SortBy(String sortField) {
      this.sortField = sortField;
    }

    public String getSortField() {
      return sortField;
    }
  }

  @Id
  @Column(nullable = false, unique = true)
  @ApiModelProperty(value = "Alert uuid", accessMode = READ_ONLY)
  private UUID uuid;

  @Column(nullable = false)
  @ApiModelProperty(value = "Cutomer uuid", accessMode = READ_ONLY)
  private UUID customerUUID;

  @Column(nullable = false)
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd HH:mm:ss")
  @ApiModelProperty(value = "Create Date time info.", accessMode = READ_ONLY)
  private Date createTime = nowWithoutMillis();

  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd HH:mm:ss")
  @ApiModelProperty(value = "Acknowledge Date time info.", accessMode = READ_ONLY)
  private Date acknowledgedTime;

  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd HH:mm:ss")
  @ApiModelProperty(value = "Resolved Date time info.", accessMode = READ_ONLY)
  private Date resolvedTime;

  @Column(columnDefinition = "Text", nullable = false)
  @ApiModelProperty(value = "Error Code.", accessMode = READ_ONLY)
  private String errCode;

  @Enumerated(EnumType.STRING)
  @ApiModelProperty(value = "Alert definition group serverity.", accessMode = READ_ONLY)
  private AlertDefinitionGroup.Severity severity;

  @Column(columnDefinition = "Text", nullable = false)
  @ApiModelProperty(value = "Alert Message.", accessMode = READ_ONLY)
  private String message;

  @Enumerated(EnumType.STRING)
  @ApiModelProperty(value = "Alert State.", accessMode = READ_ONLY)
  private State state = State.CREATED;

  @Enumerated(EnumType.STRING)
  @ApiModelProperty(value = "Target State.", accessMode = READ_ONLY)
  private State targetState = State.ACTIVE;

  @ApiModelProperty(value = "Whether to send an Email or not.", accessMode = READ_ONLY)
  private boolean sendEmail;

  @ApiModelProperty(value = "Alert Definition Uuid", accessMode = READ_ONLY)
  private UUID definitionUuid;

  @ApiModelProperty(value = "Alert group Uuid", accessMode = READ_ONLY)
  private UUID groupUuid;

  @ApiModelProperty(value = "Alert definition group type", accessMode = READ_ONLY)
  private AlertDefinitionGroup.TargetType groupType;

  @OneToMany(mappedBy = "alert", cascade = CascadeType.ALL, orphanRemoval = true)
  private List<AlertLabel> labels;

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

  public Alert setErrCode(String errCode) {
    this.errCode = errCode;
    return this;
  }

  public Alert setErrCode(KnownAlertCodes errCode) {
    this.errCode = errCode.name();
    return this;
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
    ExpressionList<Alert> query = find.query().fetch("labels").where();
    appendInClause(query, "uuid", filter.getUuids());
    appendNotInClause(query, "uuid", filter.getExcludeUuids());
    if (filter.getCustomerUuid() != null) {
      query.eq("customerUUID", filter.getCustomerUuid());
    }
    appendInClause(query, "state", filter.getStates());
    appendInClause(query, "targetState", filter.getTargetStates());
    if (filter.getErrorCode() != null) {
      query.eq("errCode", filter.getErrorCode());
    }
    appendInClause(query, "definitionUuid", filter.getDefinitionUuids());
    if (filter.getLabel() != null) {
      query
          .eq("labels.key.name", filter.getLabel().getName())
          .eq("labels.value", filter.getLabel().getValue());
    }
    if (filter.getGroupUuid() != null) {
      query.eq("groupUuid", filter.getGroupUuid());
    }
    if (filter.getSeverity() != null) {
      query.eq("severity", filter.getSeverity());
    }
    if (filter.getGroupType() != null) {
      query.eq("groupType", filter.getGroupType());
    }
    return query;
  }
}
