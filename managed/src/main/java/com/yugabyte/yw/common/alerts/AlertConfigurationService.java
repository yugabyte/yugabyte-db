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

import static com.yugabyte.yw.common.Util.doubleToString;
import static com.yugabyte.yw.models.AlertConfiguration.createQueryByFilter;
import static com.yugabyte.yw.models.helpers.CommonUtils.nowWithoutMillis;
import static com.yugabyte.yw.models.helpers.CommonUtils.performPagedQuery;
import static com.yugabyte.yw.models.helpers.EntityOperation.CREATE;
import static com.yugabyte.yw.models.helpers.EntityOperation.DELETE;
import static com.yugabyte.yw.models.helpers.EntityOperation.UPDATE;
import static io.ebean.DB.beginTransaction;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.cronutils.utils.StringUtils;
import com.google.common.collect.ImmutableSet;
import com.yugabyte.yw.common.AlertTemplate;
import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.alerts.impl.AlertTemplateService;
import com.yugabyte.yw.common.alerts.impl.AlertTemplateService.AlertTemplateDescription;
import com.yugabyte.yw.common.concurrent.MultiKeyLock;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.metrics.MetricLabelsBuilder;
import com.yugabyte.yw.models.AlertConfiguration;
import com.yugabyte.yw.models.AlertConfiguration.QuerySettings;
import com.yugabyte.yw.models.AlertConfiguration.SortBy;
import com.yugabyte.yw.models.AlertConfigurationTarget;
import com.yugabyte.yw.models.AlertConfigurationThreshold;
import com.yugabyte.yw.models.AlertDefinition;
import com.yugabyte.yw.models.AlertDestination;
import com.yugabyte.yw.models.AlertTemplateVariable;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.MaintenanceWindow;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.extended.AlertConfigurationTemplate;
import com.yugabyte.yw.models.filters.AlertConfigurationFilter;
import com.yugabyte.yw.models.filters.AlertDefinitionFilter;
import com.yugabyte.yw.models.filters.MaintenanceWindowFilter;
import com.yugabyte.yw.models.helpers.EntityOperation;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import com.yugabyte.yw.models.paging.AlertConfigurationPagedQuery;
import com.yugabyte.yw.models.paging.AlertConfigurationPagedResponse;
import com.yugabyte.yw.models.paging.PagedQuery.SortDirection;
import io.ebean.DB;
import io.ebean.Query;
import io.ebean.annotation.Transactional;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.UUID;
import java.util.function.Consumer;
import java.util.function.Function;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;

@Singleton
@Slf4j
public class AlertConfigurationService {

  private final BeanValidator beanValidator;
  private final AlertTemplateService alertTemplateService;
  private final AlertDefinitionService alertDefinitionService;
  private final MaintenanceService maintenanceService;
  private final RuntimeConfigFactory runtimeConfigFactory;
  private final MultiKeyLock<UUID> configUuidLock =
      new MultiKeyLock<>(Comparator.comparing(Function.<UUID>identity()));

  @Inject
  public AlertConfigurationService(
      BeanValidator beanValidator,
      AlertTemplateService alertTemplateService,
      AlertDefinitionService alertDefinitionService,
      MaintenanceService maintenanceService,
      RuntimeConfigFactory runtimeConfigFactory) {
    this.beanValidator = beanValidator;
    this.alertTemplateService = alertTemplateService;
    this.alertDefinitionService = alertDefinitionService;
    this.maintenanceService = maintenanceService;
    this.runtimeConfigFactory = runtimeConfigFactory;
  }

