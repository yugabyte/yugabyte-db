// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.UUID;

@ApiModel(description = "Volume encryption settings for AWS EBS volumes")
public class CloudVolumeEncryption {
  // We currently support enabling volume encryption for AWS EBS volumes only.
  @ApiModelProperty(value = "To enable volume encryption or not")
  public boolean enableVolumeEncryption;

  @ApiModelProperty(value = "KMS config UUID to be used for volume encryption")
  public UUID kmsConfigUUID;
}
