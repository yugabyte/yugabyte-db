/*
 * Copyright 2022 YugabyteDB, Inc. and Contributors
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
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.WSClientRefresher;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.TelemetryProvider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.exporters.audit.UniverseLogsExporterConfig;
import com.yugabyte.yw.models.helpers.exporters.metrics.UniverseMetricsExporterConfig;
import com.yugabyte.yw.models.helpers.exporters.query.UniverseQueryLogsExporterConfig;
import com.yugabyte.yw.models.helpers.telemetry.ProviderType;
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

  public static final String LOKI_PUSH_ENDPOINT = "/loki/api/v1/push";
  public static final String WS_CLIENT_KEY = "yb.ws";
  private final BeanValidator beanValidator;
  private final RuntimeConfGetter confGetter;
  private WSClientRefresher wsClientRefresher;
  private ApiHelper apiHelper;

  @Inject
  public TelemetryProviderService(
      BeanValidator beanValidator,
      RuntimeConfGetter confGetter,
      WSClientRefresher wsClientRefresher) {
    this.beanValidator = beanValidator;
    this.confGetter = confGetter;
    this.wsClientRefresher = wsClientRefresher;
  }

  @VisibleForTesting
  public TelemetryProviderService() {
    this(new BeanValidator(null), null, null);
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
    boolean isDBAuditLoggingEnabled = isDBAuditLoggingRuntimeFlagEnabled();
    boolean isQueryLoggingEnabled = isQueryLoggingRuntimeFlagEnabled();
    boolean isMetricsExportEnabled = isMetricsExportRuntimeFlagEnabled();
    if (!isDBAuditLoggingEnabled && !isQueryLoggingEnabled && !isMetricsExportEnabled) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "DB Audit Logging, Query Logging and Metrics Export are not enabled. Please set runtime"
              + " flag 'yb.universe.audit_logging_enabled' or 'yb.universe.query_logging_enabled'"
              + " or 'yb.universe.metrics_export_enabled' to true.");
    }
  }

  public void throwExceptionIfDBAuditLoggingRuntimeFlagDisabled() {
    if (!isDBAuditLoggingRuntimeFlagEnabled()) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "DB Audit Logging is not enabled. Please set runtime flag"
              + " 'yb.universe.audit_logging_enabled' to true.");
    }
  }

  public void throwExceptionIfQueryLoggingRuntimeFlagDisabled() {
    if (!isQueryLoggingRuntimeFlagEnabled()) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Query Logging is not enabled. Please set runtime flag"
              + " 'yb.universe.query_logging_enabled' to true.");
    }
  }

  public void throwExceptionIfMetricsExportRuntimeFlagDisabled() {
    if (!isMetricsExportRuntimeFlagEnabled()) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Metrics Export is not enabled. Please set runtime flag"
              + " 'yb.universe.metrics_export_enabled' to true.");
    }
  }

  public boolean isDBAuditLoggingRuntimeFlagEnabled() {
    return confGetter.getGlobalConf(GlobalConfKeys.dbAuditLoggingEnabled);
  }

  public boolean isQueryLoggingRuntimeFlagEnabled() {
    return confGetter.getGlobalConf(GlobalConfKeys.queryLoggingEnabled);
  }

  public boolean isMetricsExportRuntimeFlagEnabled() {
    return confGetter.getGlobalConf(GlobalConfKeys.metricsExportEnabled);
  }

  public void throwRuntimeFlagDisabledForExporterTypeException(ProviderType providerType) {
    throwExceptionIfLokiExporterRuntimeFlagDisabled(providerType);
    throwExceptionIfS3ExporterRuntimeFlagDisabled(providerType);
    throwExceptionIfOTLPExporterRuntimeFlagDisabled(providerType);
  }

  public void throwExceptionIfLokiExporterRuntimeFlagDisabled(ProviderType providerType) {
    boolean isLokiTelemetryEnabled = confGetter.getGlobalConf(GlobalConfKeys.telemetryAllowLoki);
    if (!isLokiTelemetryEnabled && providerType == ProviderType.LOKI) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Loki Exporter for Telemetry Provider is not enabled. Please set runtime flag"
              + " 'yb.telemetry.allow_loki' to true.");
    }
  }

  public void throwExceptionIfS3ExporterRuntimeFlagDisabled(ProviderType providerType) {
    boolean isS3TelemetryEnabled = confGetter.getGlobalConf(GlobalConfKeys.telemetryAllowS3);
    if (!isS3TelemetryEnabled && providerType == ProviderType.S3) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "S3 Exporter for Telemetry Provider is not enabled. Please set runtime flag"
              + " 'yb.telemetry.allow_s3' to true.");
    }
  }

  public void throwExceptionIfOTLPExporterRuntimeFlagDisabled(ProviderType providerType) {
    boolean isOTLPTelemetryEnabled = confGetter.getGlobalConf(GlobalConfKeys.telemetryAllowOTLP);
    if (!isOTLPTelemetryEnabled && providerType == ProviderType.OTLP) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "OTLP Exporter for Telemetry Provider is not enabled. Please set runtime flag"
              + " 'yb.telemetry.allow_otlp' to true.");
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

      // Check if the provider is in the list of query log exporters.
      if (primaryUserIntent.getQueryLogConfig() != null
          && primaryUserIntent.getQueryLogConfig().getUniverseLogsExporterConfig() != null) {
        List<UniverseQueryLogsExporterConfig> universeLogsExporterConfigs =
            primaryUserIntent.getQueryLogConfig().getUniverseLogsExporterConfig();

        // Check if the provider is in the list of export configs in the audit log config.
        for (UniverseQueryLogsExporterConfig config : universeLogsExporterConfigs) {
          if (config != null && providerUUID.equals(config.getExporterUuid())) {
            return true;
          }
        }
      }
      if (primaryUserIntent.getMetricsExportConfig() != null
          && primaryUserIntent.getMetricsExportConfig().getUniverseMetricsExporterConfig()
              != null) {
        List<UniverseMetricsExporterConfig> metricsExporterConfigs =
            primaryUserIntent.getMetricsExportConfig().getUniverseMetricsExporterConfig();

        // Check if the provider is in the list of export configs in the metrics export config.
        for (UniverseMetricsExporterConfig config : metricsExporterConfigs) {
          if (config != null && providerUUID.equals(config.getExporterUuid())) {
            return true;
          }
        }
      }
    }
    return false;
  }

  public ApiHelper getApiHelper() {
    return new ApiHelper(this.wsClientRefresher.getClient(WS_CLIENT_KEY));
  }

  public void validateTelemetryProvider(TelemetryProvider provider) {
    this.apiHelper = getApiHelper();
    provider.getConfig().validate(this.apiHelper, this.confGetter);
  }
}