  public List<AlertConfiguration> save(UUID customerUuid, List<AlertConfiguration> configurations) {
    if (CollectionUtils.isEmpty(configurations)) {
      return configurations;
    }

    beginTransaction();
    try {
      List<AlertConfiguration> beforeConfigurations = Collections.emptyList();
      Set<UUID> configurationUuids =
          configurations.stream()
              .filter(configuration -> !configuration.isNew())
              .map(AlertConfiguration::getUuid)
              .collect(Collectors.toSet());
      if (!configurationUuids.isEmpty()) {
        AlertConfigurationFilter filter =
            AlertConfigurationFilter.builder().uuids(configurationUuids).build();
        beforeConfigurations = list(filter);
      }
      Map<UUID, AlertConfiguration> beforeConfigMap =
          beforeConfigurations.stream()
              .collect(Collectors.toMap(AlertConfiguration::getUuid, Function.identity()));

      Map<String, Set<String>> validLabels =
          AlertTemplateVariable.list(customerUuid).stream()
              .collect(
                  Collectors.toMap(
                      AlertTemplateVariable::getName, AlertTemplateVariable::getPossibleValues));
      Map<EntityOperation, List<AlertConfiguration>> toCreateAndUpdate =
          configurations.stream()
              .map(
                  configuration ->
                      prepareForSave(configuration, beforeConfigMap.get(configuration.getUuid())))
              .map(
                  configuration ->
                      validate(
                          configuration, beforeConfigMap.get(configuration.getUuid()), validLabels))
              .collect(
                  Collectors.groupingBy(configuration -> configuration.isNew() ? CREATE : UPDATE));

      List<AlertConfiguration> toCreate =
          toCreateAndUpdate.getOrDefault(CREATE, Collections.emptyList());
      toCreate.forEach(configuration -> configuration.setCreateTime(nowWithoutMillis()));
      toCreate.forEach(AlertConfiguration::generateUUID);

      List<AlertConfiguration> toUpdate =
          toCreateAndUpdate.getOrDefault(UPDATE, Collections.emptyList());

      Set<UUID> toUpdateUuids =
          toUpdate.stream().map(AlertConfiguration::getUuid).collect(Collectors.toSet());
      try {
        configUuidLock.acquireLocks(toUpdateUuids);
        if (!CollectionUtils.isEmpty(toCreate)) {
          DB.getDefault().saveAll(toCreate);
        }
        if (!CollectionUtils.isEmpty(toUpdate)) {
          DB.getDefault().updateAll(toUpdate);
        }

        manageDefinitions(configurations, beforeConfigurations);

        log.debug("{} alert configurations saved", configurations.size());
        DB.commitTransaction();
        return configurations;
      } finally {
        configUuidLock.releaseLocks(toUpdateUuids);
      }
    } finally {
      DB.endTransaction();
    }
  }

  @Transactional
  public AlertConfiguration save(AlertConfiguration configuration) {
    return save(configuration.getCustomerUUID(), Collections.singletonList(configuration)).get(0);
  }

  public AlertConfiguration get(UUID uuid) {
    if (uuid == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Can't get Alert Configuration by null uuid");
    }
    return list(AlertConfigurationFilter.builder().uuid(uuid).build()).stream()
        .findFirst()
        .orElse(null);
  }

  public AlertConfiguration getOrBadRequest(UUID uuid) {
    if (uuid == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Invalid Alert Configuration UUID: " + uuid);
    }
    AlertConfiguration configuration = get(uuid);
    if (configuration == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Invalid Alert Configuration UUID: " + uuid);
    }
    return configuration;
  }

  public AlertConfiguration getOrBadRequest(UUID customerUuid, UUID uuid) {
    AlertConfiguration configuration = getOrBadRequest(uuid);
    if (!(configuration.getCustomerUUID().equals(customerUuid))) {
      throw new PlatformServiceException(BAD_REQUEST, "Invalid Alert Configuration UUID: " + uuid);
    }
    return configuration;
  }

  public List<AlertConfiguration> list(AlertConfigurationFilter filter) {
    return createQueryByFilter(filter).findList();
  }

  public AlertConfigurationPagedResponse pagedList(AlertConfigurationPagedQuery pagedQuery) {
    if (pagedQuery.getSortBy() == null) {
      pagedQuery.setSortBy(SortBy.createTime);
      pagedQuery.setDirection(SortDirection.DESC);
    }
    QuerySettings settings =
        QuerySettings.builder()
            .queryTargetIndex(pagedQuery.getSortBy() == SortBy.target)
            .queryDestinationIndex(pagedQuery.getSortBy() == SortBy.destination)
            .queryCount(true)
            .build();
    Query<AlertConfiguration> query = createQueryByFilter(pagedQuery.getFilter(), settings).query();
    return performPagedQuery(query, pagedQuery, AlertConfigurationPagedResponse.class);
  }

