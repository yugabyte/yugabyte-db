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
import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.cloud.UniverseResourceDetails;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.Collection;
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
  public final JsonNode universeConfig;
  public final String taskUUID;

  public UniverseResp(Universe entity, UUID taskUUID) {
    universeUUID = entity.universeUUID.toString();
    name = entity.name;
    creationDate = entity.creationDate.toString();
    version = entity.version;
    dnsName = entity.getDnsName();
    universeDetails = new UniverseDefinitionTaskParamsResp(entity.getUniverseDetails(), entity);
    this.taskUUID = taskUUID == null ? null : taskUUID.toString();
    Collection<NodeDetails> nodes = entity.getUniverseDetails().nodeDetailsSet;

    resources = getResourcesOrNull(entity);
    universeConfig = entity.config;
  }

  // TODO(UI folks): Remove this. This is redundant as it is already available in resources
  public Double getPricePerHour() {
    return resources == null ? null : resources.pricePerHour;
  }

  private static UniverseResourceDetails getResourcesOrNull(Universe entity) {
    try {
      return UniverseResourceDetails.create(entity.getUniverseDetails());
    } catch (Exception exception) {
      // TODO: fixme. Because old Universe.toJson code was just silently masking all
      //  the bugs in resources generation.
      LOG.debug("Exception when generating UniverseResourceDetails", exception);
      return null;
    }
  }

}
