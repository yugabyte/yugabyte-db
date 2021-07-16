// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;

import play.data.validation.Constraints;

import java.util.List;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;

/** This class will be used by the API validate constraints for NodeInstance data. */
public class NodeInstanceFormData {

  @Constraints.Required
  @ApiModelProperty(value = "Node instances", required = true)
  public List<NodeInstanceData> nodes;

  @ApiModel(description = "Detail of node instance")
  public static class NodeInstanceData {

    @Constraints.Required()
    @ApiModelProperty(value = "IP address of node instance", example = "1.1.1.1", required = true)
    public String ip;

    @Constraints.Required()
    @ApiModelProperty(value = "SSH user of node instance", example = "centos", required = true)
    public String sshUser;

    @Constraints.Required()
    @ApiModelProperty(value = "Region of node instance", example = "south-east", required = true)
    public String region;

    @Constraints.Required()
    @ApiModelProperty(value = "Zone of node instance", example = "south-east", required = true)
    public String zone;

    @Constraints.Required()
    @ApiModelProperty(
        value = "Instance type of node instance",
        example = "c5large",
        required = true)
    public String instanceType;

    @Constraints.Required()
    @ApiModelProperty(
        value = "Instance name of node instance",
        example = "Mumbai instance",
        required = true)
    public String instanceName;

    @ApiModelProperty(value = "Node name of node instance", example = "India node")
    public String nodeName;
  }
}
