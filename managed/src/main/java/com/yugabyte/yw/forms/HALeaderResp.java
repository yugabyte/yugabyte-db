/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.forms;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import io.swagger.annotations.ApiModelProperty.AccessMode;
import lombok.Data;

@Data
@ApiModel(value = "HA Leader response")
public class HALeaderResp {

  @ApiModelProperty(
      value = "If this YBA is the High Availability config leader",
      accessMode = AccessMode.READ_ONLY)
  private boolean isHALeader;

  public HALeaderResp(boolean isHALeader) {
    this.isHALeader = isHALeader;
  }
}
