// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.models;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;

import com.yugabyte.yw.models.helpers.TimeUnit;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.Transactional;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import jakarta.persistence.Entity;
import jakarta.persistence.Id;
import jakarta.persistence.JoinColumn;
import java.time.Instant;
import java.util.List;
import java.util.Optional;
import java.util.UUID;
import lombok.Getter;
import lombok.Setter;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Entity
@ApiModel(description = "continuous YBA backup config object")
@Getter
@Setter
public class ContinuousBackupConfig extends Model {

  private static final Finder<UUID, ContinuousBackupConfig> find =
      new Finder<>(ContinuousBackupConfig.class) {};

  @Id
  @ApiModelProperty(value = "Continuous backup config UUID")
  private UUID uuid;

  @JoinColumn(name = "storage_config_uuid", referencedColumnName = "config_uuid")
  @ApiModelProperty(value = "storage configuration UUID", accessMode = READ_WRITE)
  private UUID storageConfigUUID;

  @ApiModelProperty(value = "wait between backups", accessMode = READ_WRITE)
  private long frequency = 0L;

  @ApiModelProperty(value = "time unit for wait between backups", accessMode = READ_WRITE)
  private TimeUnit frequencyTimeUnit = TimeUnit.MINUTES;

  @ApiModelProperty(
      value = "the number of previous backups to retain",
      accessMode = READ_WRITE,
      example = "5")
  private int numBackupsToRetain = 5;

  @ApiModelProperty(value = "the folder in storage config to store backups for this YBA")
  private String backupDir;

  @ApiModelProperty(value = "the specific cloud storage path for backups")
  private String storageLocation;

  @ApiModelProperty(value = "the last time a successful backup occurred")
  private long lastBackup;

  @Transactional
  public static ContinuousBackupConfig create(
      UUID uuid, long frequency, TimeUnit timeUnit, int numBackups, String backupDir) {
    ContinuousBackupConfig cbConfig = new ContinuousBackupConfig();
    cbConfig.storageConfigUUID = uuid;
    cbConfig.frequency = frequency;
    cbConfig.frequencyTimeUnit = timeUnit;
    cbConfig.numBackupsToRetain = numBackups;
    cbConfig.backupDir = backupDir;
    cbConfig.save();
    return cbConfig;
  }

  public static Optional<ContinuousBackupConfig> get() {
    return find.query().where().findOneOrEmpty();
  }

  public static Optional<ContinuousBackupConfig> get(UUID uuid) {
    return Optional.ofNullable(find.byId(uuid));
  }

  public static List<ContinuousBackupConfig> getAll() {
    return find.query().findList();
  }

  public static void delete(UUID uuid) {
    find.deleteById(uuid);
  }

  public void updateLastBackup() {
    this.lastBackup = Instant.now().toEpochMilli();
    this.update();
  }

  public void updateStorageLocation(String storageLocation) {
    this.storageLocation = storageLocation;
    this.update();
  }
}
