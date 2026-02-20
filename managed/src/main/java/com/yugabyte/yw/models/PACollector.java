// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;
import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;

import com.fasterxml.jackson.annotation.JsonIgnore;
import io.ebean.ExpressionList;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.Encrypted;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import jakarta.persistence.Entity;
import jakarta.persistence.Id;
import jakarta.persistence.Table;
import java.util.*;
import javax.validation.constraints.NotNull;
import lombok.Data;
import lombok.EqualsAndHashCode;
import org.hibernate.validator.constraints.URL;

@Data
@EqualsAndHashCode(callSuper = false)
@Entity
@Table(name = "pa_collector")
@ApiModel(description = "Performance Advisor Collector Model")
public class PACollector extends Model {

  @NotNull
  @Id
  @ApiModelProperty(value = "PA Collector UUID", accessMode = READ_ONLY)
  private UUID uuid;

  @NotNull
  @ApiModelProperty(value = "Customer UUID", accessMode = READ_ONLY)
  private UUID customerUUID;

  @NotNull
  @ApiModelProperty(value = "PA Collector URL", accessMode = READ_WRITE)
  @URL
  private String paUrl;

  @NotNull
  @ApiModelProperty(value = "YBA URL", accessMode = READ_WRITE)
  @URL
  private String ybaUrl;

  @NotNull
  @ApiModelProperty(value = "Metrics URL", accessMode = READ_WRITE)
  @URL
  private String metricsUrl;

  @NotNull
  @ApiModelProperty(value = "Metrics API Username", accessMode = READ_WRITE)
  @Encrypted
  private String metricsUsername;

  @NotNull
  @ApiModelProperty(value = "Metrics API Password", accessMode = READ_WRITE)
  @Encrypted
  private String metricsPassword;

  @NotNull
  @ApiModelProperty(value = "YBA API Token", accessMode = READ_WRITE)
  @Encrypted
  private String apiToken;

  @ApiModelProperty(value = "PA Collector API Token", accessMode = READ_WRITE)
  @Encrypted
  private String paApiToken;

  @NotNull
  @ApiModelProperty(value = "Metrics Scrape Period Seconds", accessMode = READ_WRITE)
  private Long metricsScrapePeriodSecs;

  public PACollector generateUUID() {
    this.uuid = UUID.randomUUID();
    return this;
  }

  @JsonIgnore
  public boolean isNew() {
    return uuid == null;
  }

  private static final Finder<UUID, PACollector> find =
      new Finder<UUID, PACollector>(PACollector.class) {};

  public static ExpressionList<PACollector> createQuery() {
    return find.query().where();
  }
}
