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

import static com.yugabyte.yw.models.AlertDefinition.createQueryByFilter;
import static com.yugabyte.yw.models.helpers.EntityOperation.CREATE;
import static com.yugabyte.yw.models.helpers.EntityOperation.UPDATE;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.AlertDefinition;
import com.yugabyte.yw.models.filters.AlertDefinitionFilter;
import com.yugabyte.yw.models.filters.AlertFilter;
import com.yugabyte.yw.models.helpers.EntityOperation;
import io.ebean.DB;
import io.ebean.annotation.Transactional;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.function.Consumer;
import java.util.function.Function;
import java.util.stream.Collectors;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;

@Singleton
@Slf4j
public class AlertDefinitionService {

  private final BeanValidator beanValidator;
  private final AlertService alertService;

  @Inject
  public AlertDefinitionService(BeanValidator beanValidator, AlertService alertService) {
    this.beanValidator = beanValidator;
    this.alertService = alertService;
  }

  @Transactional
  public List<AlertDefinition> save(List<AlertDefinition> definitions) {
    if (CollectionUtils.isEmpty(definitions)) {
      return definitions;
    }

    Set<UUID> definitionUuids =
        definitions.stream()
            .filter(definition -> !definition.isNew())
            .map(AlertDefinition::getUuid)
            .collect(Collectors.toSet());
    Map<UUID, AlertDefinition> beforeDefinitions;
    if (definitionUuids.size() > 0) {
      AlertDefinitionFilter filter = AlertDefinitionFilter.builder().uuids(definitionUuids).build();
      beforeDefinitions =
          list(filter).stream()
              .collect(Collectors.toMap(AlertDefinition::getUuid, Function.identity()));
    } else {
      beforeDefinitions = Collections.emptyMap();
    }

    Map<EntityOperation, List<AlertDefinition>> toCreateAndUpdate =
        definitions.stream()
            .peek(definition -> validate(definition, beforeDefinitions.get(definition.getUuid())))
            .collect(Collectors.groupingBy(definition -> definition.isNew() ? CREATE : UPDATE));

    if (toCreateAndUpdate.containsKey(CREATE)) {
      List<AlertDefinition> toCreate = toCreateAndUpdate.get(CREATE);
      toCreate.forEach(AlertDefinition::generateUUID);
      DB.getDefault().saveAll(toCreate);
    }

    if (toCreateAndUpdate.containsKey(UPDATE)) {
      List<AlertDefinition> toUpdate = toCreateAndUpdate.get(UPDATE);
      DB.getDefault().updateAll(toUpdate);
    }

    log.debug("{} alert definitions saved", definitions.size());
    return definitions;
  }

  @Transactional
  public AlertDefinition save(AlertDefinition definition) {
    return save(Collections.singletonList(definition)).get(0);
  }

  public AlertDefinition get(UUID uuid) {
    if (uuid == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Can't get alert definition by null uuid");
    }
    return list(AlertDefinitionFilter.builder().uuid(uuid).build()).stream()
        .findFirst()
        .orElse(null);
  }

  public AlertDefinition getOrBadRequest(UUID uuid) {
    if (uuid == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Invalid Alert Definition UUID: " + uuid);
    }
    AlertDefinition definition = get(uuid);
    if (definition == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Invalid Alert Definition UUID: " + uuid);
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

  @Transactional
  public void delete(UUID uuid) {
    delete(AlertDefinitionFilter.builder().uuid(uuid).build());
  }

  @Transactional
  public void delete(AlertDefinitionFilter filter) {
    List<AlertDefinition> toDelete = list(filter);
    AlertFilter alertFilter =
        AlertFilter.builder()
            .definitionUuids(
                toDelete.stream().map(AlertDefinition::getUuid).collect(Collectors.toList()))
            .build();
    alertService.markResolved(alertFilter);
    int deleted = createQueryByFilter(filter).delete();
    log.debug("{} alert definitions deleted", deleted);
  }

  private void validate(AlertDefinition definition, AlertDefinition before) {
    beanValidator.validate(definition);
    if (before != null) {
      if (!definition.getCustomerUUID().equals(before.getCustomerUUID())) {
        beanValidator
            .error()
            .forField("customerUUID", "can't change for definition '" + definition.getUuid() + "'")
            .throwError();
      }
    } else if (!definition.isNew()) {
      beanValidator
          .error()
          .forField("", "can't update missing definition '" + definition.getUuid() + "'")
          .throwError();
    }
  }
}
