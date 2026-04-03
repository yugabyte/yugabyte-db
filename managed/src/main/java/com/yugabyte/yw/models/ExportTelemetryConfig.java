// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models;

import com.yugabyte.yw.common.export.TelemetryConfig;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.DbJson;
import io.ebean.annotation.WhenCreated;
import io.ebean.annotation.WhenModified;
import jakarta.persistence.Column;
import jakarta.persistence.Entity;
import jakarta.persistence.Id;
import jakarta.persistence.Table;
import java.util.Date;
import java.util.Optional;
import java.util.UUID;
import lombok.Getter;
import lombok.Setter;

/**
 * Stores unified telemetry export configuration per universe. This is the source of truth; universe
 * details (auditLogConfig, queryLogConfig, metricsExportConfig) are synced from here at the end of
 * the configure task.
 */
@Entity
@Table(name = "export_telemetry_config")
@Getter
@Setter
public class ExportTelemetryConfig extends Model {

  @Id
  @Column(name = "universe_uuid", nullable = false)
  private UUID universeUuid;

  @DbJson
  @Column(name = "telemetry_config")
  private TelemetryConfig telemetryConfig;

  @Column(name = "created_at", nullable = false)
  @WhenCreated
  private Date createdAt;

  @Column(name = "updated_at", nullable = false)
  @WhenModified
  private Date updatedAt;

  public static final Finder<UUID, ExportTelemetryConfig> find =
      new Finder<>(ExportTelemetryConfig.class);

  public static Optional<ExportTelemetryConfig> getForUniverse(UUID universeUuid) {
    return Optional.ofNullable(find.byId(universeUuid));
  }
}
