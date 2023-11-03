/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common.config.impl;

import static com.yugabyte.yw.models.ScopedRuntimeConfig.GLOBAL_SCOPE_UUID;

import com.google.common.annotations.VisibleForTesting;
import com.typesafe.config.Config;
import com.typesafe.config.ConfigFactory;
import com.typesafe.config.ConfigParseOptions;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.ybflyway.YBFlywayInit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.RuntimeConfigEntry;
import com.yugabyte.yw.models.Universe;
import io.ebean.Model;
import java.util.Map;
import java.util.UUID;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import javax.inject.Inject;
import javax.inject.Singleton;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.db.ebean.EbeanDynamicEvolutions;
import play.libs.Json;

/** Factory to create RuntimeConfig for various scopes */
@Singleton
public class SettableRuntimeConfigFactory implements RuntimeConfigFactory {
  private static final Logger LOG = LoggerFactory.getLogger(SettableRuntimeConfigFactory.class);

  // For example, anything just email, or ending with .email, _email or -email matches.
  private static final Pattern SENSITIVE_CONFIG_NAME_PAT =
      Pattern.compile("(^|\\.|[_\\-])(email|password|server)$");

  @VisibleForTesting
  static final String RUNTIME_CONFIG_INCLUDED_OBJECTS = "runtime_config.included_objects";

  private final Config appConfig;

  private static final String newLine = System.getProperty("line.separator");

  // We need to do this because appConfig is preResolved by playFramework
  // So setting references to global or universe scoped config in reference.conf or application.conf
  // wont resolve to unexpected.
  // This helps us avoid unnecessary migrations of config keys.
  private static final Config UNRESOLVED_STATIC_CONFIG =
      ConfigFactory.parseString(
          String.join(
              newLine,
              "yb {",
              "  upgrade.vmImage = ${yb.cloud.enabled}",
              "  external_script {",
              "    content = ${?platform_ext_script_content}",
              "    params = ${?platform_ext_script_params}",
              "    schedule = ${?platform_ext_script_schedule}",
              "  }",
              "  health { trigger_api.enabled = ${yb.cloud.enabled} }",
              "  security {",
              "    custom_hooks {",
              "      enable_api_triggered_hooks = ${yb.cloud.enabled}",
              "    }",
              "  }",
              "}"));

  @Inject
  public SettableRuntimeConfigFactory(
      Config appConfig, EbeanDynamicEvolutions ebeanDynamicEvolutions, YBFlywayInit ybFlywayInit) {
    this.appConfig = appConfig;
  }

  /**
   * @return A RuntimeConfig instance for a given scope
   */
  @Override
  public RuntimeConfig<Customer> forCustomer(Customer customer) {
    RuntimeConfig<Customer> config =
        new RuntimeConfig<>(
            customer,
            getConfigForScope(customer.getUuid(), "Scoped Config (" + customer + ")")
                .withFallback(globalConfig()));
    LOG.trace("forCustomer {}: {}", customer.getUuid(), config);
    return config;
  }

  /**
   * @return A RuntimeConfig instance for a given scope
   */
  @Override
  public RuntimeConfig<Universe> forUniverse(Universe universe) {
    Customer customer = Customer.get(universe.getCustomerId());
    RuntimeConfig<Universe> config =
        new RuntimeConfig<>(
            universe,
            getConfigForScope(universe.getUniverseUUID(), "Scoped Config (" + universe + ")")
                .withFallback(
                    getConfigForScope(customer.getUuid(), "Scoped Config (" + customer + ")"))
                .withFallback(globalConfig()));
    LOG.trace("forUniverse {}: {}", universe.getUniverseUUID(), config);
    return config;
  }

  /**
   * @return A RuntimeConfig instance for a given scope
   */
  @Override
  public RuntimeConfig<Provider> forProvider(Provider provider) {
    Customer customer = Customer.get(provider.getCustomerUUID());
    RuntimeConfig<Provider> config =
        new RuntimeConfig<>(
            provider,
            getConfigForScope(provider.getUuid(), "Scoped Config (" + provider + ")")
                .withFallback(
                    getConfigForScope(customer.getUuid(), "Scoped Config (" + customer + ")"))
                .withFallback(globalConfig()));
    LOG.trace("forProvider {}: {}", provider.getUuid(), config);
    return config;
  }

  /**
   * @return A RuntimeConfig instance for a GLOBAL_SCOPE
   */
  @Override
  public RuntimeConfig<Model> globalRuntimeConf() {
    return new RuntimeConfig<>(globalConfig());
  }

  @Override
  public Config staticApplicationConf() {
    return appConfig;
  }

  private Config globalConfig() {
    Config config =
        getConfigForScope(GLOBAL_SCOPE_UUID, "Global Runtime Config (" + GLOBAL_SCOPE_UUID + ")")
            .withFallback(UNRESOLVED_STATIC_CONFIG)
            .withFallback(appConfig);
    if (LOG.isTraceEnabled()) {
      LOG.trace("globalConfig : {}", toRedactedString(config));
    }
    return config;
  }

  @VisibleForTesting
  Config getConfigForScope(UUID scope, String description) {
    Map<String, String> values = RuntimeConfigEntry.getAsMapForScope(scope);
    return toConfig(description, values);
  }

  private Config toConfig(String description, Map<String, String> values) {
    String confStr = toConfigString(values);
    Config config =
        ConfigFactory.parseString(
            confStr, ConfigParseOptions.defaults().setOriginDescription(description));

    if (LOG.isTraceEnabled()) {
      LOG.trace("Read from DB for {}: {}", description, toRedactedString(config));
    }
    return config;
  }

  private String toConfigString(Map<String, String> values) {
    return values.entrySet().stream()
        .map(entry -> entry.getKey() + "=" + maybeQuote(entry))
        .collect(Collectors.joining("\n"));
  }

  private String maybeQuote(Map.Entry<String, String> entry) {
    final boolean isObject =
        appConfig.getStringList(RUNTIME_CONFIG_INCLUDED_OBJECTS).contains(entry.getKey());
    if (isObject || entry.getValue().startsWith("\"")) {
      // No need to escape
      return entry.getValue();
    }
    return Json.stringify(Json.toJson(entry.getValue()));
  }

  @VisibleForTesting
  static String toRedactedString(Config config) {
    return config.entrySet().stream()
        .map(
            entry -> {
              if (SENSITIVE_CONFIG_NAME_PAT.matcher(entry.getKey()).find()) {
                return entry.getKey() + "=REDACTED";
              }
              return entry.getKey() + "=" + entry.getValue();
            })
        .collect(Collectors.joining(", "));
  }
}