  public List<UUID> listIds(AlertConfigurationFilter filter) {
    return createQueryByFilter(filter).findIds();
  }

  public void process(AlertConfigurationFilter filter, Consumer<AlertConfiguration> consumer) {
    createQueryByFilter(filter).findEach(consumer);
  }

  @Transactional
  public void delete(UUID uuid) {
    AlertConfigurationFilter filter = AlertConfigurationFilter.builder().uuid(uuid).build();
    delete(filter);
  }

  @Transactional
  public void delete(Collection<AlertConfiguration> configurations) {
    if (CollectionUtils.isEmpty(configurations)) {
      return;
    }
    AlertConfigurationFilter filter =
        AlertConfigurationFilter.builder()
            .uuids(
                configurations.stream()
                    .map(AlertConfiguration::getUuid)
                    .collect(Collectors.toSet()))
            .build();
    delete(filter);
  }

  public void delete(AlertConfigurationFilter filter) {
    List<AlertConfiguration> toDelete = list(filter);
    Set<UUID> toDeleteUuids =
        toDelete.stream().map(AlertConfiguration::getUuid).collect(Collectors.toSet());

    try {
      configUuidLock.acquireLocks(toDeleteUuids);
      manageDefinitions(Collections.emptyList(), toDelete);

      int deleted = createQueryByFilter(filter).delete();
      log.debug("{} alert definition configurations deleted", deleted);
    } finally {
      configUuidLock.releaseLocks(toDeleteUuids);
    }
  }

  private AlertConfiguration prepareForSave(
      AlertConfiguration configuration, AlertConfiguration before) {
    if (before != null) {
      configuration.setCreateTime(before.getCreateTime());
    } else {
      configuration.setCreateTime(nowWithoutMillis());
    }
    return configuration;
  }

