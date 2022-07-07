// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.google.common.collect.Sets;
import com.yugabyte.yw.models.helpers.NodeConfiguration;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.List;
import java.util.Set;
import java.util.stream.Collectors;
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

    @ApiModelProperty(value = "Node name", example = "India node")
    public String nodeName;

    // TODO This is not mandatory for nodes added from the UI.
    // When it becomes mandatory, add validations in all cases.
    @ApiModelProperty(value = "Node configurations")
    public Set<NodeConfiguration> nodeConfigurations;

    /**
     * Returns all the types which fail the configuration checks.
     *
     * @param typeGroup the type group.
     * @return the set of failed types.
     */
    @JsonIgnore
    public Set<NodeConfiguration.Type> getFailedNodeConfigurationTypes(
        NodeConfiguration.TypeGroup typeGroup) {
      if (nodeConfigurations == null) {
        return typeGroup.getRequiredConfigTypes();
      }
      Set<NodeConfiguration.Type> configuredTypes =
          nodeConfigurations
              .stream()
              .filter(NodeConfiguration::isConfigured)
              .map(config -> config.type)
              .collect(Collectors.toSet());
      return Sets.difference(typeGroup.getRequiredConfigTypes(), configuredTypes);
    }
  }
}
