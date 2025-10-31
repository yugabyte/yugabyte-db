/* Copyright 2024 YugabyteDB, Inc. and Contributors */
package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.yugabyte.yw.common.HaConfigStates.GlobalState;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.PlatformInstance;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Date;
import java.util.List;
import java.util.UUID;

@ApiModel(description = "HA config response")
public class HAConfigGetResp {
  private final HighAvailabilityConfig haConfig;

  public HAConfigGetResp(HighAvailabilityConfig haConfig) {
    this.haConfig = haConfig;
  }

  @ApiModelProperty(value = "HA config UUID")
  public UUID getUUID() {
    return haConfig.getUuid();
  }

  @ApiModelProperty(name = "cluster_key", value = "HA config cluster key")
  @JsonProperty("cluster_key")
  public String getClusterKey() {
    return haConfig.getClusterKey();
  }

  @ApiModelProperty(value = "HA last failover", example = "2022-12-12T13:07:18Z")
  @JsonProperty("last_failover")
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  public Date getLastFailover() {
    return haConfig.getLastFailover();
  }

  @ApiModelProperty(value = "HA config global state")
  @JsonProperty("global_state")
  public GlobalState getGlobalState() {
    return haConfig.computeGlobalState();
  }

  @ApiModelProperty(value = "HA config platform instances")
  public List<PlatformInstance> getInstances() {
    return haConfig.getInstances();
  }

  @ApiModelProperty(value = "HA accepts any certificate")
  @JsonProperty("accept_any_certificate")
  public boolean getAcceptAnyCertificate() {
    return haConfig.getAcceptAnyCertificate();
  }
}
