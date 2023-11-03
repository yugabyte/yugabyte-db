/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common.alerts;

import static com.yugabyte.yw.models.AlertTemplateSettings.createQueryByFilter;
import static com.yugabyte.yw.models.helpers.CommonUtils.nowWithoutMillis;
import static com.yugabyte.yw.models.helpers.EntityOperation.CREATE;
import static com.yugabyte.yw.models.helpers.EntityOperation.UPDATE;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.common.AlertTemplate;
import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.AlertConfiguration;
import com.yugabyte.yw.models.AlertDefinition;
import com.yugabyte.yw.models.AlertTemplateSettings;
import com.yugabyte.yw.models.filters.AlertConfigurationFilter;
import com.yugabyte.yw.models.filters.AlertDefinitionFilter;
import com.yugabyte.yw.models.filters.AlertTemplateSettingsFilter;
import com.yugabyte.yw.models.helpers.EntityOperation;
import io.ebean.DB;
import io.ebean.annotation.Transactional;
import java.util.Collections;
import java.util.List;
import java.util.Map;
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
public class AlertTemplateSettingsService {

  private final BeanValidator beanValidator;
  private final AlertConfigurationService alertConfigurationService;
  private final AlertDefinitionService alertDefinitionService;

  @Inject
  public AlertTemplateSettingsService(
      BeanValidator beanValidator,
      AlertConfigurationService alertConfigurationService,
      AlertDefinitionService alertDefinitionService) {
    this.beanValidator = beanValidator;
    this.alertConfigurationService = alertConfigurationService;
    this.alertDefinitionService = alertDefinitionService;
  }

  @Transactional
  public List<AlertTemplateSettings> save(UUID customerUUID, List<AlertTemplateSettings> settings) {
    if (CollectionUtils.isEmpty(settings)) {
      return settings;
    }

    settings.forEach(s -> s.setCustomerUUID(customerUUID));
    List<AlertTemplateSettings> before = Collections.emptyList();
    Set<UUID> existingUuids =
        settings.stream()
            .filter(configuration -> !configuration.isNew())
            .map(AlertTemplateSettings::getUuid)
            .collect(Collectors.toSet());
    if (!existingUuids.isEmpty()) {
      AlertTemplateSettingsFilter filter =
          AlertTemplateSettingsFilter.builder().uuids(existingUuids).build();
      before = list(filter);
    }
    Map<UUID, AlertTemplateSettings> beforeMap =
        before.stream()
            .collect(Collectors.toMap(AlertTemplateSettings::getUuid, Function.identity()));

    Map<EntityOperation, List<AlertTemplateSettings>> toCreateAndUpdate =
        settings.stream()
            .peek(s -> prepareForSave(s, beforeMap.get(s.getUuid())))
            .peek(s -> validate(s, beforeMap.get(s.getUuid())))
            .collect(
                Collectors.groupingBy(configuration -> configuration.isNew() ? CREATE : UPDATE));

    List<AlertTemplateSettings> toCreate =
        toCreateAndUpdate.getOrDefault(CREATE, Collections.emptyList());
    toCreate.forEach(AlertTemplateSettings::generateUUID);

    List<AlertTemplateSettings> toUpdate =
        toCreateAndUpdate.getOrDefault(UPDATE, Collections.emptyList());

    if (!CollectionUtils.isEmpty(toCreate)) {
      DB.getDefault().saveAll(toCreate);
    }
    if (!CollectionUtils.isEmpty(toUpdate)) {
      DB.getDefault().updateAll(toUpdate);
    }

    writeDefinitions(customerUUID, settings);

    log.debug("{} alert template settings saved", settings.size());
    return settings;
  }

  @Transactional
  public AlertTemplateSettings save(AlertTemplateSettings settings) {
    return save(settings.getCustomerUUID(), Collections.singletonList(settings)).get(0);
  }

