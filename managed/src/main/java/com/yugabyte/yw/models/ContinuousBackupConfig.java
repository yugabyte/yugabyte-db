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

  private static final long NANOSECONDS_IN_MICROSECOND = 1000L;
  private static final long MICROSECONDS_IN_MILLISECOND = 1000L;
  private static final long MILLISECONDS_IN_SECOND = 1000L;
  private static final long SECONDS_IN_MINUTE = 60L;
  private static final long MINUTES_IN_HOUR = 60L;
  private static final long HOURS_IN_DAY = 24L;
  private static final long DAYS_IN_MONTH = 30L;
  private static final long DAYS_IN_YEAR = 365L;

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
    if (frequencyInMilliseconds
        > MILLISECONDS_IN_SECOND * SECONDS_IN_MINUTE * MINUTES_IN_HOUR * HOURS_IN_DAY) {
      throw new PlatformServiceException(BAD_REQUEST, "Frequency must be less than 1 day");
    }
    if (frequencyInMilliseconds < MILLISECONDS_IN_SECOND * SECONDS_IN_MINUTE * 2) {
      throw new PlatformServiceException(BAD_REQUEST, "Frequency must be at least 2 minutes");
    }
  }

  public long getFrequencyInMilliseconds() {
    switch (this.getFrequencyTimeUnit()) {
      case NANOSECONDS:
        return this.getFrequency() / (NANOSECONDS_IN_MICROSECOND * MICROSECONDS_IN_MILLISECOND);
      case MICROSECONDS:
        return this.getFrequency() / MICROSECONDS_IN_MILLISECOND;
      case MILLISECONDS:
        return this.getFrequency();
      case SECONDS:
        return this.getFrequency() * MILLISECONDS_IN_SECOND;
      case MINUTES:
        return this.getFrequency() * MILLISECONDS_IN_SECOND * SECONDS_IN_MINUTE;
      case HOURS:
        return this.getFrequency() * MILLISECONDS_IN_SECOND * SECONDS_IN_MINUTE * MINUTES_IN_HOUR;
      case DAYS:
        return this.getFrequency()
            * MILLISECONDS_IN_SECOND
            * SECONDS_IN_MINUTE
            * MINUTES_IN_HOUR
            * HOURS_IN_DAY;
      case MONTHS:
        return this.getFrequency()
            * MILLISECONDS_IN_SECOND
            * SECONDS_IN_MINUTE
            * MINUTES_IN_HOUR
            * HOURS_IN_DAY
            * DAYS_IN_MONTH;
      case YEARS:
        return this.getFrequency()
            * MILLISECONDS_IN_SECOND
            * SECONDS_IN_MINUTE
            * MINUTES_IN_HOUR
            * HOURS_IN_DAY
            * DAYS_IN_YEAR;
    }
    return this.getFrequency();
  }
}
