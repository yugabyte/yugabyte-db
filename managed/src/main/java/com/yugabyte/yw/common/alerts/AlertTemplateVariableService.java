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
import com.yugabyte.yw.models.AlertChannel.ChannelType;
import com.yugabyte.yw.models.AlertChannelTemplates;
import com.yugabyte.yw.models.AlertConfiguration;
import com.yugabyte.yw.models.AlertTemplateVariable;
import com.yugabyte.yw.models.filters.AlertConfigurationFilter;
import com.yugabyte.yw.models.helpers.EntityOperation;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import io.ebean.DB;
import io.ebean.annotation.Transactional;
import java.util.Arrays;
import java.util.Collection;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
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
import org.apache.commons.collections4.CollectionUtils;

@Singleton
@Slf4j
public class AlertTemplateVariableService {

  public static final Set<String> SYSTEM_LABELS =
      Arrays.stream(KnownAlertLabels.values())
          .map(KnownAlertLabels::labelName)
          .collect(Collectors.toSet());

  private final BeanValidator beanValidator;

  private final AlertConfigurationService alertConfigurationService;

  private final AlertChannelTemplateService alertChannelTemplateService;

  @Inject
  public AlertTemplateVariableService(
      BeanValidator beanValidator,
      AlertConfigurationService alertConfigurationService,
      AlertChannelTemplateService alertChannelTemplateService) {
    this.beanValidator = beanValidator;
    this.alertConfigurationService = alertConfigurationService;
    this.alertChannelTemplateService = alertChannelTemplateService;
  }

  @Transactional
  public List<AlertTemplateVariable> save(
      UUID customerUuid, List<AlertTemplateVariable> variables) {
    if (CollectionUtils.isEmpty(variables)) {
      return variables;
    }

    List<AlertTemplateVariable> beforeVariables = Collections.emptyList();
    Set<UUID> variableUuids =
        variables.stream()
            .filter(variable -> !variable.isNew())
            .map(AlertTemplateVariable::getUuid)
            .collect(Collectors.toSet());
    if (!variableUuids.isEmpty()) {
      beforeVariables = list(variableUuids);
    }
    Map<UUID, AlertTemplateVariable> beforeVariablesMap =
        beforeVariables.stream()
            .collect(Collectors.toMap(AlertTemplateVariable::getUuid, Function.identity()));

    List<AlertTemplateVariable> existingVariablesWithSameName = Collections.emptyList();
    Set<String> newVariableNames =
        variables.stream()
            .filter(AlertTemplateVariable::isNew)
            .map(AlertTemplateVariable::getName)
            .filter(Objects::nonNull)
            .collect(Collectors.toSet());
    if (!newVariableNames.isEmpty()) {
      existingVariablesWithSameName = list(customerUuid, newVariableNames);
    }
    Map<String, AlertTemplateVariable> existingVariablesWithSameNameMap =
        existingVariablesWithSameName.stream()
            .collect(Collectors.toMap(AlertTemplateVariable::getName, Function.identity()));

    Map<EntityOperation, List<AlertTemplateVariable>> toCreateAndUpdate =
        variables.stream()
            .peek(
                configuration ->
                    validate(
                        configuration,
                        beforeVariablesMap.get(configuration.getUuid()),
                        existingVariablesWithSameNameMap))
            .collect(
                Collectors.groupingBy(configuration -> configuration.isNew() ? CREATE : UPDATE));

    List<AlertTemplateVariable> toCreate =
        toCreateAndUpdate.getOrDefault(CREATE, Collections.emptyList());
    toCreate.forEach(AlertTemplateVariable::generateUUID);

    List<AlertTemplateVariable> toUpdate =
        toCreateAndUpdate.getOrDefault(UPDATE, Collections.emptyList());

    if (!CollectionUtils.isEmpty(toCreate)) {
      DB.getDefault().saveAll(toCreate);
    }
    if (!CollectionUtils.isEmpty(toUpdate)) {
      Map<String, Set<String>> removedValues = new HashMap<>();
      DB.getDefault().updateAll(toUpdate);

      toUpdate.forEach(
          updated -> {
            AlertTemplateVariable before = beforeVariablesMap.get(updated.getUuid());
            before
                .getPossibleValues()
                .forEach(
                    value -> {
                      if (!updated.getPossibleValues().contains(value)) {
                        removedValues
                            .computeIfAbsent(updated.getName(), k -> new HashSet<>())
                            .add(value);
                      }
                    });
          });
      cleanAffectedConfigs(customerUuid, removedValues);
    }

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
    return AlertTemplateVariable.list(customerUuid);
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

    Map<ChannelType, Set<String>> alertChannelTemplateVariables =
        alertChannelTemplateService.list(customerUuid).stream()
            .collect(
                Collectors.toMap(
                    AlertChannelTemplates::getType, AlertChannelTemplates::getCustomVariablesSet));
    variables.forEach(variable -> validateVariableRemoval(variable, alertChannelTemplateVariables));

    appendInClause(AlertTemplateVariable.createQuery(), "uuid", uuidsToDelete).delete();

    Map<String, Set<String>> removedValues =
        variables.stream()
            .collect(
                Collectors.toMap(
                    AlertTemplateVariable::getName, AlertTemplateVariable::getPossibleValues));
    cleanAffectedConfigs(customerUuid, removedValues);
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
    if (SYSTEM_LABELS.contains(variable.getName())) {
      beanValidator
          .error()
          .forField("name", "variable '" + variable.getName() + "' is a system variable")
          .throwError();
    }
    if (!variable.getPossibleValues().contains(variable.getDefaultValue())) {
      beanValidator
          .error()
          .forField("defaultValue", "default value is missing from possible values list")
          .throwError();
    }
  }

  private void validateVariableRemoval(
      AlertTemplateVariable variable, Map<ChannelType, Set<String>> alertChannelTemplateVariables) {
    alertChannelTemplateVariables.forEach(
        (channelType, variables) -> {
          if (variables.contains(variable.getName())) {
            beanValidator
                .error()
                .forField(
                    "",
                    "variable '"
                        + variable.getName()
                        + "' is used in '"
                        + channelType.name()
                        + "' template")
                .throwError();
          }
        });
  }

  private void cleanAffectedConfigs(UUID customerUuid, Map<String, Set<String>> removedValues) {
    List<AlertConfiguration> alertConfigurations =
        alertConfigurationService.list(
            AlertConfigurationFilter.builder().customerUuid(customerUuid).build());
    List<AlertConfiguration> affectedConfigurations =
        alertConfigurations.stream()
            .filter(configuration -> configuration.getLabels() != null)
            .filter(
                configuration -> {
                  boolean affected = false;
                  for (Map.Entry<String, Set<String>> removedValueEntry :
                      removedValues.entrySet()) {
                    if (configuration.getLabels().containsKey(removedValueEntry.getKey())
                        && removedValueEntry
                            .getValue()
                            .contains(configuration.getLabels().get(removedValueEntry.getKey()))) {
                      affected = true;
                      configuration.getLabels().remove(removedValueEntry.getKey());
                    }
                  }
                  return affected;
                })
            .collect(Collectors.toList());
    alertConfigurationService.save(customerUuid, affectedConfigurations);
  }
}
