// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;
import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;

import com.fasterxml.jackson.annotation.JsonIgnore;
import io.ebean.ExpressionList;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.DbJson;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import javax.persistence.Entity;
import javax.persistence.Id;
import javax.validation.constraints.NotNull;
import javax.validation.constraints.Size;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.experimental.Accessors;

@Data
@Accessors(chain = true)
@EqualsAndHashCode(callSuper = false)
@Entity
@ApiModel(description = "Alert template variable")
public class AlertTemplateVariable extends Model {

  @Id
  @ApiModelProperty(value = "Variable UUID", accessMode = READ_ONLY)
  private UUID uuid;

  @NotNull
  @Size(min = 1, max = 100)
  @ApiModelProperty(value = "Name", accessMode = READ_WRITE)
  private String name;

  @NotNull
  @ApiModelProperty(value = "Customer UUID", accessMode = READ_ONLY)
  private UUID customerUUID;

  @NotNull
  @Size(min = 1)
  @DbJson
  @ApiModelProperty(value = "Possible values", accessMode = READ_WRITE)
  @EqualsAndHashCode.Exclude
  private Set<String> possibleValues;

  @NotNull
  @Size(min = 1)
  @ApiModelProperty(value = "Default value", accessMode = READ_WRITE)
  private String defaultValue;

  private static final Finder<UUID, AlertTemplateVariable> find =
      new Finder<UUID, AlertTemplateVariable>(AlertTemplateVariable.class) {};

  public AlertTemplateVariable generateUUID() {
    this.uuid = UUID.randomUUID();
    return this;
  }

  @JsonIgnore
  public boolean isNew() {
    return uuid == null;
  }

  @JsonIgnore
  @EqualsAndHashCode.Include
  /*
    This is required, because Ebean creates ModifyAwareSet instead of regular HashSet while
    reads object from DB. And equals with regular HashMap in just created config fails.
  */
  public Set<String> possibleValuesSet() {
    if (possibleValues == null) {
      return null;
    }
    return new HashSet<>(possibleValues);
  }

  public static ExpressionList<AlertTemplateVariable> createQuery() {
    return find.query().where();
  }

  public static List<AlertTemplateVariable> list(UUID customerUuid) {
    return AlertTemplateVariable.createQuery().eq("customerUUID", customerUuid).findList();
  }
}
