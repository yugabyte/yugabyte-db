/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common.customer.config;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util.UniverseDetailSubset;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.CustomerConfig;
import com.yugabyte.yw.models.CustomerConfig.ConfigType;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CustomerConfigValidator;
import java.util.Collection;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.UUID;
import java.util.function.Function;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import javax.inject.Inject;
import javax.inject.Singleton;
import org.apache.commons.collections4.CollectionUtils;

@Singleton
public class CustomerConfigService {

  private final CustomerConfigValidator configValidator;

  @Inject
  public CustomerConfigService(CustomerConfigValidator configValidator) {
    this.configValidator = configValidator;
  }

  public CustomerConfig getOrBadRequest(UUID customerUUID, UUID configUUID) {
    CustomerConfig config = CustomerConfig.get(customerUUID, configUUID);
    if (config == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Invalid StorageConfig UUID: " + configUUID);
    }
    return config;
  }

  public List<CustomerConfigUI> listForUI(UUID customerUUID) {
    List<CustomerConfig> rawConfigs = CustomerConfig.getAll(customerUUID);

    return enrichConfigsForUI(customerUUID, rawConfigs);
  }

  public void create(CustomerConfig customerConfig) {
    configValidator.validateConfig(customerConfig);
    customerConfig.generateUUID();
    customerConfig.save();
  }

  public void edit(CustomerConfig customerConfig) {
    configValidator.validateConfig(customerConfig);
    customerConfig.update();
  }

  public void delete(UUID customerUUID, UUID configUUID) {
    CustomerConfig customerConfig = getOrBadRequest(customerUUID, configUUID);
    configValidator.validateConfigRemoval(customerConfig);
    customerConfig.delete();
  }

  private List<CustomerConfigUI> enrichConfigsForUI(
      UUID customerUUID, List<CustomerConfig> rawConfigs) {
    List<CustomerConfigUI> configs =
        rawConfigs
            .stream()
            .map(config -> config.setData(config.getMaskedData()))
            .map(config -> new CustomerConfigUI().setCustomerConfig(config))
            .collect(Collectors.toList());

    List<CustomerConfigUI> storageConfigs =
        configs
            .stream()
            .filter(config -> config.getCustomerConfig().getType() == ConfigType.STORAGE)
            .collect(Collectors.toList());
    Set<UUID> storageConfigUuids =
        storageConfigs
            .stream()
            .map(CustomerConfigUI::getCustomerConfig)
            .map(CustomerConfig::getConfigUUID)
            .collect(Collectors.toSet());

    Map<UUID, List<Backup>> backupsByConfigUuid =
        Backup.getInProgressAndCompleted(customerUUID)
            .stream()
            .filter(backup -> storageConfigUuids.contains(getStorageConfigUuid(backup)))
            .collect(Collectors.groupingBy(this::getStorageConfigUuid, Collectors.toList()));

    Map<UUID, List<Schedule>> schedulesByConfigUuid =
        Schedule.getActiveBackupSchedules(customerUUID)
            .stream()
            .filter(schedule -> storageConfigUuids.contains(getStorageConfigUuid(schedule)))
            .collect(Collectors.groupingBy(this::getStorageConfigUuid, Collectors.toList()));

    Set<UUID> universeUuids =
        Stream.concat(
                backupsByConfigUuid
                    .values()
                    .stream()
                    .flatMap(Collection::stream)
                    .map(this::getUniverseUuid),
                schedulesByConfigUuid
                    .values()
                    .stream()
                    .flatMap(Collection::stream)
                    .map(this::getUniverseUuid))
            .collect(Collectors.toSet());

    Map<UUID, UniverseDetailSubset> universeMap =
        Universe.getAllWithoutResources(universeUuids)
            .stream()
            .map(UniverseDetailSubset::new)
            .collect(Collectors.toMap(UniverseDetailSubset::getUuid, Function.identity()));

    for (CustomerConfigUI configUI : storageConfigs) {
      UUID storageUUID = configUI.getCustomerConfig().getConfigUUID();

      Set<UUID> storageUniverseUuids =
          Stream.concat(
                  backupsByConfigUuid
                      .getOrDefault(storageUUID, Collections.emptyList())
                      .stream()
                      .map(this::getUniverseUuid),
                  schedulesByConfigUuid
                      .getOrDefault(storageUUID, Collections.emptyList())
                      .stream()
                      .map(this::getUniverseUuid))
              .collect(Collectors.toSet());
      configUI.setUniverseDetails(
          storageUniverseUuids
              .stream()
              .map(universeMap::get)
              .filter(Objects::nonNull)
              .collect(Collectors.toList()));

      // In case we have backup or schedule for missing universe - count as not used.
      configUI.setInUse(CollectionUtils.isNotEmpty(configUI.getUniverseDetails()));
    }
    return configs;
  }

  private UUID getStorageConfigUuid(Backup backup) {
    return backup.getBackupInfo().storageConfigUUID;
  }

  private UUID getStorageConfigUuid(Schedule schedule) {
    return UUID.fromString(schedule.getTaskParams().path("storageConfigUUID").asText());
  }

  private UUID getUniverseUuid(Backup backup) {
    return backup.getBackupInfo().universeUUID;
  }

  private UUID getUniverseUuid(Schedule schedule) {
    return UUID.fromString(schedule.getTaskParams().get("universeUUID").asText());
  }
}
