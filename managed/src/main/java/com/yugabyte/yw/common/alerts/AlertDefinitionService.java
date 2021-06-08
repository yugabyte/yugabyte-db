/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common.alerts;

import com.yugabyte.yw.common.YWServiceException;
import com.yugabyte.yw.models.AlertDefinition;
import com.yugabyte.yw.models.filters.AlertDefinitionFilter;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import javax.inject.Singleton;
import java.util.List;
import java.util.UUID;
import java.util.function.Consumer;

import static play.mvc.Http.Status.BAD_REQUEST;

@Singleton
public class AlertDefinitionService {

  private static final Logger LOG = LoggerFactory.getLogger(AlertDefinitionService.class);

  public AlertDefinition create(AlertDefinition definition) {
    if (definition.getUuid() != null) {
      throw new IllegalArgumentException("Can't create alert definition with predefined uuid");
    }
    definition.generateUUID();
    definition.save();
    LOG.debug("Alert definition {} created", definition);
    return definition;
  }

  public AlertDefinition get(UUID uuid) {
    return list(new AlertDefinitionFilter().setUuid(uuid)).stream().findFirst().orElse(null);
  }

  public AlertDefinition getOrBadRequest(UUID uuid) {
    return list(new AlertDefinitionFilter().setUuid(uuid))
        .stream()
        .findFirst()
        .orElseThrow(
            () -> new YWServiceException(BAD_REQUEST, "Invalid Alert Definition UUID: " + uuid));
  }

  public List<AlertDefinition> list(AlertDefinitionFilter filter) {
    return AlertDefinition.list(filter);
  }

  public List<UUID> listIds(AlertDefinitionFilter filter) {
    return AlertDefinition.listIds(filter);
  }

  public void process(AlertDefinitionFilter filter, Consumer<AlertDefinition> consumer) {
    AlertDefinition.process(filter, consumer);
  }

  public AlertDefinition update(AlertDefinition definition) {
    if (definition.getUuid() == null) {
      throw new IllegalArgumentException("Can't update alert definition without uuid");
    }
    AlertDefinition before = get(definition.getUuid());
    if (!definition.configEquals(before)) {
      definition.setConfigWritten(false);
    }
    definition.save();
    LOG.debug("Alert definition {} updated", definition);
    return definition;
  }

  public void delete(UUID uuid) {
    AlertDefinition definition = get(uuid);
    if (definition == null) {
      LOG.warn("Alert definition {} is already deleted", uuid);
      return;
    }
    definition.delete();
    LOG.debug("Alert definition {} deleted", uuid);
  }

  public void delete(AlertDefinitionFilter filter) {
    int deleted = AlertDefinition.delete(filter);
    LOG.debug("{} alert definitions deleted", deleted);
  }
}
