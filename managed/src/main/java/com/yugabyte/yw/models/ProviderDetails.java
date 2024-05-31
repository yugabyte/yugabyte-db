/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.models;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonInclude;
import com.yugabyte.yw.models.AccessKey.MigratedKeyInfoFields;
import com.yugabyte.yw.models.helpers.provider.AWSCloudInfo;
import com.yugabyte.yw.models.helpers.provider.AzureCloudInfo;
import com.yugabyte.yw.models.helpers.provider.GCPCloudInfo;
import com.yugabyte.yw.models.helpers.provider.KubernetesInfo;
import com.yugabyte.yw.models.helpers.provider.LocalCloudInfo;
import com.yugabyte.yw.models.helpers.provider.OnPremCloudInfo;
import io.swagger.annotations.ApiModelProperty;
import java.util.Objects;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.ToString;

@JsonInclude(JsonInclude.Include.NON_NULL)
@Data
// Excluding cloudInfo as cloudInfo has its own equals & hashCode implementation.
@EqualsAndHashCode(
    callSuper = true,
    exclude = {"cloudInfo"})
@ToString(callSuper = true)
public class ProviderDetails extends MigratedKeyInfoFields {

  @Data
  @JsonInclude(JsonInclude.Include.NON_NULL)
  public static class CloudInfo {
    @ApiModelProperty public AWSCloudInfo aws;
    @ApiModelProperty public AzureCloudInfo azu;
    @ApiModelProperty public GCPCloudInfo gcp;
    @ApiModelProperty public KubernetesInfo kubernetes;
    @ApiModelProperty public OnPremCloudInfo onprem;
    @ApiModelProperty public LocalCloudInfo local;
  }

  @ApiModelProperty private CloudInfo cloudInfo;
  // Flag to enable node agent for this provider depending on the runtime config settings.
  @ApiModelProperty public boolean enableNodeAgent;

  @JsonIgnore
  public boolean isUpdateNeeded(ProviderDetails details) {
    return !Objects.equals(this.getCloudInfo(), details.getCloudInfo());
  }
}
