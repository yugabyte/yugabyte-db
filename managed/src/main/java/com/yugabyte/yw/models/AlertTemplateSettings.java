/*
 * Copyright 2022 YugaByte, Inc. and Contributors
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
import com.yugabyte.yw.models.filters.AlertTemplateSettingsFilter;
import com.yugabyte.yw.models.paging.PagedQuery.SortByIF;
import io.ebean.ExpressionList;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.PersistenceContextScope;
import io.ebean.Query;
import io.ebean.annotation.DbJson;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import jakarta.persistence.Column;
import jakarta.persistence.Entity;
import jakarta.persistence.Id;
import java.util.Date;
import java.util.Map;
import java.util.UUID;
import javax.validation.Valid;
import javax.validation.constraints.NotNull;
import javax.validation.constraints.Size;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.experimental.Accessors;

@Entity
@Data
@Accessors(chain = true)
@EqualsAndHashCode(callSuper = false)
@ApiModel(description = "Alert template settings")
public class AlertTemplateSettings extends Model {

  public enum SortBy implements SortByIF {
    uuid("uuid"),
    name("name");

    private final String sortField;

    SortBy(String sortField) {
      this.sortField = sortField;
    }

    public String getSortField() {
      return sortField;
    }

    @Override
    public SortByIF getOrderField() {
      return SortBy.name;
    }
  }

  @Id private UUID uuid;

  @NotNull
  @Size(min = 1, max = 50)
  @ApiModelProperty(value = "Template", accessMode = READ_WRITE)
  private String template;

  @NotNull
  @Column(nullable = false)
  @ApiModelProperty(value = "Customer UUID", accessMode = READ_ONLY)
  private UUID customerUUID;

  @NotNull
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  @ApiModelProperty(
      value = "Creation time",
      accessMode = READ_ONLY,
      example = "2022-12-12T13:07:18Z")
  private Date createTime;

  @DbJson
  @ApiModelProperty(value = "Labels", accessMode = READ_WRITE)
  @Valid
  private Map<String, String> labels;

  private static final Finder<UUID, AlertTemplateSettings> find =
      new Finder<UUID, AlertTemplateSettings>(AlertTemplateSettings.class) {};

  public AlertTemplateSettings generateUUID() {
    this.uuid = UUID.randomUUID();
    return this;
  }

  @JsonIgnore
  public boolean isNew() {
    return uuid == null;
  }

  public static ExpressionList<AlertTemplateSettings> createQueryByFilter(
      AlertTemplateSettingsFilter filter) {
    Query<AlertTemplateSettings> query = find.query();

    ExpressionList<AlertTemplateSettings> expression =
        query.setPersistenceContextScope(PersistenceContextScope.QUERY).where();
    appendInClause(expression, "uuid", filter.getUuids());
    if (filter.getCustomerUuid() != null) {
      expression.eq("customerUUID", filter.getCustomerUuid());
    }
    appendInClause(expression, "template", filter.getTemplates());
    return expression;
  }
}
