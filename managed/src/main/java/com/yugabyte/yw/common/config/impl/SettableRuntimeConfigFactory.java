/*
 * Copyright 2022 YugabyteDB, Inc. and Contributors
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
import com.google.common.cache.Cache;
import com.google.common.cache.CacheBuilder;
import com.typesafe.config.Config;
import com.typesafe.config.ConfigFactory;
import com.typesafe.config.ConfigParseOptions;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.ybflyway.YBFlywayInit;
import com.yugabyte.yw.forms.RuntimeConfigFormData.ScopedConfig.ScopeType;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.RuntimeConfigEntry;
import com.yugabyte.yw.models.ScopedRuntimeConfig;
import com.yugabyte.yw.models.Universe;
import io.ebean.Model;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.TimeUnit;
import java.util.function.BiConsumer;
import java.util.function.Supplier;
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

  private static final String CACHE_ENABLED_KEY = "runtime_config.cache_enabled";
  private static final String CACHE_EXPIRY_DURATION_KEY = "runtime_config.cache_expiry_duration";
  private static final String CACHE_CAPACITY_KEY = "runtime_config.cache_capacity";

  // For example, anything just email, or ending with .email, _email or -email matches.
  private static final Pattern SENSITIVE_CONFIG_NAME_PAT =
      Pattern.compile("(^|\\.|[_\\-])(email|password|server)$");

  @VisibleForTesting
  static final String RUNTIME_CONFIG_INCLUDED_OBJECTS = "runtime_config.included_objects";

  private final Config appConfig;

  private static final String newLine = System.lineSeparator();

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

  // Track all the overridden scopes to facilitate cache invalidation on scope deletion.
  private final Set<UUID> overriddenScopes;
  // Cache the resolved configs for scopes.
  private final Cache<UUID, Config> cachedConfigs;

  @Inject
  public SettableRuntimeConfigFactory(
      Config appConfig, EbeanDynamicEvolutions ebeanDynamicEvolutions, YBFlywayInit ybFlywayInit) {
    this.appConfig = appConfig;
    this.overriddenScopes = ConcurrentHashMap.newKeySet();
    this.cachedConfigs = createCache();
  }

  /**
   * @return A RuntimeConfig instance for a given scope
   */
  @Override
  public RuntimeConfig<Customer> forCustomer(Customer customer) {
    Config scopeConfig =
        getOrCacheConfigForScope(
            customer.getUuid(),
            () ->
                getConfigForScope(customer.getUuid(), "Scoped Config (" + customer + ")")
                    .withFallback(globalConfig())
                    .resolve());
    RuntimeConfig<Customer> config =
        new RuntimeConfig<Customer>(customer, scopeConfig, getChangeListener());
    LOG.trace("forCustomer {}: {}", customer.getUuid(), config);
    return config;
  }

  /**
   * @return A RuntimeConfig instance for a given scope
   */
  @Override
  public RuntimeConfig<Universe> forUniverse(Universe universe) {
    Customer customer = Customer.get(universe.getCustomerId());
    Config scopeConfig =
        getOrCacheConfigForScope(
            universe.getUniverseUUID(),
            () ->
                getConfigForScope(universe.getUniverseUUID(), "Scoped Config (" + universe + ")")
                    .withFallback(
                        getConfigForScope(customer.getUuid(), "Scoped Config (" + customer + ")"))
                    .withFallback(globalConfig())
                    .resolve());
    RuntimeConfig<Universe> config =
        new RuntimeConfig<>(universe, scopeConfig, getChangeListener());
    LOG.trace("forUniverse {}: {}", universe.getUniverseUUID(), config);
    return config;
  }

  /**
   * @return A RuntimeConfig instance for a given scope
   */
  @Override
  public RuntimeConfig<Provider> forProvider(Provider provider) {
    Customer customer = Customer.get(provider.getCustomerUUID());
    Config scopeConfig =
        getOrCacheConfigForScope(
            provider.getUuid(),
            () ->
                getConfigForScope(provider.getUuid(), "Scoped Config (" + provider + ")")
                    .withFallback(
                        getConfigForScope(customer.getUuid(), "Scoped Config (" + customer + ")"))
                    .withFallback(globalConfig())
                    .resolve());
    RuntimeConfig<Provider> config =
        new RuntimeConfig<>(provider, scopeConfig, getChangeListener());
    LOG.trace("forProvider {}: {}", provider.getUuid(), config);
    return config;
  }

  /**
   * @return A RuntimeConfig instance for a GLOBAL_SCOPE
   */
  @Override
  public RuntimeConfig<Model> globalRuntimeConf() {
    Config globalConfig =
        getOrCacheConfigForScope(GLOBAL_SCOPE_UUID, () -> globalConfig().resolve());
    return new RuntimeConfig<>(globalConfig, getChangeListener());
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

  @VisibleForTesting
  public Map<UUID, Config> getCachedConfigs() {
    return cachedConfigs != null ? cachedConfigs.asMap() : Map.of();
  }

  @VisibleForTesting
  public void clearCache() {
    synchronized (overriddenScopes) {
      if (cachedConfigs != null) {
        cachedConfigs.invalidateAll();
      }
      overriddenScopes.clear();
    }
  }

  void removeCache(UUID scope) {
    if (cachedConfigs != null) {
      cachedConfigs.invalidate(scope);
    }
  }

  private Config getOrCacheConfigForScope(UUID scope, Supplier<Config> supplier) {
    if (cachedConfigs == null) {
      // Cache is disabled. Load directly from DB.
      return supplier.get();
    }
    Set<UUID> snapshotScopes = new HashSet<>(overriddenScopes);
    if (snapshotScopes.size() > 0 && !ScopedRuntimeConfig.containsAll(snapshotScopes)) {
      synchronized (overriddenScopes) {
        if (overriddenScopes.equals(snapshotScopes)) {
          // Some overridden scopes are deleted. E.g universe or provider is deleted.
          // OverridenScopes is stale too. Clear if it has not been cleared (compare and clear).
          LOG.debug(
              "Clearing entire runtime config cache as there are deleted scopes {}",
              snapshotScopes);
          clearCache();
        }
      }
    }
    try {
      return cachedConfigs.get(
          scope,
          () -> {
            Config config = supplier.get();
            if (ScopedRuntimeConfig.isPresent(scope)) {
              synchronized (overriddenScopes) {
                // Override is present for this scope. Track it for invalidation on deletion.
                // It is ok to add the scope for tracking after clearing the overriddenScopes.
                // But, it is not ok to clear (tracking lost) after adding to overriddenScopes.
                // Compare and clear is needed to avoid the latter case.
                overriddenScopes.add(scope);
              }
            }
            return config;
          });
    } catch (ExecutionException e) {
      // Actual loading is in this thread.
      throw new RuntimeException("Error loading config for scope " + scope, e);
    }
  }

  private Cache<UUID, Config> createCache() {
    if (appConfig.getBoolean(CACHE_ENABLED_KEY)) {
      long expirySeconds = appConfig.getDuration(CACHE_EXPIRY_DURATION_KEY, TimeUnit.SECONDS);
      int capacity = appConfig.getInt(CACHE_CAPACITY_KEY);
      LOG.info(
          "Runtime config cache is enabled with expiry {} secs and capacity {}",
          expirySeconds,
          capacity);
      return CacheBuilder.newBuilder()
          .expireAfterAccess(expirySeconds, TimeUnit.SECONDS)
          .maximumSize(capacity)
          .build();
    } else {
      LOG.info("Runtime config cache is disabled");
    }
    return null;
  }

  // Cache invalidator for changes triggered via APIs.
  private BiConsumer<ScopeType, RuntimeConfigEntry> getChangeListener() {
    return (type, entry) -> {
      LOG.debug(
          "Received notification for scope {}({}) with path {}",
          entry.getScopeUUID(),
          type,
          entry.getPath());
      if (type == ScopeType.GLOBAL || type == ScopeType.CUSTOMER) {
        // Finer grained invalidation can be implemented here if needed. For now we just clear the
        // cache to also remove the dependents due to fall-backs.
        clearCache();
      } else {
        removeCache(entry.getScopeUUID());
      }
    };
  }
}
