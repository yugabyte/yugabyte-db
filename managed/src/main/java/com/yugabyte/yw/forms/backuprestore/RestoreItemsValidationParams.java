// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms.backuprestore;

import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Customer;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.List;
import java.util.UUID;
import javax.validation.constraints.NotNull;
import lombok.AllArgsConstructor;
import lombok.Data;
import lombok.NoArgsConstructor;
import lombok.extern.slf4j.Slf4j;

@Data
@AllArgsConstructor
@NoArgsConstructor
@ApiModel(value = "Parameters for validating Restorable keyspace and tables in backup")
@Slf4j
public class RestoreItemsValidationParams {
  @ApiModelProperty(value = "UUID of the backup being restored")
  @NotNull
  protected UUID backupUUID;

  @ApiModelProperty(value = "Point in restore timestamp in millis")
  protected long restoreToPointInTimeMillis;

  @ApiModelProperty(value = "List of keyspace(s) and tables to be restored")
  protected List<KeyspaceTables> keyspaceTables;

  public void validateParams(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);
    Backup backup = Backup.getOrBadRequest(customerUUID, backupUUID);
    long latestCreateTime = 0L;
    if (restoreToPointInTimeMillis > 0L) {
      // Fetch latest successful backup in the backup chain.
      Backup latestSuccessfulBackup =
          Backup.getLastSuccessfulBackupInChain(customerUUID, backup.getBaseBackupUUID());
      if (latestSuccessfulBackup != null) {
        latestCreateTime = latestSuccessfulBackup.backupCreateTimeInMillis();
      }
      // If restore timestamp is ahead of latest backup creation time, fail the request.
      if (latestCreateTime < restoreToPointInTimeMillis) {
        throw new RuntimeException("No backups available to restore to the provided point in time");
      }
    } else {
      log.debug("Restore timestamp not provided, treating as discrete restore");
    }
  }
}
