// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.params;

import com.yugabyte.yw.forms.AbstractTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import io.ebean.annotation.JsonIgnore;
import java.util.UUID;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Data
@EqualsAndHashCode(callSuper = true)
public class DetachedNodeTaskParams extends AbstractTaskParams implements INodeTaskParams {
  public static final String DEFAULT_NODE_NAME = "detached_node";

  private UUID azUuid;

  private UUID nodeUuid;

  private String instanceType;

  @JsonIgnore private AvailabilityZone zone;

  @Override
  public AvailabilityZone getAZ() {
    if (zone == null) {
      zone = INodeTaskParams.super.getAZ();
    }
    return zone;
  }

  @Override
  public String getNodeName() {
    return DEFAULT_NODE_NAME;
  }
}
