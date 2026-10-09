// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.forms;

import com.yugabyte.yw.common.pa.PaRegistrationMode;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.UUID;
import lombok.AllArgsConstructor;
import lombok.Data;
import lombok.NoArgsConstructor;

@Data
@NoArgsConstructor
@AllArgsConstructor
@ApiModel("PA Collector universe registration status")
public class PaRegistrationStatusResponse {

  @ApiModelProperty(value = "Whether the universe is registered with PA Collector")
  private boolean success;

  @ApiModelProperty(
      value = "Whether advanced observability (metrics export to Prometheus) is enabled")
  private boolean advancedObservability;

  @ApiModelProperty(value = "How the universe is registered with the collector")
  private PaRegistrationMode mode;

  @ApiModelProperty(value = "Perf Advisor Endpoint UUID, set for ONLINE mode only")
  private UUID paEndpointUuid;

  @ApiModelProperty(value = "Perf Advisor Endpoint name, set for ONLINE mode only")
  private String paEndpointName;
}
