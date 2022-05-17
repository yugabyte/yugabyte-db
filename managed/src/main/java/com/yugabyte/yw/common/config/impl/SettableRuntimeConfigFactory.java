/*
 * Copyright 2021 YugaByte, Inc. and Contributors
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

  @VisibleForTesting
  static final String RUNTIME_CONFIG_INCLUDED_OBJECTS = "runtime_config.included_objects";

  private final Config appConfig;

  @Inject
  public SettableRuntimeConfigFactory(
      Config appConfig, EbeanDynamicEvolutions ebeanDynamicEvolutions, YBFlywayInit ybFlywayInit) {
    this.appConfig = appConfig;
  }

  /** @return A RuntimeConfig instance for a given scope */
  @Override
  public RuntimeConfig<Customer> forCustomer(Customer customer) {
    Config config =
        getConfigForScope(customer.uuid, "Scoped Config (" + customer + ")")
            .withFallback(globalConfig());
    LOG.trace("forCustomer {}: {}", customer.uuid, config);
    return new RuntimeConfig<>(customer, config);
  }

  /** @return A RuntimeConfig instance for a given scope */
  @Override
  public RuntimeConfig<Universe> forUniverse(Universe universe) {
    Customer customer = Customer.get(universe.customerId);
    Config config =
        getConfigForScope(universe.universeUUID, "Scoped Config (" + universe + ")")
            .withFallback(getConfigForScope(customer.uuid, "Scoped Config (" + customer + ")"))
            .withFallback(globalConfig());
    LOG.trace("forUniverse {}: {}", universe.universeUUID, config);
    return new RuntimeConfig<>(universe, config);
  }

  /** @return A RuntimeConfig instance for a given scope */
  @Override
  public RuntimeConfig<Provider> forProvider(Provider provider) {
    Customer customer = Customer.get(provider.customerUUID);
    Config config =
        getConfigForScope(provider.uuid, "Scoped Config (" + provider + ")")
            .withFallback(getConfigForScope(customer.uuid, "Scoped Config (" + customer + ")"))
            .withFallback(globalConfig());
    LOG.trace("forProvider {}: {}", provider.uuid, config);
    return new RuntimeConfig<>(provider, config);
  }

  /** @return A RuntimeConfig instance for a GLOBAL_SCOPE */
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
            .withFallback(appConfig);
    LOG.trace("globalConfig : {}", config);
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
    LOG.trace("Read from DB for {}: {}", description, config);
    return config;
  }

  private String toConfigString(Map<String, String> values) {
    return values
        .entrySet()
        .stream()
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
}
