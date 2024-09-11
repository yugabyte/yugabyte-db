// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.migrations;

import com.fasterxml.jackson.annotation.JsonIgnore;
import io.ebean.Finder;
import io.ebean.Model;
import jakarta.persistence.CascadeType;
import jakarta.persistence.Entity;
import jakarta.persistence.Id;
import jakarta.persistence.JoinColumn;
import jakarta.persistence.ManyToOne;
import jakarta.persistence.OneToMany;
import java.util.List;
import java.util.UUID;
import lombok.Data;

public class V376 {

  @Entity
  @Data
  public static class DrConfig extends Model {

    public static final Finder<UUID, DrConfig> find = new Finder<UUID, DrConfig>(DrConfig.class) {};

    @Id private UUID uuid;

    @OneToMany(mappedBy = "drConfig", cascade = CascadeType.ALL)
    @JsonIgnore
    private List<XClusterConfig> xClusterConfigs;

    @ManyToOne
    @JoinColumn(name = "storage_config_uuid", referencedColumnName = "config_uuid")
    @JsonIgnore
    private UUID storageConfigUuid;

    private Long pitrRetentionPeriodSec;

    private Long pitrSnapshotIntervalSec;

    public static List<DrConfig> getAll() {
      return find.query().where().findList();
    }
  }

  @Entity
  @Data
  public static class XClusterConfig extends Model {

    @Id private UUID uuid;

    @ManyToOne
    @JoinColumn(name = "source_universe_uuid", referencedColumnName = "universe_uuid")
    private UUID sourceUniverseUUID;

    @ManyToOne
    @JoinColumn(name = "target_universe_uuid", referencedColumnName = "universe_uuid")
    private UUID targetUniverseUUID;

    @ManyToOne(cascade = {CascadeType.PERSIST, CascadeType.MERGE, CascadeType.REFRESH})
    @JoinColumn(name = "dr_config_uuid", referencedColumnName = "uuid")
    @JsonIgnore
    private DrConfig drConfig;

    private boolean secondary;
  }
}
