// Copyright (c) YugabyteDB, Inc.

package api.v2.mappers;

import api.v2.models.BackupSpec;
import api.v2.models.BackupState;
import api.v2.models.TableType;
import com.yugabyte.yw.models.Backup.BackupCategory;
import com.yugabyte.yw.models.Backup.StorageConfigType;
import com.yugabyte.yw.models.helpers.TimeUnit;
import org.mapstruct.Mapper;
import org.mapstruct.ReportingPolicy;
import org.mapstruct.ValueMapping;
import org.mapstruct.ValueMappings;

/** Explicit v1-to-v2 backup enum mappings with compile-time exhaustiveness checks. */
@Mapper(unmappedSourcePolicy = ReportingPolicy.ERROR)
public interface BackupEnumMapper {

  @ValueMappings({
    @ValueMapping(target = "YQL_TABLE_TYPE", source = "YQL_TABLE_TYPE"),
    @ValueMapping(target = "REDIS_TABLE_TYPE", source = "REDIS_TABLE_TYPE"),
    @ValueMapping(target = "PGSQL_TABLE_TYPE", source = "PGSQL_TABLE_TYPE"),
    @ValueMapping(
        target = "TRANSACTION_STATUS_TABLE_TYPE",
        source = "TRANSACTION_STATUS_TABLE_TYPE")
  })
  TableType toTableType(org.yb.CommonTypes.TableType tableType);

  @ValueMappings({
    @ValueMapping(target = "BACKUP_SCRIPT", source = "YB_BACKUP_SCRIPT"),
    @ValueMapping(target = "CONTROLLER", source = "YB_CONTROLLER")
  })
  BackupSpec.CategoryEnum toCategoryEnum(BackupCategory category);

  @ValueMappings({
    @ValueMapping(target = "S3", source = "S3"),
    @ValueMapping(target = "NFS", source = "NFS"),
    @ValueMapping(target = "AZ", source = "AZ"),
    @ValueMapping(target = "GCS", source = "GCS"),
    @ValueMapping(target = "FILE", source = "FILE")
  })
  BackupSpec.StorageConfigTypeEnum toStorageConfigTypeEnum(StorageConfigType storageConfigType);

  @ValueMappings({
    @ValueMapping(target = "NANOSECONDS", source = "NANOSECONDS"),
    @ValueMapping(target = "MICROSECONDS", source = "MICROSECONDS"),
    @ValueMapping(target = "MILLISECONDS", source = "MILLISECONDS"),
    @ValueMapping(target = "SECONDS", source = "SECONDS"),
    @ValueMapping(target = "MINUTES", source = "MINUTES"),
    @ValueMapping(target = "HOURS", source = "HOURS"),
    @ValueMapping(target = "DAYS", source = "DAYS"),
    @ValueMapping(target = "MONTHS", source = "MONTHS"),
    @ValueMapping(target = "YEARS", source = "YEARS")
  })
  BackupSpec.ExpiryTimeUnitEnum toExpiryTimeUnitEnum(TimeUnit expiryTimeUnit);

  @ValueMappings({
    @ValueMapping(target = "BackupInProgress", source = "InProgress"),
    @ValueMapping(target = "BackupCompleted", source = "Completed"),
    @ValueMapping(target = "BackupFailed", source = "Failed"),
    @ValueMapping(target = "BackupDeleted", source = "Deleted"),
    @ValueMapping(target = "BackupSkipped", source = "Skipped"),
    @ValueMapping(target = "BackupFailedToDelete", source = "FailedToDelete"),
    @ValueMapping(target = "BackupStopping", source = "Stopping"),
    @ValueMapping(target = "BackupStopped", source = "Stopped"),
    @ValueMapping(target = "BackupQueuedForDeletion", source = "QueuedForDeletion"),
    @ValueMapping(target = "BackupQueuedForForcedDeletion", source = "QueuedForForcedDeletion"),
    @ValueMapping(target = "BackupDeleteInProgress", source = "DeleteInProgress")
  })
  BackupState toBackupState(com.yugabyte.yw.models.Backup.BackupState state);
}
