package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonFormat;
import io.swagger.annotations.ApiModelProperty;
import java.util.HashSet;
import java.util.Set;
import java.util.UUID;
import lombok.Data;
import play.data.validation.Constraints;

@Data
public class RestorePreflightParams {

  @ApiModelProperty(value = "The backup of which the restore is being attempted")
  private UUID backupUUID;

  @ApiModelProperty(value = "Storage config UUID", required = true)
  @Constraints.Required
  private UUID storageConfigUUID;

  @ApiModelProperty(value = "Target universe UUID", required = true)
  @Constraints.Required
  private UUID universeUUID;

  @ApiModelProperty(value = "List of backup locations", required = true)
  @Constraints.Required
  @JsonFormat(with = JsonFormat.Feature.ACCEPT_SINGLE_VALUE_AS_ARRAY)
  private Set<String> backupLocations = new HashSet<>();
}
