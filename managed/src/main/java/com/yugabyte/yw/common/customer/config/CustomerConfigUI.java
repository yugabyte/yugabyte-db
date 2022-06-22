/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common.customer.config;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;

import com.fasterxml.jackson.annotation.JsonUnwrapped;
import com.yugabyte.yw.common.Util.UniverseDetailSubset;
import com.yugabyte.yw.models.configs.CustomerConfig;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.List;
import lombok.Data;
import lombok.experimental.Accessors;

@Data
@Accessors(chain = true)
@ApiModel(
    description =
        "Customer configuration with additional information. "
            + "Includes storage, alerts, password policy, and call-home level.")
public class CustomerConfigUI {
  @ApiModelProperty(value = "Customer configuration", accessMode = READ_ONLY)
  @JsonUnwrapped
  private CustomerConfig customerConfig;

  @ApiModelProperty(
      value = "True if there is an in use reference to the object",
      accessMode = READ_ONLY)
  private boolean inUse;

  @ApiModelProperty(value = "Universe details", example = "{\"name\": \"jd-aws-21-6-21-test4\"}")
  private List<UniverseDetailSubset> universeDetails;
}