  private AlertConfiguration validate(
      AlertConfiguration configuration,
      AlertConfiguration before,
      Map<String, Set<String>> validLabels) {
    beanValidator.validate(configuration);
    AlertConfigurationTarget target = configuration.getTarget();
    if (target.isAll() != CollectionUtils.isEmpty(target.getUuids())) {
      beanValidator
          .error()
          .forField("target", "should select either all entries or particular UUIDs")
          .throwError();
    }
    if (!CollectionUtils.isEmpty(target.getUuids())) {
      boolean hasNulls = target.getUuids().stream().anyMatch(Objects::isNull);
      if (hasNulls) {
        beanValidator.error().forField("target.uuids", "can't have null entries").throwError();
      }
      switch (configuration.getTargetType()) {
        case UNIVERSE:
          Set<UUID> existingUuids =
              Universe.getAllWithoutResources(
                      ImmutableSet.copyOf(configuration.getTarget().getUuids()))
                  .stream()
                  .map(Universe::getUniverseUUID)
                  .collect(Collectors.toSet());
          Set<UUID> missingUuids = new HashSet<>(configuration.getTarget().getUuids());
          missingUuids.removeAll(existingUuids);
          if (!missingUuids.isEmpty()) {
            beanValidator
                .error()
                .forField(
                    "target.uuids",
                    "universe(s) missing: "
                        + missingUuids.stream()
                            .map(UUID::toString)
                            .collect(Collectors.joining(", ")))
                .throwError();
          }
          break;
        default:
          beanValidator
              .error()
              .forField(
                  "target.uuids",
                  configuration.getTargetType().name() + " configuration can't have target uuids")
              .throwError();
      }
    }
    AlertTemplateDescription templateDescription =
        alertTemplateService.getTemplateDescription(configuration.getTemplate());
    if (templateDescription.getTargetType() != configuration.getTargetType()) {
      beanValidator.error().global("target type should be consistent with template").throwError();
    }
    if (configuration.getDestinationUUID() != null) {
      if (AlertDestination.get(configuration.getCustomerUUID(), configuration.getDestinationUUID())
          == null) {
        beanValidator
            .error()
            .forField(
                "destinationUUID",
                "alert destination " + configuration.getDestinationUUID() + " is missing")
            .throwError();
      }
      if (configuration.isDefaultDestination()) {
        beanValidator
            .error()
            .forField("", "destination can't be filled in case default destination is selected")
            .throwError();
      }
    }
    if (configuration.getThresholdUnit() != templateDescription.getDefaultThresholdUnit()) {
      beanValidator
          .error()
          .forField("thresholdUnit", "incompatible with alert definition template")
          .throwError();
    }
    configuration
        .getThresholds()
        .forEach(
            (severity, threshold) -> {
              if (threshold.getThreshold() < templateDescription.getThresholdMinValue()) {
                beanValidator
                    .error()
                    .forField(
                        "thresholds[" + severity.name() + "].threshold",
                        "can't be less than "
                            + doubleToString(templateDescription.getThresholdMinValue()))
                    .throwError();
              }
              if (threshold.getThreshold() > templateDescription.getThresholdMaxValue()) {
                beanValidator
                    .error()
                    .forField(
                        "thresholds[" + severity.name() + "].threshold",
                        "can't be greater than "
                            + doubleToString(templateDescription.getThresholdMaxValue()))
                    .throwError();
              }
            });
    if (before != null) {
      if (!configuration.getCustomerUUID().equals(before.getCustomerUUID())) {
        beanValidator
            .error()
            .forField(
                "customerUUID", "can't change for configuration '" + configuration.getName() + "'")
            .throwError();
      }
      if (!configuration.getTargetType().equals(before.getTargetType())) {
        beanValidator
            .error()
            .forField(
                "targetType", "can't change for configuration '" + configuration.getName() + "'")
            .throwError();
      }
      if (!configuration.getCreateTime().equals(before.getCreateTime())) {
        beanValidator
            .error()
            .forField(
                "createTime", "can't change for configuration '" + configuration.getName() + "'")
            .throwError();
      }
    } else if (!configuration.isNew()) {
      beanValidator
          .error()
          .forField("", "can't update missing configuration '" + configuration.getName() + "'")
          .throwError();
    }
    if (configuration.getLabels() != null) {
      configuration
          .getLabels()
          .forEach(
              (name, value) -> {
                if (!validLabels.containsKey(name)) {
                  beanValidator
                      .error()
                      .forField("labels", "variable '" + name + "' does not exist")
                      .throwError();
                }
                if (!validLabels.get(name).contains(value)) {
                  beanValidator
                      .error()
                      .forField(
                          "labels", "variable '" + name + "' does not have value '" + value + "'")
                      .throwError();
                }
              });
    }
    return configuration;
  }

  @Transactional
  public void handleSourceRemoval(
      UUID customerUuid, AlertConfiguration.TargetType configType, UUID targetUuid) {
    AlertConfigurationFilter filter =
        AlertConfigurationFilter.builder()
            .customerUuid(customerUuid)
            .targetType(configType)
            .build();

    List<AlertConfiguration> configurations =
        list(filter).stream()
            .filter(
                configuration ->
                    configuration.getTarget().isAll()
                        || configuration.getTarget().getUuids().remove(targetUuid))
            .collect(Collectors.toList());

    Map<EntityOperation, List<AlertConfiguration>> toUpdateAndDelete =
        configurations.stream()
            .collect(
                Collectors.groupingBy(
                    configuration ->
                        configuration.getTarget().isAll()
                                || !configuration.getTarget().getUuids().isEmpty()
                            ? UPDATE
                            : DELETE));

    // Just need to save - service will delete definition itself.
    save(customerUuid, toUpdateAndDelete.get(UPDATE));
    delete(toUpdateAndDelete.get(DELETE));
  }

