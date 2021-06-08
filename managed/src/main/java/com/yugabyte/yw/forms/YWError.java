/*
 * Copyright 2021 YugaByte, Inc. and Contributors
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

// TODO: Move this as inner class of YWResults
@ApiModel("Generic error response from Yugawware Platform API")
public class YWError {
  public final boolean success = false;

  @ApiModelProperty(
      value = "User visible unstructurred error message",
      example = "There was a problem creating universe")
  public final String error;

  public YWError(String error) {
    this.error = error;
  }
}
