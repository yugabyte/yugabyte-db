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
import lombok.extern.slf4j.Slf4j;

import javax.inject.Singleton;
import java.util.List;
import java.util.UUID;
import java.util.function.Consumer;

import static com.yugabyte.yw.models.AlertDefinition.createQueryByFilter;
import static play.mvc.Http.Status.BAD_REQUEST;

@Singleton
@Slf4j
public class AlertDefinitionService {

  public AlertDefinition create(AlertDefinition definition) {
    if (definition.getUuid() != null) {
      throw new IllegalArgumentException("Can't create alert definition with predefined uuid");
    }
    definition.generateUUID();
    definition.save();
    log.debug("Alert definition {} created", definition);
    return definition;
  }

  public AlertDefinition get(UUID uuid) {
    if (uuid == null) {
      throw new IllegalArgumentException("Can't get alert definition by null uuid");
    }
    return list(AlertDefinitionFilter.builder().uuids(uuid).build())
        .stream()
        .findFirst()
        .orElse(null);
  }

  public AlertDefinition getOrBadRequest(UUID uuid) {
    if (uuid == null) {
      throw new YWServiceException(BAD_REQUEST, "Invalid Alert Definition UUID: " + uuid);
    }
    AlertDefinition definition = get(uuid);
    if (definition == null) {
      throw new YWServiceException(BAD_REQUEST, "Invalid Alert Definition UUID: " + uuid);
    }
    return definition;
  }

  public List<AlertDefinition> list(AlertDefinitionFilter filter) {
    return createQueryByFilter(filter).findList();
  }

  public List<UUID> listIds(AlertDefinitionFilter filter) {
    return createQueryByFilter(filter).findIds();
  }

  public void process(AlertDefinitionFilter filter, Consumer<AlertDefinition> consumer) {
    createQueryByFilter(filter).findEach(consumer);
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
    log.debug("Alert definition {} updated", definition);
    return definition;
  }

  public void delete(UUID uuid) {
    AlertDefinition definition = get(uuid);
    if (definition == null) {
      log.warn("Alert definition {} is already deleted", uuid);
      return;
    }
    definition.delete();
    log.debug("Alert definition {} deleted", uuid);
  }

  public void delete(AlertDefinitionFilter filter) {
    int deleted = createQueryByFilter(filter).delete();
    log.debug("{} alert definitions deleted", deleted);
  }
}
