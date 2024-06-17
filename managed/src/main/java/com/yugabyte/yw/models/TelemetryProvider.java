// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;
import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.yugabyte.yw.models.helpers.telemetry.TelemetryProviderConfig;
import io.ebean.ExpressionList;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.DbJson;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import jakarta.persistence.Entity;
import jakarta.persistence.Id;
import java.util.Date;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import javax.validation.Valid;
import javax.validation.constraints.NotNull;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Data
@EqualsAndHashCode(callSuper = false)
@Entity
@ApiModel(description = "Telemetry Provider Model")
public class TelemetryProvider extends Model {

  @NotNull
  @Id
  @ApiModelProperty(value = "Telemetry Provider UUID", accessMode = READ_ONLY)
  private UUID uuid;

  @NotNull
  @ApiModelProperty(value = "Customer UUID", accessMode = READ_ONLY)
  private UUID customerUUID;

  @NotNull
  @ApiModelProperty(value = "Name", accessMode = READ_WRITE)
  private String name;

  @NotNull
  @Valid
  @DbJson
  @ApiModelProperty(value = "configuration", accessMode = READ_WRITE)
  private TelemetryProviderConfig config;

  @NotNull
  @Valid
  @DbJson
  @ApiModelProperty(value = "Extra Tags", accessMode = READ_WRITE)
  private Map<String, String> tags = new HashMap<>();

  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  @ApiModelProperty(
      value = "Creation timestamp",
      example = "2022-12-12T13:07:18Z",
      accessMode = READ_ONLY)
  private Date createTime;

  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  @ApiModelProperty(
      value = "Updation timestamp",
      example = "2022-12-12T13:07:18Z",
      accessMode = READ_ONLY)
  private Date updateTime;

  public TelemetryProvider generateUUID() {
    this.uuid = UUID.randomUUID();
    return this;
  }

  @JsonIgnore
  public boolean isNew() {
    return uuid == null;
  }

  private static final Finder<UUID, TelemetryProvider> find =
      new Finder<UUID, TelemetryProvider>(TelemetryProvider.class) {};

  public static ExpressionList<TelemetryProvider> createQuery() {
    return find.query().where();
  }

  public static List<TelemetryProvider> list(UUID customerUuid) {
    return TelemetryProvider.createQuery().eq("customerUUID", customerUuid).findList();
  }
}
