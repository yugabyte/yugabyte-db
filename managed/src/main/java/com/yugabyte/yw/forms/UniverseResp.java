/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonInclude;
import com.yugabyte.yw.cloud.UniverseResourceDetails;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.Collection;
import java.util.Map;
import java.util.UUID;

@JsonInclude(JsonInclude.Include.NON_NULL)
public class UniverseResp {

  public static final Logger LOG = LoggerFactory.getLogger(UniverseResp.class);

  public final String universeUUID;
  public final String name;
  public final String creationDate;
  public final int version;
  public final String dnsName;

  public final UniverseResourceDetails resources;

  public final UniverseDefinitionTaskParamsResp universeDetails;
  public final Map<String, String> universeConfig;
  public final String taskUUID;

  public UniverseResp(Universe entity) {
    this(entity, null, null);
  }

  public UniverseResp(Universe entity, UUID taskUUID) {
    this(entity, taskUUID, null);
  }

  public UniverseResp(Universe entity, UUID taskUUID, UniverseResourceDetails resources) {
    universeUUID = entity.universeUUID.toString();
    name = entity.name;
    creationDate = entity.creationDate.toString();
    version = entity.version;
    dnsName = entity.getDnsName();
    universeDetails = new UniverseDefinitionTaskParamsResp(entity.getUniverseDetails(), entity);
    this.taskUUID = taskUUID == null ? null : taskUUID.toString();
    Collection<NodeDetails> nodes = entity.getUniverseDetails().nodeDetailsSet;

    this.resources = resources;
    universeConfig = entity.getConfig();
  }

  // TODO(UI folks): Remove this. This is redundant as it is already available in resources
  public Double getPricePerHour() {
    return resources == null ? null : resources.pricePerHour;
  }
}
