// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import java.util.UUID;
import lombok.Data;
import lombok.NoArgsConstructor;

@NoArgsConstructor
@JsonIgnoreProperties(ignoreUnknown = true)
@Data
public class UniverseBackupRequestParams extends UniverseTaskParams {

  private UUID universeUUID;

  public UUID storageConfigUUID;

  public long timeBeforeDelete = 0L;

  public UUID customerUUID;

  public UniverseBackupRequestParams(
      UniverseBackupRequestFormData formData, UUID customerUUID, UUID universeUUID) {
    this.storageConfigUUID = formData.storageConfigUUID;
    this.setUniverseUUID(universeUUID);
    this.timeBeforeDelete = formData.timeBeforeDelete;
    this.customerUUID = customerUUID;
  }
}
