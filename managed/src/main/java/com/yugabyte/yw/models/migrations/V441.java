// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models.migrations;

import com.fasterxml.jackson.databind.JsonNode;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.DbJson;
import jakarta.persistence.Column;
import jakarta.persistence.Entity;
import jakarta.persistence.Id;
import jakarta.persistence.Table;
import java.util.List;
import java.util.UUID;
import lombok.Getter;
import lombok.Setter;

/** Snapshot View of ORM entities at the time migration V440 was added. */
public class V441 {

  @Entity
  @Table(name = "universe")
  @Getter
  @Setter
  public static class Universe extends Model {

    @Id public UUID universeUUID;

    @Column(columnDefinition = "TEXT", nullable = false)
    public String universeDetailsJson;

    public static final Finder<UUID, Universe> find = new Finder<>(Universe.class);

    public static List<Universe> getAll() {
      return find.query().findList();
    }
  }

  @Entity
  @Table(name = "export_telemetry_config")
  @Getter
  @Setter
  public static class ExportTelemetryConfig extends Model {

    @Id
    @Column(name = "universe_uuid", nullable = false)
    private UUID universeUuid;

    @DbJson
    @Column(name = "telemetry_config")
    private TelemetryConfig telemetryConfig;

    public static final Finder<UUID, ExportTelemetryConfig> find =
        new Finder<>(ExportTelemetryConfig.class);
  }

  /** Snapshot of TelemetryConfig structure using JsonNode for nested configs. */
  @Getter
  @Setter
  public static class TelemetryConfig {
    private JsonNode auditLogConfig;
    private JsonNode queryLogConfig;
    private JsonNode metricsExportConfig;
  }
}
