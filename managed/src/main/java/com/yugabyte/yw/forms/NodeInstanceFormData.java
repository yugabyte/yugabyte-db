// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.forms;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;

import com.yugabyte.yw.models.helpers.NodeConfig;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.List;
import java.util.Set;
import javax.validation.constraints.NotNull;
import javax.validation.constraints.Size;

/** This class will be used by the API validate constraints for NodeInstance data. */
public class NodeInstanceFormData {

  @NotNull
  @Size(min = 1)
  @ApiModelProperty(value = "Node instances", required = true)
  public List<NodeInstanceData> nodes;

  @ApiModel(
      description =
          "Details of a node instance. Used by the API to validate data against input constraints.")
  public static class NodeInstanceData {

    @NotNull
    @ApiModelProperty(value = "IP address", example = "1.1.1.1", required = true)
    public String ip;

    @NotNull
    @ApiModelProperty(value = "SSH user", example = "centos", required = true)
    public String sshUser;

    @NotNull
    @ApiModelProperty(value = "Region", example = "south-east", required = true)
    public String region;

    @NotNull
    @ApiModelProperty(value = "Zone", example = "south-east", required = true)
    public String zone;

    @NotNull
    @ApiModelProperty(value = "Node instance type", example = "c5large", required = true)
    public String instanceType;

    @NotNull
    @ApiModelProperty(value = "Node instance name", example = "Mumbai instance", required = true)
    public String instanceName;

    @ApiModelProperty(
        value = "Node name in a universe",
        example = "universe_node1",
        accessMode = READ_ONLY)
    public String nodeName;

    @ApiModelProperty(value = "Node configurations", hidden = true)
    /**
     * Hide this as it is not persisted. See {{@link
     * com.yugabyte.yw.models.NodeInstance#setDetails(NodeInstanceData)}
     */
    public Set<NodeConfig> nodeConfigs;
  }
}
