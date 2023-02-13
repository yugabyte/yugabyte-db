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

import static com.yugabyte.yw.models.helpers.CommonUtils.appendInClause;
import static com.yugabyte.yw.models.helpers.EntityOperation.CREATE;
import static com.yugabyte.yw.models.helpers.EntityOperation.UPDATE;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.AlertConfiguration;
import com.yugabyte.yw.models.AlertTemplateVariable;
import com.yugabyte.yw.models.helpers.EntityOperation;
import io.ebean.annotation.Transactional;
import java.util.Collection;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.UUID;
import java.util.function.Function;
import java.util.stream.Collectors;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections.CollectionUtils;

@Singleton
@Slf4j
public class AlertTemplateVariableService {

  private final BeanValidator beanValidator;

  @Inject
  public AlertTemplateVariableService(BeanValidator beanValidator) {
    this.beanValidator = beanValidator;
  }

  @Transactional
  public List<AlertTemplateVariable> save(
      UUID customerUuid, List<AlertTemplateVariable> variables) {
    if (CollectionUtils.isEmpty(variables)) {
      return variables;
    }

    List<AlertTemplateVariable> beforeVariables = Collections.emptyList();
    Set<UUID> variableUuids =
        variables
            .stream()
            .filter(variable -> !variable.isNew())
            .map(AlertTemplateVariable::getUuid)
            .collect(Collectors.toSet());
    if (!variableUuids.isEmpty()) {
      beforeVariables = list(variableUuids);
    }
    Map<UUID, AlertTemplateVariable> beforeConfigMap =
        beforeVariables
            .stream()
            .collect(Collectors.toMap(AlertTemplateVariable::getUuid, Function.identity()));

    List<AlertTemplateVariable> existingVariablesWithSameName = Collections.emptyList();
    Set<String> newVariableNames =
        variables
            .stream()
            .filter(AlertTemplateVariable::isNew)
            .map(AlertTemplateVariable::getName)
            .filter(Objects::nonNull)
            .collect(Collectors.toSet());
    if (!newVariableNames.isEmpty()) {
      existingVariablesWithSameName = list(customerUuid, newVariableNames);
    }
    Map<String, AlertTemplateVariable> existingVariablesWithSameNameMap =
        existingVariablesWithSameName
            .stream()
            .collect(Collectors.toMap(AlertTemplateVariable::getName, Function.identity()));

    Map<EntityOperation, List<AlertTemplateVariable>> toCreateAndUpdate =
        variables
            .stream()
            .peek(
                configuration ->
                    validate(
                        configuration,
                        beforeConfigMap.get(configuration.getUuid()),
                        existingVariablesWithSameNameMap))
            .collect(
                Collectors.groupingBy(configuration -> configuration.isNew() ? CREATE : UPDATE));

    List<AlertTemplateVariable> toCreate =
        toCreateAndUpdate.getOrDefault(CREATE, Collections.emptyList());
    toCreate.forEach(AlertTemplateVariable::generateUUID);

    List<AlertTemplateVariable> toUpdate =
        toCreateAndUpdate.getOrDefault(UPDATE, Collections.emptyList());

    if (!CollectionUtils.isEmpty(toCreate)) {
      AlertConfiguration.db().saveAll(toCreate);
    }
    if (!CollectionUtils.isEmpty(toUpdate)) {
      AlertConfiguration.db().updateAll(toUpdate);
    }
    // TODO remove variable values from alert configs
    return variables;
  }

  @Transactional
  public AlertTemplateVariable save(AlertTemplateVariable variable) {
    return save(variable.getCustomerUUID(), Collections.singletonList(variable)).get(0);
  }

  public AlertTemplateVariable get(UUID uuid) {
    if (uuid == null) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Can't get Alert Template Variable by null uuid");
    }
    return list(Collections.singleton(uuid)).stream().findFirst().orElse(null);
  }

  public AlertTemplateVariable getOrBadRequest(UUID uuid) {
    AlertTemplateVariable variable = get(uuid);
    if (variable == null) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Invalid Alert Template Variable UUID: " + uuid);
    }
    return variable;
  }

  public List<AlertTemplateVariable> list(Set<UUID> uuids) {
    return appendInClause(AlertTemplateVariable.createQuery(), "uuid", uuids).findList();
  }

  public List<AlertTemplateVariable> list(UUID customerUuid, Set<String> names) {
    return appendInClause(AlertTemplateVariable.createQuery(), "name", names)
        .eq("customerUUID", customerUuid)
        .findList();
  }

  public List<AlertTemplateVariable> list(UUID customerUuid) {
    return AlertTemplateVariable.createQuery().eq("customerUUID", customerUuid).findList();
  }

  @Transactional
  public void delete(UUID uuid) {
    AlertTemplateVariable variable = getOrBadRequest(uuid);
    delete(variable.getCustomerUUID(), Collections.singleton(variable));
  }

  @Transactional
  public void delete(UUID customerUuid, Collection<AlertTemplateVariable> variables) {
    if (CollectionUtils.isEmpty(variables)) {
      return;
    }
    Set<UUID> uuidsToDelete =
        variables.stream().map(AlertTemplateVariable::getUuid).collect(Collectors.toSet());

    // TODO check that variable is not used in templates and delete values from configs.

    appendInClause(AlertTemplateVariable.createQuery(), "uuid", uuidsToDelete).delete();
  }

  private void validate(
      AlertTemplateVariable variable,
      AlertTemplateVariable before,
      Map<String, AlertTemplateVariable> existingVariablesByName) {
    beanValidator.validate(variable);
    if (before != null) {
      if (!variable.getCustomerUUID().equals(before.getCustomerUUID())) {
        beanValidator
            .error()
            .forField("customerUUID", "can't change for variable '" + variable.getName() + "'")
            .throwError();
      }
      if (!variable.getName().equals(before.getName())) {
        beanValidator
            .error()
            .forField("name", "can't change for variable '" + before.getName() + "'")
            .throwError();
      }
    } else {
      if (!variable.isNew()) {
        beanValidator
            .error()
            .forField("", "can't update missing variable '" + variable.getName() + "'")
            .throwError();
      }
      if (existingVariablesByName.containsKey(variable.getName())) {
        beanValidator
            .error()
            .forField("name", "duplicate variable '" + variable.getName() + "'")
            .throwError();
      }
    }
  }
}
