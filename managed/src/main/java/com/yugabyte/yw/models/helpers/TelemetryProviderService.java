/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.models.helpers;

import static com.yugabyte.yw.models.helpers.CommonUtils.appendInClause;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.google.common.annotations.VisibleForTesting;
import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.TelemetryProvider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.audit.UniverseLogsExporterConfig;
import io.ebean.annotation.Transactional;
import java.util.Collection;
import java.util.Collections;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;

@Singleton
@Slf4j
public class TelemetryProviderService {

  private final BeanValidator beanValidator;
  private final RuntimeConfGetter confGetter;

  @Inject
  public TelemetryProviderService(BeanValidator beanValidator, RuntimeConfGetter confGetter) {
    this.beanValidator = beanValidator;
    this.confGetter = confGetter;
  }

  @VisibleForTesting
  public TelemetryProviderService() {
    this(new BeanValidator(null), null);
  }

  @Transactional
  public TelemetryProvider save(TelemetryProvider provider) {
    if (provider.getUuid() == null) {
      provider.generateUUID();
    }

    validate(provider);

    provider.save();
    return provider;
  }

  public TelemetryProvider get(UUID uuid) {
    if (uuid == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Can't get Telemetry Provider by null uuid");
    }
    return list(Collections.singleton(uuid)).stream().findFirst().orElse(null);
  }

  private TelemetryProvider get(UUID customerUUID, String providerName) {
    return TelemetryProvider.createQuery()
        .eq("customerUUID", customerUUID)
        .eq("name", providerName)
        .findOne();
  }

  public TelemetryProvider getOrBadRequest(UUID uuid) {
    TelemetryProvider variable = get(uuid);
    if (variable == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Invalid Telemetry Provider UUID: " + uuid);
    }
    return variable;
  }

  public TelemetryProvider getOrBadRequest(UUID customerUUID, UUID uuid) {
    TelemetryProvider provider = getOrBadRequest(uuid);
    if (!(provider.getCustomerUUID().equals(customerUUID))) {
      throw new PlatformServiceException(BAD_REQUEST, "Invalid Telemetry Provider UUID: " + uuid);
    }
    return provider;
  }

  public boolean checkIfExists(UUID customerUUID, UUID uuid) {
    try {
      TelemetryProvider provider = getOrBadRequest(customerUUID, uuid);
      if (provider != null) {
        return true;
      }
    } catch (Exception e) {
      return false;
    }
    return false;
  }

  public List<TelemetryProvider> list(Set<UUID> uuids) {
    return appendInClause(TelemetryProvider.createQuery(), "uuid", uuids).findList();
  }

  public List<TelemetryProvider> list(UUID customerUUID, Set<String> names) {
    return appendInClause(TelemetryProvider.createQuery(), "name", names)
        .eq("customerUUID", customerUUID)
        .findList();
  }

  public List<TelemetryProvider> list(UUID customerUUID) {
    return TelemetryProvider.list(customerUUID);
  }

  @Transactional
  public void delete(UUID uuid) {
    TelemetryProvider provider = getOrBadRequest(uuid);
    delete(provider.getCustomerUUID(), Collections.singleton(provider));
  }

  @Transactional
  public void delete(UUID customerUUID, Collection<TelemetryProvider> providers) {
    if (CollectionUtils.isEmpty(providers)) {
      return;
    }
    Set<UUID> uuidsToDelete =
        providers.stream().map(TelemetryProvider::getUuid).collect(Collectors.toSet());

    appendInClause(TelemetryProvider.createQuery(), "uuid", uuidsToDelete).delete();
  }

  public void validateBean(TelemetryProvider provider) {
    beanValidator.validate(provider);
  }

  public void validate(TelemetryProvider provider) {
    validateBean(provider);

    TelemetryProvider providerWithSameName = get(provider.getCustomerUUID(), provider.getName());
    if ((providerWithSameName != null)
        && !provider.getUuid().equals(providerWithSameName.getUuid())) {
      beanValidator
          .error()
          .forField("name", "provider with such name already exists.")
          .throwError();
    }
  }

  public void throwExceptionIfRuntimeFlagDisabled() {
    boolean isDBAuditLoggingEnabled =
        confGetter.getGlobalConf(GlobalConfKeys.dbAuditLoggingEnabled);
    if (!isDBAuditLoggingEnabled) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "DB Audit Logging is not enabled. Please set runtime flag"
              + " 'yb.universe.audit_logging_enabled' to true.");
    }
  }

  public boolean isProviderInUse(Customer customer, UUID providerUUID) {
    Set<Universe> allUniverses = Universe.getAllWithoutResources(customer);

    // Iterate through all universe details and check if any of them have an audit log config.
    for (Universe universe : allUniverses) {
      UserIntent primaryUserIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;

      if (primaryUserIntent.getAuditLogConfig() != null
          && primaryUserIntent.getAuditLogConfig().getUniverseLogsExporterConfig() != null) {
        List<UniverseLogsExporterConfig> universeLogsExporterConfigs =
            primaryUserIntent.getAuditLogConfig().getUniverseLogsExporterConfig();

        // Check if the provider is in the list of export configs in the audit log config.
        for (UniverseLogsExporterConfig config : universeLogsExporterConfigs) {
          if (config != null && providerUUID.equals(config.getExporterUuid())) {
            return true;
          }
        }
      }
    }
    return false;
  }

  public void validateTelemetryProvider(TelemetryProvider provider) {
    provider.getConfig().validate();
  }
}