  public AlertTemplateSettings get(UUID uuid) {
    if (uuid == null) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Can't get Alert Template Settings by null uuid");
    }
    return list(AlertTemplateSettingsFilter.builder().uuid(uuid).build()).stream()
        .findFirst()
        .orElse(null);
  }

  public AlertTemplateSettings get(UUID customerUUID, String template) {
    return list(
            AlertTemplateSettingsFilter.builder()
                .customerUuid(customerUUID)
                .template(template)
                .build())
        .stream()
        .findFirst()
        .orElse(null);
  }

  public AlertTemplateSettings getOrBadRequest(UUID uuid) {
    if (uuid == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Invalid Template Settings UUID: " + uuid);
    }
    AlertTemplateSettings settings = get(uuid);
    if (settings == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Invalid Template Settings UUID: " + uuid);
    }
    return settings;
  }

  public AlertTemplateSettings getOrBadRequest(UUID customerUuid, UUID uuid) {
    AlertTemplateSettings settings = getOrBadRequest(uuid);
    if (!(settings.getCustomerUUID().equals(customerUuid))) {
      throw new PlatformServiceException(BAD_REQUEST, "Invalid Template Settings UUID: " + uuid);
    }
    return settings;
  }

  public List<AlertTemplateSettings> list(AlertTemplateSettingsFilter filter) {
    return createQueryByFilter(filter).findList();
  }

  @Transactional
  public void delete(UUID uuid) {
    AlertTemplateSettingsFilter filter = AlertTemplateSettingsFilter.builder().uuid(uuid).build();
    delete(filter);
  }

  @Transactional
  public void delete(AlertTemplateSettingsFilter filter) {
    List<AlertTemplateSettings> toDelete = list(filter);
    Map<UUID, List<AlertTemplateSettings>> toDeleteByCustomer =
        toDelete.stream().collect(Collectors.groupingBy(AlertTemplateSettings::getCustomerUUID));

    AlertTemplateSettingsFilter uuidsFilter =
        AlertTemplateSettingsFilter.builder()
            .uuids(
                toDelete.stream().map(AlertTemplateSettings::getUuid).collect(Collectors.toSet()))
            .build();
    int deleted = createQueryByFilter(uuidsFilter).delete();

    toDeleteByCustomer.forEach(this::writeDefinitions);
    log.debug("{} alert template settings deleted", deleted);
  }

  private void prepareForSave(AlertTemplateSettings after, AlertTemplateSettings before) {
    if (before != null) {
      after.setCreateTime(before.getCreateTime());
    } else {
      after.setCreateTime(nowWithoutMillis());
    }
  }

  private void validate(AlertTemplateSettings after, AlertTemplateSettings before) {
    beanValidator.validate(after);
    try {
      AlertTemplate.valueOf(after.getTemplate());
    } catch (IllegalArgumentException illegalArgumentException) {
      beanValidator
          .error()
          .forField("template", "Template '" + after.getTemplate() + "' is missing")
          .throwError();
    }
    if (before != null) {
      if (!after.getCreateTime().equals(before.getCreateTime())) {
        beanValidator
            .error()
            .forField("createTime", "can't change for '" + after.getTemplate() + "' settings")
            .throwError();
      }
    } else if (!after.isNew()) {
      beanValidator
          .error()
          .forField("", "can't update missing '" + after.getTemplate() + "' settings")
          .throwError();
    }
  }

  private void writeDefinitions(UUID customerUUID, List<AlertTemplateSettings> settings) {
    Set<AlertTemplate> templates =
        settings.stream()
            .map(AlertTemplateSettings::getTemplate)
            .map(AlertTemplate::valueOf)
            .collect(Collectors.toSet());
    AlertConfigurationFilter filter =
        AlertConfigurationFilter.builder().customerUuid(customerUUID).templates(templates).build();
    List<AlertConfiguration> affectedConfigurations = alertConfigurationService.list(filter);

    if (CollectionUtils.isEmpty(affectedConfigurations)) {
      return;
    }

    AlertDefinitionFilter alertDefinitionFilter =
        AlertDefinitionFilter.builder()
            .configurationUuids(
                affectedConfigurations.stream()
                    .map(AlertConfiguration::getUuid)
                    .collect(Collectors.toSet()))
            .build();

    List<AlertDefinition> alertDefinitions = alertDefinitionService.list(alertDefinitionFilter);
    if (CollectionUtils.isEmpty(alertDefinitions)) {
      return;
    }

    alertDefinitions.forEach(alertDefinition -> alertDefinition.setConfigWritten(false));
    alertDefinitionService.save(alertDefinitions);
  }
}
