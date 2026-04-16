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

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.annotations.VisibleForTesting;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.WSClientRefresher;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.TelemetryProvider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.exporters.audit.AuditLogConfig;
import com.yugabyte.yw.models.helpers.exporters.audit.UniverseLogsExporterConfig;
import com.yugabyte.yw.models.helpers.exporters.metrics.MetricsExportConfig;
import com.yugabyte.yw.models.helpers.exporters.metrics.UniverseMetricsExporterConfig;
import com.yugabyte.yw.models.helpers.exporters.query.QueryLogConfig;
import com.yugabyte.yw.models.helpers.exporters.query.UniverseQueryLogsExporterConfig;
import com.yugabyte.yw.models.helpers.telemetry.AWSCloudWatchConfig;
import com.yugabyte.yw.models.helpers.telemetry.GCPCloudMonitoringConfig;
import com.yugabyte.yw.models.helpers.telemetry.ProviderType;
import com.yugabyte.yw.models.helpers.telemetry.S3Config;
import io.ebean.annotation.Transactional;
import java.util.Collection;
import java.util.Collections;
import java.util.EnumSet;
import java.util.HashSet;
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

        // Check if the provider is in the list of export configs in the query log config.
        for (UniverseQueryLogsExporterConfig config : universeLogsExporterConfigs) {
          if (config != null && providerUUID.equals(config.getExporterUuid())) {
            return true;
          }
        }
      }

      // Check if the provider is in the list of metrics export exporters.
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
    return new ApiHelper(
        this.wsClientRefresher.getClient(WS_CLIENT_KEY), wsClientRefresher.getMaterializer());
  }

  public void validateTelemetryProvider(TelemetryProvider provider) {
    this.apiHelper = getApiHelper();
    provider.getConfig().validate(this.apiHelper, this.confGetter);
  }

  /**
   * Compares credentials across telemetry providers if they are consistent. For AWS providers, it
   * checks if access keys and secret keys are the same. For GCP Cloud Monitoring providers, it
   * checks if credentials JsonNode are the same. We only need to check AWS and GCP since those are
   * exported as environment variables on the DB nodes.
   *
   * @param universe The universe to validate credentials for
   * @param telemetryProviders List of telemetry providers to compare
   * @return true if all credentials are consistent, false if any differences are found
   */
  public boolean areCredentialsConsistent(
      Universe universe, List<TelemetryProvider> telemetryProviders) {
    if (CollectionUtils.isEmpty(telemetryProviders)) {
      return true;
    }

    boolean allCredentialsConsistent = true;

    // Filter and validate AWS CloudWatch and S3 providers
    // Checking combined since both cloudwatch and S3 should have a single set of credentials.
    Set<ProviderType> awsProviderTypes = EnumSet.of(ProviderType.AWS_CLOUDWATCH, ProviderType.S3);
    List<TelemetryProvider> awsProviders =
        telemetryProviders.stream()
            .filter(p -> p.getConfig() != null && p.getConfig().getType() != null)
            .filter(p -> awsProviderTypes.contains(p.getConfig().getType()))
            .collect(Collectors.toList());

    if (awsProviders.size() > 1) {
      String firstAccessKey = null;
      String firstSecretKey = null;
      if (ProviderType.AWS_CLOUDWATCH.equals(awsProviders.get(0).getConfig().getType())) {
        AWSCloudWatchConfig firstAWSConfig = (AWSCloudWatchConfig) awsProviders.get(0).getConfig();
        firstAccessKey = firstAWSConfig.getAccessKey();
        firstSecretKey = firstAWSConfig.getSecretKey();
      } else if (ProviderType.S3.equals(awsProviders.get(0).getConfig().getType())) {
        S3Config firstS3Config = (S3Config) awsProviders.get(0).getConfig();
        firstAccessKey = firstS3Config.getAccessKey();
        firstSecretKey = firstS3Config.getSecretKey();
      }

      for (int i = 1; i < awsProviders.size(); i++) {
        String currentAccessKey = null;
        String currentSecretKey = null;
        if (ProviderType.AWS_CLOUDWATCH.equals(awsProviders.get(i).getConfig().getType())) {
          AWSCloudWatchConfig currentAWSConfig =
              (AWSCloudWatchConfig) awsProviders.get(i).getConfig();
          currentAccessKey = currentAWSConfig.getAccessKey();
          currentSecretKey = currentAWSConfig.getSecretKey();
        } else if (ProviderType.S3.equals(awsProviders.get(i).getConfig().getType())) {
          S3Config currentS3Config = (S3Config) awsProviders.get(i).getConfig();
          currentAccessKey = currentS3Config.getAccessKey();
          currentSecretKey = currentS3Config.getSecretKey();
        }

        if (firstAccessKey != null
            && firstSecretKey != null
            && currentAccessKey != null
            && currentSecretKey != null
            && !(java.util.Objects.equals(firstAccessKey, currentAccessKey)
                && java.util.Objects.equals(firstSecretKey, currentSecretKey))) {
          allCredentialsConsistent = false;
          log.error(
              "AWS telemetry provider credentials mismatch detected on Universe '{}'"
                  + " (UUID: {}). Telemetry Provider '{}' (UUID: {}) has different access key or"
                  + " secret key compared to telemetry provider '{}' (UUID: {}).",
              universe.getName(),
              universe.getUniverseUUID(),
              awsProviders.get(i).getName(),
              awsProviders.get(i).getUuid(),
              awsProviders.get(0).getName(),
              awsProviders.get(0).getUuid());
        }
      }
    }

    // Filter and validate GCP Cloud Monitoring providers
    List<TelemetryProvider> gcpProviders =
        telemetryProviders.stream()
            .filter(p -> p.getConfig() != null)
            .filter(p -> ProviderType.GCP_CLOUD_MONITORING.equals(p.getConfig().getType()))
            .collect(Collectors.toList());

    if (gcpProviders.size() > 1) {
      GCPCloudMonitoringConfig firstGCPConfig =
          (GCPCloudMonitoringConfig) gcpProviders.get(0).getConfig();
      JsonNode firstCredentials = firstGCPConfig.getGcmCredentials();

      for (int i = 1; i < gcpProviders.size(); i++) {
        GCPCloudMonitoringConfig currentConfig =
            (GCPCloudMonitoringConfig) gcpProviders.get(i).getConfig();
        JsonNode currentCredentials = currentConfig.getGcmCredentials();

        if (!java.util.Objects.equals(firstCredentials, currentCredentials)) {
          allCredentialsConsistent = false;
          log.error(
              "GCP Cloud Monitoring telemetry provider credentials mismatch detected on Universe"
                  + " '{}' (UUID: {}). Telemetry Provider '{}' (UUID: {}) has different credentials"
                  + " compared to telemetry provider '{}' (UUID: {}).",
              universe.getName(),
              universe.getUniverseUUID(),
              gcpProviders.get(i).getName(),
              gcpProviders.get(i).getUuid(),
              gcpProviders.get(0).getName(),
              gcpProviders.get(0).getUuid());
        }
      }
    }

    return allCredentialsConsistent;
  }

  /**
   * Validates telemetry provider credentials consistency on a universe. Collects all telemetry
   * providers from the universe's audit log config, query log config, and metrics export config,
   * combines them with any extra exporter UUIDs provided, then validates their credentials
   * consistency for AWS and GCP TPs. AWS and GCP TPs must have the same credentials on the same
   * universe, since they are exported as environment variables on the DB nodes.
   *
   * @param universe The universe to validate credentials for
   * @param auditLogExporterUuids Optional set of audit log exporter UUIDs to override in
   *     validation. If not provided, the universe audit log config will be used.
   * @param queryLogExporterUuids Optional set of query log exporter UUIDs to override in
   *     validation. If not provided, the universe query log config will be used.
   * @param metricsExportExporterUuids Optional set of metrics export exporter UUIDs to override in
   *     validation. If not provided, the universe metrics export config will be used.
   * @return true if all credentials are consistent, false if any differences are found
   */
  public boolean areTPsCredentialsConsistentOnUniverse(
      Universe universe,
      Set<UUID> auditLogExporterUuids,
      Set<UUID> queryLogExporterUuids,
      Set<UUID> metricsExportExporterUuids) {
    if (universe == null || universe.getUniverseDetails() == null) {
      return true;
    }

    if (confGetter.getConfForScope(universe, UniverseConfKeys.skipTPsCredsConsistencyCheck)) {
      log.info(
          "Skipping telemetry provider credential consistency check on universe '{}'",
          universe.getName());
      return true;
    }

    UserIntent primaryUserIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
    if (primaryUserIntent == null) {
      return true;
    }

    Set<UUID> telemetryProviderUuids = new HashSet<>();

    // Collect telemetry provider UUIDs from audit log config
    if (auditLogExporterUuids != null && !auditLogExporterUuids.isEmpty()) {
      telemetryProviderUuids.addAll(auditLogExporterUuids);
    } else {
      AuditLogConfig auditLogConfig = primaryUserIntent.getAuditLogConfig();
      if (auditLogConfig != null && auditLogConfig.getUniverseLogsExporterConfig() != null) {
        telemetryProviderUuids.addAll(
            auditLogConfig.getUniverseLogsExporterConfig().stream()
                .filter(config -> config != null && config.getExporterUuid() != null)
                .map(UniverseLogsExporterConfig::getExporterUuid)
                .collect(Collectors.toSet()));
      }
    }

    // Collect telemetry provider UUIDs from query log config
    if (queryLogExporterUuids != null && !queryLogExporterUuids.isEmpty()) {
      telemetryProviderUuids.addAll(queryLogExporterUuids);
    } else {
      QueryLogConfig queryLogConfig = primaryUserIntent.getQueryLogConfig();
      if (queryLogConfig != null && queryLogConfig.getUniverseLogsExporterConfig() != null) {
        telemetryProviderUuids.addAll(
            queryLogConfig.getUniverseLogsExporterConfig().stream()
                .filter(config -> config != null && config.getExporterUuid() != null)
                .map(UniverseQueryLogsExporterConfig::getExporterUuid)
                .collect(Collectors.toSet()));
      }
    }

    // Collect telemetry provider UUIDs from metrics export config
    if (metricsExportExporterUuids != null && !metricsExportExporterUuids.isEmpty()) {
      telemetryProviderUuids.addAll(metricsExportExporterUuids);
    } else {
      MetricsExportConfig metricsExportConfig = primaryUserIntent.getMetricsExportConfig();
      if (metricsExportConfig != null
          && metricsExportConfig.getUniverseMetricsExporterConfig() != null) {
        telemetryProviderUuids.addAll(
            metricsExportConfig.getUniverseMetricsExporterConfig().stream()
                .filter(config -> config != null && config.getExporterUuid() != null)
                .map(UniverseMetricsExporterConfig::getExporterUuid)
                .collect(Collectors.toSet()));
      }
    }

    // Fetch all telemetry providers in one batch query
    List<TelemetryProvider> allTelemetryProviders = list(telemetryProviderUuids);

    return areCredentialsConsistent(universe, allTelemetryProviders);
  }

  /**
   * Validates telemetry provider credentials consistency on a universe. This is a convenience
   * overload that calls the main method with an empty set of extra exporter UUIDs.
   *
   * @param universe The universe to validate credentials for
   * @return true if all credentials are consistent, false if any differences are found
   */
  public boolean areTPsCredentialsConsistentOnUniverse(Universe universe) {
    return areTPsCredentialsConsistentOnUniverse(universe, null, null, null);
  }
}
