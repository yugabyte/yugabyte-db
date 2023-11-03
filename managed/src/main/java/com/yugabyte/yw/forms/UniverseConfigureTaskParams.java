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

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import io.swagger.annotations.ApiModelProperty;

@JsonIgnoreProperties(ignoreUnknown = true)
@JsonDeserialize(converter = UniverseConfigureTaskParams.Converter.class)
public class UniverseConfigureTaskParams extends UniverseDefinitionTaskParams {

  @ApiModelProperty public ClusterOperationType clusterOperation;

  @ApiModelProperty public boolean allowGeoPartitioning;

  @Deprecated @ApiModelProperty public Boolean regionsChanged;

  public enum ClusterOperationType {
    CREATE,
    EDIT,
    DELETE // This is never used
  }

  public static class Converter extends BaseConverter<UniverseConfigureTaskParams> {}
}
