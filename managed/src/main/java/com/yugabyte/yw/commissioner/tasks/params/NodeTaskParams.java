// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.params;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import io.ebean.annotation.JsonIgnore;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;

@JsonIgnoreProperties(ignoreUnknown = true)
@JsonDeserialize(converter = NodeTaskParams.Converter.class)
public class NodeTaskParams extends UniverseDefinitionTaskParams implements INodeTaskParams {
  // The AZ in which the node should be. This can be used to find the region.
  public UUID azUuid;

  // The node about which we need to fetch details.
  public String nodeName;

  // The UUID of the node that we have selected.
  public UUID nodeUuid;

  // The UUID of the primary/read-replica cluster to which the node belongs.
  public UUID placementUuid;

  // The type of instance for this node.
  public String instanceType;

  public boolean useSystemd;
  // Using custom ssh user

  public String sshUserOverride;

  public Integer sshPortOverride;

  public Map<String, String> tags;

  @JsonIgnore private AvailabilityZone zone;

  @Override
  public String getNodeName() {
    return nodeName;
  }

  @Override
  public UUID getAzUuid() {
    return azUuid;
  }

  @Override
  public AvailabilityZone getAZ() {
    if (zone == null) {
      zone = INodeTaskParams.super.getAZ();
    }
    return zone;
  }

  // Less prominent params can be added to properties variable
  private Map<String, String> properties = new HashMap<>();

  public Map<String, String> getProperties() {
    return properties;
  }

  public void setProperty(String key, String value) {
    properties.put(key, value);
  }

  public String getProperty(String key) {
    return properties.getOrDefault(key, null);
  }

  public static class Converter extends BaseConverter<NodeTaskParams> {}
}
