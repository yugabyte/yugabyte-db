// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.UUID;
import javax.validation.constraints.NotNull;
import lombok.Data;
import lombok.NoArgsConstructor;

@JsonIgnoreProperties(ignoreUnknown = true)
@Data
@NoArgsConstructor
@ApiModel(description = "Parameters to resume a paused canary software upgrade")
public class ResumeCanaryUpgradeParams {

  @NotNull
  @ApiModelProperty(value = "UUID of the paused upgrade task to resume", required = true)
  public UUID taskUUID;
}