  /**
   * The purpose of this method is to manage alert definitions, related to modified definition alert
   * configurations. The main idea is to read existing definitions, remove the ones, which are not
   * needed anymore, and create new ones (universe is added or target list changed) and update
   * existing ones (in case group itself has changed).
   *
   * @param configurations List of new/updated configurations
   * @param beforeList List of configurations before update
   */
  private void manageDefinitions(
      List<AlertConfiguration> configurations, List<AlertConfiguration> beforeList) {
    // Make sure we process both new, updated and deleted configurations.
    Set<UUID> configurationUUIDs =
        Stream.concat(configurations.stream(), beforeList.stream())
            .map(AlertConfiguration::getUuid)
            .collect(Collectors.toSet());

    if (configurationUUIDs.isEmpty()) {
      return;
    }

    // First read existing alert definitions for all the affected configurations.
    AlertDefinitionFilter filter =
        AlertDefinitionFilter.builder().configurationUuids(configurationUUIDs).build();
    Map<UUID, List<AlertDefinition>> definitionsByConfiguration =
        alertDefinitionService.list(filter).stream()
            .collect(
                Collectors.groupingBy(AlertDefinition::getConfigurationUUID, Collectors.toList()));

    // Read existing maintenance windows, associated with saved configurations.
    Set<UUID> maintenanceWindowUuids =
        configurations.stream()
            .flatMap(configuration -> configuration.getMaintenanceWindowUuidsSet().stream())
            .filter(Objects::nonNull)
            .collect(Collectors.toSet());
    MaintenanceWindowFilter maintenanceWindowFilter =
        MaintenanceWindowFilter.builder().uuids(maintenanceWindowUuids).build();
    Map<UUID, MaintenanceWindow> maintenanceWindowMap =
        maintenanceService.list(maintenanceWindowFilter).stream()
            .collect(Collectors.toMap(MaintenanceWindow::getUuid, Function.identity()));

    List<AlertDefinition> toSave = new ArrayList<>();
    List<AlertDefinition> toRemove = new ArrayList<>();

    Map<UUID, AlertConfiguration> configurationsMap =
        configurations.stream()
            .collect(Collectors.toMap(AlertConfiguration::getUuid, Function.identity()));
    Map<UUID, AlertConfiguration> beforeMap =
        beforeList.stream()
            .collect(Collectors.toMap(AlertConfiguration::getUuid, Function.identity()));

    for (UUID uuid : configurationUUIDs) {
      AlertConfiguration configuration = configurationsMap.get(uuid);
      AlertConfiguration before = beforeMap.get(uuid);

      // List of existing definitions for particular configuration.
      List<AlertDefinition> currentDefinitions =
          definitionsByConfiguration.getOrDefault(uuid, Collections.emptyList());
      if (configuration == null) {
        // If configuration was deleted - remove all the associated definitions.
        toRemove.addAll(currentDefinitions);
      } else {
        AlertTemplateDescription templateDescription =
            alertTemplateService.getTemplateDescription(configuration.getTemplate());
        boolean configurationChanged = before != null && !before.equals(configuration);
        Customer customer = Customer.getOrBadRequest(configuration.getCustomerUUID());
        AlertConfigurationTarget target = configuration.getTarget();
        switch (configuration.getTargetType()) {
          case PLATFORM:
            // For platform level configurations we always have only one definition
            // - linked to customer
            if (currentDefinitions.size() > 1) {
              throw new IllegalStateException(
                  "More than one definition for CUSTOMER alert definition configuration " + uuid);
            }
            AlertDefinition definition;
            if (currentDefinitions.isEmpty()) {
              // If it's missing - we need to create one. Probably config is just created.
              definition = createEmptyDefinition(configuration);
            } else {
              // If it exists - we need to update existing one just in case group is updated.
              definition = currentDefinitions.get(0);
            }
            definition.setConfigWritten(false);
            if (!templateDescription.isSkipSourceLabels()) {
              definition.setLabels(
                  MetricLabelsBuilder.create()
                      .appendCustomer(customer)
                      .appendSource(customer)
                      .getDefinitionLabels());
            }
            if (!configuration.getMaintenanceWindowUuidsSet().isEmpty()) {
              definition.setLabel(
                  KnownAlertLabels.MAINTENANCE_WINDOW_UUIDS,
                  maintenanceWindowUuidsString(configuration.getMaintenanceWindowUuidsSet()));
            } else {
              definition.removeLabel(KnownAlertLabels.MAINTENANCE_WINDOW_UUIDS);
            }
            toSave.add(definition);
            break;
          case UNIVERSE:
            // For universe level configurations we have a definition per universe.
            Set<UUID> universeUUIDs;
            Set<Universe> universes;

            if (target.isAll()) {
              // Get all universes + concat universe UUIDs from existing definitions for
              // the universes which were deleted.
              universes = Universe.getAllWithoutResources(customer);
              universeUUIDs =
                  Stream.concat(
                          currentDefinitions.stream().map(AlertDefinition::getUniverseUUID),
                          universes.stream().map(Universe::getUniverseUUID))
                      .collect(Collectors.toSet());
            } else {
              // Get target universes + universes from existing definitions for
              // the cases, when universe is not a target for this configuration anymore.
              universeUUIDs =
                  Stream.concat(
                          currentDefinitions.stream().map(AlertDefinition::getUniverseUUID),
                          target.getUuids().stream())
                      .collect(Collectors.toSet());
              universes = Universe.getAllWithoutResources(universeUUIDs);
            }
            Map<UUID, Universe> universeMap =
                universes.stream()
                    .collect(Collectors.toMap(Universe::getUniverseUUID, Function.identity()));
            Map<UUID, List<AlertDefinition>> definitionsByUniverseUuid =
                currentDefinitions.stream()
                    .collect(Collectors.groupingBy(AlertDefinition::getUniverseUUID));
            for (UUID universeUuid : universeUUIDs) {
              Universe universe = universeMap.get(universeUuid);
              List<AlertDefinition> universeDefinitions =
                  definitionsByUniverseUuid.get(universeUuid);
              // In case universe still exists and is in our target - we need to have definition.
              boolean shouldHaveDefinition =
                  (target.isAll() || target.getUuids().contains(universeUuid)) && universe != null;
              AlertDefinition universeDefinition;
              if (shouldHaveDefinition) {
                if (CollectionUtils.isEmpty(universeDefinitions)) {
                  // Either new universe is created or it's just added to the configuration target.
                  universeDefinition = createEmptyDefinition(configuration);
                } else {
                  universeDefinition = universeDefinitions.get(0);
                  if (universeDefinitions.size() > 1) {
                    log.warn(
                        "Have more than one definition for configuration {} universe {}",
                        uuid,
                        universeUuid);
                    toRemove.addAll(universeDefinitions.subList(1, universeDefinitions.size()));
                  }
                  if (!configurationChanged
                      && configuration.getMaintenanceWindowUuidsSet().isEmpty()) {
                    // Universe had definition before the update and group is not changed.
                    // We want to avoid updating definitions unnecessarily.
                    // Also configuration should not be under maintenance window - because
                    // maintenance window targets may be changed before config is saved.
                    continue;
                  }
                }
                universeDefinition.setConfigWritten(false);
                if (!templateDescription.isSkipSourceLabels()) {
                  universeDefinition.setLabels(
                      MetricLabelsBuilder.create()
                          .appendCustomer(customer)
                          .appendSource(universe)
                          .getDefinitionLabels());
                }
                Set<UUID> appliedMaintenanceWindows = new HashSet<>();
                if (!configuration.getMaintenanceWindowUuidsSet().isEmpty()) {
                  List<MaintenanceWindow> activeWindows =
                      configuration.getMaintenanceWindowUuidsSet().stream()
                          .map(maintenanceWindowMap::get)
                          .filter(Objects::nonNull)
                          .collect(Collectors.toList());
                  appliedMaintenanceWindows = filterMaintenanceWindows(activeWindows, universeUuid);
                }
                if (CollectionUtils.isNotEmpty(appliedMaintenanceWindows)) {
                  universeDefinition.setLabel(
                      KnownAlertLabels.MAINTENANCE_WINDOW_UUIDS,
                      maintenanceWindowUuidsString(appliedMaintenanceWindows));
                } else {
                  universeDefinition.removeLabel(KnownAlertLabels.MAINTENANCE_WINDOW_UUIDS);
                }
                toSave.add(universeDefinition);
              } else if (!CollectionUtils.isEmpty(universeDefinitions)) {
                // Remove existing definition if it's not needed.
                toRemove.addAll(universeDefinitions);
              }
            }
            break;
          default:
            throw new IllegalStateException(
                "Unexpected target type " + configuration.getTargetType());
        }
      }
    }

    if (!toSave.isEmpty()) {
      alertDefinitionService.save(toSave);
    }
    if (!toRemove.isEmpty()) {
      Set<UUID> uuids = toRemove.stream().map(AlertDefinition::getUuid).collect(Collectors.toSet());
      alertDefinitionService.delete(AlertDefinitionFilter.builder().uuids(uuids).build());
    }
  }

