// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.params;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import java.util.UUID;

public interface INodeTaskParams {

  String getNodeName();

  UUID getAzUuid();

  @JsonIgnore
  default AvailabilityZone getAZ() {
    UUID azUuid = getAzUuid();
    if (azUuid != null) {
      return AvailabilityZone.find.query().fetch("region").where().idEq(azUuid).findOne();
    }
    return null;
  }

  @JsonIgnore
  default Region getRegion() {
    return getAZ() == null ? null : getAZ().getRegion();
  }

  @JsonIgnore
  default Provider getProvider() {
    return getAZ() == null ? null : getAZ().getProvider();
  }
}
