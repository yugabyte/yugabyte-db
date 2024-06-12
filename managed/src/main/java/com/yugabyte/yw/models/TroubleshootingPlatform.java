// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;
import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;

import com.fasterxml.jackson.annotation.JsonIgnore;
import io.ebean.ExpressionList;
import io.ebean.Finder;
import io.ebean.Model;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import jakarta.persistence.Entity;
import jakarta.persistence.Id;
import java.util.*;
import javax.validation.constraints.NotNull;
import lombok.Data;
import lombok.EqualsAndHashCode;
import org.hibernate.validator.constraints.URL;

@Data
@EqualsAndHashCode(callSuper = false)
@Entity
@ApiModel(description = "Troubleshooting Platform Model")
public class TroubleshootingPlatform extends Model {

  @NotNull
  @Id
  @ApiModelProperty(value = "Troubleshooting Platform UUID", accessMode = READ_ONLY)
  private UUID uuid;

  @NotNull
  @ApiModelProperty(value = "Customer UUID", accessMode = READ_ONLY)
  private UUID customerUUID;

  @NotNull
  @ApiModelProperty(value = "Troubleshooting Platform URL", accessMode = READ_WRITE)
  @URL
  private String tpUrl;

  @NotNull
  @ApiModelProperty(value = "YBA URL", accessMode = READ_WRITE)
  @URL
  private String ybaUrl;

  @NotNull
  @ApiModelProperty(value = "Metrics URL", accessMode = READ_WRITE)
  @URL
  private String metricsUrl;

  public TroubleshootingPlatform generateUUID() {
    this.uuid = UUID.randomUUID();
    return this;
  }

  @JsonIgnore
  public boolean isNew() {
    return uuid == null;
  }

  private static final Finder<UUID, TroubleshootingPlatform> find =
      new Finder<UUID, TroubleshootingPlatform>(TroubleshootingPlatform.class) {};

  public static ExpressionList<TroubleshootingPlatform> createQuery() {
    return find.query().where();
  }
}