  private Set<UUID> filterMaintenanceWindows(List<MaintenanceWindow> windows, UUID targetUuid) {
    return windows.stream()
        .filter(
            window -> {
              AlertConfigurationTarget target = window.getAlertConfigurationFilter().getTarget();
              if (target == null) {
                // Means any target matches
                return true;
              }
              if (!CollectionUtils.isEmpty(target.getUuids())) {
                // Filtering by target UUID
                return target.getUuids().contains(targetUuid);
              } else {
                return target.isAll();
              }
            })
        .map(MaintenanceWindow::getUuid)
        .collect(Collectors.toSet());
  }

  public AlertConfigurationTemplate createConfigurationTemplate(
      Customer customer, AlertTemplate template) {
    AlertTemplateDescription templateDescription =
        alertTemplateService.getTemplateDescription(template);
    AlertConfiguration configuration =
        new AlertConfiguration()
            .setCustomerUUID(customer.getUuid())
            .setName(templateDescription.getName())
            .setDescription(templateDescription.getDescription())
            .setTargetType(templateDescription.getTargetType())
            .setTarget(new AlertConfigurationTarget().setAll(true))
            .setThresholds(
                templateDescription.getDefaultThresholdMap().entrySet().stream()
                    .collect(
                        Collectors.toMap(
                            Map.Entry::getKey,
                            e ->
                                new AlertConfigurationThreshold()
                                    .setCondition(
                                        templateDescription.getDefaultThresholdCondition())
                                    .setThreshold(
                                        !StringUtils.isEmpty(e.getValue().getParamName())
                                            ? runtimeConfigFactory
                                                .globalRuntimeConf()
                                                .getDouble(e.getValue().getParamName())
                                            : e.getValue().getThreshold()))))
            .setThresholdUnit(templateDescription.getDefaultThresholdUnit())
            .setTemplate(template)
            .setDurationSec(templateDescription.getDefaultDurationSec())
            .setDefaultDestination(true);
    return new AlertConfigurationTemplate()
        .setDefaultConfiguration(configuration)
        .setThresholdMinValue(templateDescription.getThresholdMinValue())
        .setThresholdMaxValue(templateDescription.getThresholdMaxValue())
        .setThresholdInteger(templateDescription.getDefaultThresholdUnit().isInteger())
        .setThresholdReadOnly(templateDescription.isThresholdReadOnly())
        .setThresholdConditionReadOnly(templateDescription.isThresholdConditionReadOnly())
        .setThresholdUnitName(templateDescription.getThresholdUnitName());
  }

  public void createDefaultConfigs(Customer customer) {
    List<AlertConfiguration> alertConfigurations =
        Arrays.stream(AlertTemplate.values())
            .filter(
                template ->
                    alertTemplateService.getTemplateDescription(template).isCreateForNewCustomer())
            .map(template -> createConfigurationTemplate(customer, template))
            .map(AlertConfigurationTemplate::getDefaultConfiguration)
            .collect(Collectors.toList());
    save(customer.getUuid(), alertConfigurations);
  }

  private AlertDefinition createEmptyDefinition(AlertConfiguration configuration) {
    return new AlertDefinition()
        .setCustomerUUID(configuration.getCustomerUUID())
        .setConfigurationUUID(configuration.getUuid());
  }

  private String maintenanceWindowUuidsString(Collection<UUID> uuids) {
    return uuids.stream().map(UUID::toString).sorted().collect(Collectors.joining(","));
  }
}
