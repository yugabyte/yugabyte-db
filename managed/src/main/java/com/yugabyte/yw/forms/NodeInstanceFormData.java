// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.List;
import play.data.validation.Constraints;

/** This class will be used by the API validate constraints for NodeInstance data. */
public class NodeInstanceFormData {

  @Constraints.Required
  @ApiModelProperty(value = "Node instances", required = true)
  public List<NodeInstanceData> nodes;

  @ApiModel(
      description =
          "Details of a node instance. Used by the API to validate data against input constraints.")
  public static class NodeInstanceData {

    @Constraints.Required()
    @ApiModelProperty(value = "IP address", example = "1.1.1.1", required = true)
    public String ip;

    @Constraints.Required()
    @ApiModelProperty(value = "SSH user", example = "centos", required = true)
    public String sshUser;

    @Constraints.Required()
    @ApiModelProperty(value = "Region", example = "south-east", required = true)
    public String region;

    @Constraints.Required()
    @ApiModelProperty(value = "Zone", example = "south-east", required = true)
    public String zone;

    @Constraints.Required()
    @ApiModelProperty(value = "Node instance type", example = "c5large", required = true)
    public String instanceType;

    @Constraints.Required()
    @ApiModelProperty(value = "Node instance name", example = "Mumbai instance", required = true)
    public String instanceName;

    @ApiModelProperty(value = "Node name", example = "India node")
    public String nodeName;
  }
}
