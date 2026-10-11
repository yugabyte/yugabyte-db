// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;
import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.yugabyte.yw.common.pa.PerfAdvisorClient.ExportMetricsType;
import com.yugabyte.yw.models.helpers.paendpoint.PaEndpointAuth;
import com.yugabyte.yw.models.helpers.paendpoint.PerfAdvisorEndpointType;
import io.ebean.ExpressionList;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.DbJson;
import io.ebean.annotation.Encrypted;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import jakarta.persistence.Entity;
import jakarta.persistence.EnumType;
import jakarta.persistence.Enumerated;
import jakarta.persistence.Id;
import jakarta.persistence.Table;
import java.util.Date;
import java.util.UUID;
import javax.validation.Valid;
import javax.validation.constraints.NotNull;
import lombok.Data;
import lombok.EqualsAndHashCode;
import org.hibernate.validator.constraints.URL;

/**
 * An external destination a universe's collected data can be sent to when it is registered in
 * online mode.
 *
 * <p>YBA owns this record and pushes it to a Perf Advisor Collector as that collector's {@code
 * ExportConfig}, keyed by this row's own {@link #uuid}: the collector's PUT is an upsert, so one
 * identity serves both sides and there is nothing to reconcile by name.
 */
@Data
@EqualsAndHashCode(callSuper = false)
@Entity
@Table(name = "perf_advisor_endpoint")
@ApiModel(description = "Perf Advisor Endpoint Model")
public class PerfAdvisorEndpoint extends Model {

  @NotNull
  @Id
  @ApiModelProperty(value = "Perf Advisor Endpoint UUID", accessMode = READ_ONLY)
  private UUID uuid;

  @NotNull
  @ApiModelProperty(value = "Customer UUID", accessMode = READ_ONLY)
  private UUID customerUUID;

  @NotNull
  @ApiModelProperty(value = "Name, unique per customer", accessMode = READ_WRITE)
  private String name;

  @NotNull
  @Enumerated(EnumType.STRING)
  @ApiModelProperty(value = "Endpoint type", accessMode = READ_WRITE)
  private PerfAdvisorEndpointType type = PerfAdvisorEndpointType.BYOC;

  @NotNull
  @URL
  @ApiModelProperty(value = "Metrics endpoint URL", accessMode = READ_WRITE)
  private String metricsEndpoint;

  @NotNull
  @Enumerated(EnumType.STRING)
  @ApiModelProperty(value = "Metrics protocol", accessMode = READ_WRITE)
  private ExportMetricsType metricsType = ExportMetricsType.otlphttp;

  @Valid
  @DbJson
  @Encrypted
  @ApiModelProperty(value = "Metrics endpoint credentials", accessMode = READ_WRITE)
  private PaEndpointAuth metricsAuth;

  @NotNull
  @URL
  @ApiModelProperty(value = "Collection endpoint URL", accessMode = READ_WRITE)
  private String collectionEndpoint;

  @Valid
  @DbJson
  @Encrypted
  @ApiModelProperty(value = "Collection endpoint credentials", accessMode = READ_WRITE)
  private PaEndpointAuth collectionAuth;

  @ApiModelProperty(value = "YugabyteDB Managed account ID, for a BYOC gateway destination")
  private String ybmAccountId;

  @ApiModelProperty(value = "YugabyteDB Managed project ID, for a BYOC gateway destination")
  private String ybmProjectId;

  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  @ApiModelProperty(
      value = "Creation timestamp",
      example = "2026-08-25T13:07:18Z",
      accessMode = READ_ONLY)
  private Date createTime;

  /**
   * Bumped on every edit. {@code PACollectorSync} compares this against what it last pushed to
   * decide whether a collector's copy is stale, because the collector returns passwords masked and
   * a field-by-field comparison cannot see a changed credential.
   */
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  @ApiModelProperty(
      value = "Last update timestamp",
      example = "2026-08-25T13:07:18Z",
      accessMode = READ_ONLY)
  private Date updateTime;

  public PerfAdvisorEndpoint generateUUID() {
    this.uuid = UUID.randomUUID();
    return this;
  }

  @JsonIgnore
  public boolean isNew() {
    return uuid == null;
  }

  private static final Finder<UUID, PerfAdvisorEndpoint> find =
      new Finder<UUID, PerfAdvisorEndpoint>(PerfAdvisorEndpoint.class) {};

  public static ExpressionList<PerfAdvisorEndpoint> createQuery() {
    return find.query().where();
  }
}
