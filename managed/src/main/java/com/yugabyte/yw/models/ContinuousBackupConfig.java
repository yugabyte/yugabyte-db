// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.common.PlatformServiceException;
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
  private Long lastBackup;

  @Transactional
  public static ContinuousBackupConfig create(
      UUID storageConfigUUID, long frequency, TimeUnit timeUnit, int numBackups, String backupDir) {
    ContinuousBackupConfig cbConfig = new ContinuousBackupConfig();
    cbConfig.storageConfigUUID = storageConfigUUID;
    cbConfig.frequency = frequency;
    cbConfig.frequencyTimeUnit = timeUnit;
    cbConfig.numBackupsToRetain = numBackups;
    cbConfig.backupDir = backupDir;
    cbConfig.validate();
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

  public void validate() {
    long frequencyInMilliseconds = this.getFrequencyInMilliseconds();
    if (frequencyInMilliseconds > 1000 * 60 * 60 * 24) {
      throw new PlatformServiceException(BAD_REQUEST, "Frequency must be less than 1 day");
    }
    if (frequencyInMilliseconds < 1000 * 60 * 2) {
      throw new PlatformServiceException(BAD_REQUEST, "Frequency must be at least 2 minutes");
    }
  }

  public long getFrequencyInMilliseconds() {
    switch (this.getFrequencyTimeUnit()) {
      case NANOSECONDS:
        return this.getFrequency() / 100000;
      case MICROSECONDS:
        return this.getFrequency() / 1000;
      case MILLISECONDS:
        return this.getFrequency();
      case SECONDS:
        return this.getFrequency() * 1000;
      case MINUTES:
        return this.getFrequency() * 1000 * 60;
      case HOURS:
        return this.getFrequency() * 1000 * 60 * 60;
      case DAYS:
        return this.getFrequency() * 1000 * 60 * 60 * 24;
      case MONTHS:
        return this.getFrequency() * 1000 * 60 * 60 * 24 * 30;
      case YEARS:
        return this.getFrequency() * 1000 * 60 * 60 * 24 * 365;
    }
    return this.getFrequency();
  }
}
