// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms.backuprestore;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.HashSet;
import java.util.Set;
import java.util.UUID;
import lombok.Data;
import play.data.validation.Constraints;

@Data
@ApiModel(value = "Parameters for Advanced Restore preflight checks")
public class AdvancedRestorePreflightParams {
  @ApiModelProperty(value = "Storage config UUID", required = true)
  @Constraints.Required
  private UUID storageConfigUUID;

  @ApiModelProperty(value = "List of backup locations to restore from", required = true)
  @Constraints.Required
  @JsonFormat(with = JsonFormat.Feature.ACCEPT_SINGLE_VALUE_AS_ARRAY)
  private Set<String> backupLocations = new HashSet<>();

  @ApiModelProperty(value = "Target universe UUID", required = true)
  @Constraints.Required
  private UUID universeUUID;

  @ApiModelProperty(value = "Point in restore timestamp in millis")
  protected long restoreToPointInTimeMillis;

  public void validateParams(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);
    Universe.getOrBadRequest(universeUUID);
  }
}
