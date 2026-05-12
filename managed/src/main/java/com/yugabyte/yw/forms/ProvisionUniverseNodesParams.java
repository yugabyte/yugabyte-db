// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import io.swagger.annotations.ApiModelProperty;
import java.util.Set;

@JsonIgnoreProperties(ignoreUnknown = true)
@JsonDeserialize(converter = ProvisionUniverseNodesParams.Converter.class)
public class ProvisionUniverseNodesParams extends UpgradeTaskParams {

  @ApiModelProperty(value = "Node names to provision (empty means all nodes)")
  public Set<String> nodeNames;

  public static class Converter extends BaseConverter<ProvisionUniverseNodesParams> {}
}
