/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common.config;

import static com.yugabyte.yw.models.ScopedRuntimeConfig.GLOBAL_SCOPE_UUID;

import com.google.common.collect.ImmutableSet;
import com.google.common.collect.Sets;
import com.google.common.collect.Streams;
import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.typesafe.config.ConfigRenderOptions;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.ConfKeyInfo.ConfKeyTags;
import com.yugabyte.yw.common.config.impl.RuntimeConfig;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.controllers.AuthenticatedController;
import com.yugabyte.yw.forms.RuntimeConfigFormData;
import com.yugabyte.yw.forms.RuntimeConfigFormData.ConfigEntry;
import com.yugabyte.yw.forms.RuntimeConfigFormData.ScopedConfig;
import com.yugabyte.yw.forms.RuntimeConfigFormData.ScopedConfig.ScopeType;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.RuntimeConfigEntry;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import io.ebean.Model;
import io.ebean.annotation.Transactional;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Singleton
public class RuntimeConfService extends AuthenticatedController {

  public static final String INCLUDED_OBJECTS_KEY = "runtime_config.included_objects";

  private final SettableRuntimeConfigFactory settableRuntimeConfigFactory;
  private final Set<String> mutableObjects;
  private final Set<String> mutableKeys;
  private static final Set<String> sensitiveKeys =
      ImmutableSet.of("yb.security.ldap.ldap_service_account_password", "yb.security.secret");

  private final RuntimeConfigPreChangeNotifier preChangeNotifier;

  private final RuntimeConfigChangeNotifier changeNotifier;

  private final Map<String, ConfKeyInfo<?>> keyMetaData;

  private final Config staticConfig;

  @Inject
  public RuntimeConfService(
      SettableRuntimeConfigFactory settableRuntimeConfigFactory,
      RuntimeConfigPreChangeNotifier preChangeNotifier,
      RuntimeConfigChangeNotifier changeNotifier,
      Config config,
      Map<String, ConfKeyInfo<?>> keyMetaData) {
    this.settableRuntimeConfigFactory = settableRuntimeConfigFactory;
    this.mutableObjects =
        Sets.newLinkedHashSet(
            settableRuntimeConfigFactory
                .staticApplicationConf()
                .getStringList(INCLUDED_OBJECTS_KEY));
    this.preChangeNotifier = preChangeNotifier;
    this.changeNotifier = changeNotifier;
    this.keyMetaData = keyMetaData;
    this.staticConfig = config;
    this.mutableKeys = buildMutableKeysSet();
  }

  private Set<String> buildMutableKeysSet() {
    Config config = settableRuntimeConfigFactory.staticApplicationConf();
    List<String> included = config.getStringList("runtime_config.included_paths");
    List<String> excluded = config.getStringList("runtime_config.excluded_paths");
    return Streams.concat(
            keyMetaData.keySet().stream(),
            mutableObjects.stream(),
            config.entrySet().stream()
                .map(Entry::getKey)
                .filter(
                    key ->
                        included.stream().anyMatch(key::startsWith)
                            && excluded.stream().noneMatch(key::startsWith)))
        .collect(Collectors.toSet());
  }

  public Set<String> getMutableKeys() {
    return mutableKeys;
  }

  public List<String> getFeatureFlagKeys() {
    return keyMetaData.values().stream()
        .filter(
            cki ->
                (cki.tags.contains(ConfKeyTags.FEATURE_FLAG)
                    && ScopeType.GLOBAL.equals(cki.getScope())))
        .map(cki -> cki.getKey())
        .collect(Collectors.toList());
  }

  public List<ConfigEntry> getFeatureFlagEntries() {
    List<ConfigEntry> featureFlagEntries = new ArrayList<>();
    Config globalConfig = settableRuntimeConfigFactory.globalRuntimeConf();
    for (String k : getFeatureFlagKeys()) {
      String value = globalConfig.getValue(k).render(ConfigRenderOptions.concise());
      value = unwrap(value);
      if (sensitiveKeys.contains(k)) {
        value = CommonUtils.getEmptiableMaskedValue(k, value);
      }
      featureFlagEntries.add(new ConfigEntry(false, k, value));
    }

    return featureFlagEntries;
  }

  public ScopedConfig getConfig(
      UUID customerUUID, UUID scopeUUID, boolean includeInherited, boolean isSuperAdmin) {
    log.trace(
        "customerUUID: {} scopeUUID: {} includeInherited: {}",
        customerUUID,
        scopeUUID,
        includeInherited);

    ScopedConfig scopedConfig = getScopedConfigOrFail(customerUUID, scopeUUID, isSuperAdmin);
    Config fullConfig = scopedConfig.runtimeConfig(settableRuntimeConfigFactory);
    Map<String, String> overriddenInScope = RuntimeConfigEntry.getAsMapForScope(scopeUUID);
    for (String k : mutableKeys) {
      boolean isOverridden = overriddenInScope.containsKey(k);
      log.trace(
          "key: {} overriddenInScope: {} includeInherited: {}", k, isOverridden, includeInherited);

      String value = fullConfig.getValue(k).render(ConfigRenderOptions.concise());
      value = unwrap(value);
      if (sensitiveKeys.contains(k)) {
        value = CommonUtils.getEmptiableMaskedValue(k, value);
      }

      if (isOverridden) {
        scopedConfig.configEntries.add(new ConfigEntry(false, k, value));
      } else if (includeInherited) {
        // Show entries even if not overridden in this scope. We will lookup value from fullConfig
        // for this scope
        scopedConfig.configEntries.add(new ConfigEntry(true, k, value));
      }
    }

    return scopedConfig;
  }

  private String unwrap(String maybeQuoted) {
    if (maybeQuoted.startsWith("\"") && maybeQuoted.endsWith("\"")) {
      return maybeQuoted.substring(1, maybeQuoted.length() - 1);
    }
    return maybeQuoted;
  }

  public String getKeyOrBadRequest(
      UUID customerUUID, UUID scopeUUID, String path, boolean isSuperAdmin) {
    Optional<String> value = maybeGetKey(customerUUID, scopeUUID, path, isSuperAdmin);
    if (value.isPresent()) {
      return value.get();
    } else if (isValidScope(path, scopeUUID)) {
      ScopeType scope = getScopeType(scopeUUID);
      switch (scope) {
        case GLOBAL:
          return settableRuntimeConfigFactory.globalRuntimeConf().getString(path);
        case CUSTOMER:
          return settableRuntimeConfigFactory.forCustomer(Customer.get(scopeUUID)).getString(path);
        case PROVIDER:
          return settableRuntimeConfigFactory
              .forProvider(Provider.maybeGet(scopeUUID).get())
              .getString(path);
        case UNIVERSE:
          return settableRuntimeConfigFactory
              .forUniverse(Universe.maybeGet(scopeUUID).get())
              .getString(path);
        default:
          // should never reach here.
          return settableRuntimeConfigFactory.staticApplicationConf().getString(path);
      }
    } else {
      throw new PlatformServiceException(
          NOT_FOUND, String.format("Key %s is not defined in scope %s", path, scopeUUID));
    }
  }

  private ScopeType getScopeType(UUID scopeUUID) {
    if (scopeUUID.equals(GLOBAL_SCOPE_UUID)) {
      return ScopeType.GLOBAL;
    } else if (Customer.get(scopeUUID) != null) {
      return ScopeType.CUSTOMER;
    } else if (Provider.maybeGet(scopeUUID).isPresent()) {
      return ScopeType.PROVIDER;
    } else if (Universe.maybeGet(scopeUUID).isPresent()) {
      return ScopeType.UNIVERSE;
    } else {
      throw new PlatformServiceException(BAD_REQUEST, "Invalid Scope UUID!");
    }
  }

  private boolean isValidScope(String path, UUID scopeUUID) {
    if (keyMetaData.containsKey(path)) {
      ScopeType scope = keyMetaData.get(path).getScope();
      return scope.isValid(scopeUUID);
    } else {
      return true;
    }
  }

  public String getKeyIfPresent(
      UUID customerUUID, UUID scopeUUID, String path, boolean isSuperAdmin) {
    return maybeGetKey(customerUUID, scopeUUID, path, isSuperAdmin).orElse(null);
  }

  public Optional<String> maybeGetKey(
      UUID customerUUID, UUID scopeUUID, String path, boolean isSuperAdmin) {
    if (!mutableKeys.contains(path))
      throw new PlatformServiceException(NOT_FOUND, "No mutable key found: " + path);

    Optional<ScopedConfig> scopedConfig = getScopedConfig(customerUUID, scopeUUID, isSuperAdmin);

    if (!scopedConfig.isPresent()) {
      throw new PlatformServiceException(
          NOT_FOUND, String.format("No scope %s  found for customer %s", scopeUUID, customerUUID));
    }

    Optional<RuntimeConfigEntry> runtimeConfigEntry = RuntimeConfigEntry.maybeGet(scopeUUID, path);
    if (!runtimeConfigEntry.isPresent()) {
      return Optional.empty();
    }

    String value = runtimeConfigEntry.get().getValue();
    if (sensitiveKeys.contains(path)) {
      value = CommonUtils.getMaskedValue(path, value);
    }
    return Optional.of(value);
  }

  @Transactional
  public void setKey(
      UUID customerUUID, UUID scopeUUID, String path, String value, boolean isSuperAdmin) {
    if (!mutableKeys.contains(path)) {
      throw new PlatformServiceException(NOT_FOUND, "No mutable key found: " + path);
    }

    String logValue = value;
    if (sensitiveKeys.contains(path)) {
      logValue = CommonUtils.getMaskedValue(path, logValue);
    }
    log.info(
        "Setting runtime conf for key '{}' on scope {} to value '{}' of length {}",
        path,
        scopeUUID,
        (logValue.length() < 50 ? logValue : "[long value hidden]"),
        logValue.length());
    final RuntimeConfig<?> mutableRuntimeConfig =
        getMutableRuntimeConfigForScopeOrFail(customerUUID, scopeUUID, isSuperAdmin);
    preConfigChangeValidate(scopeUUID, path, value);
    if (mutableObjects.contains(path)) {
      mutableRuntimeConfig.setObject(path, value);
    } else {
      mutableRuntimeConfig.setValue(path, value);
    }
    postConfigChange(scopeUUID, path);
  }

  private void postConfigChange(UUID scopeUUID, String path) {
    try {
      changeNotifier.notifyListeners(scopeUUID, path);
    } catch (RuntimeException e) {
      log.error("Failed to apply runtime config value", e);
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Failed to apply runtime config value");
    }
  }

  private void preConfigChangeValidate(UUID scopeUUID, String path, String newValue) {
    preChangeNotifier.notifyListeners(scopeUUID, path, newValue);
  }

  private void preConfigDeleteValidate(UUID scopeUUID, String path) {
    preChangeNotifier.notifyListenersDeleteConfig(scopeUUID, path);
  }

  public void deleteKey(UUID customerUUID, UUID scopeUUID, String path, boolean isSuperAdmin) {
    if (!mutableKeys.contains(path)) {
      throw new PlatformServiceException(NOT_FOUND, "No mutable key found: " + path);
    }

    preConfigDeleteValidate(scopeUUID, path);
    getMutableRuntimeConfigForScopeOrFail(customerUUID, scopeUUID, isSuperAdmin).deleteEntry(path);
    postConfigChange(scopeUUID, path);
  }

  private RuntimeConfig<? extends Model> getMutableRuntimeConfigForScopeOrFail(
      UUID customerUUID, UUID scopeUUID, boolean isSuperAdmin) {
    ScopedConfig scopedConfig = getScopedConfigOrFail(customerUUID, scopeUUID, isSuperAdmin);
    if (!scopedConfig.mutableScope) {
      throw new PlatformServiceException(
          FORBIDDEN,
          "Customer "
              + customerUUID
              + " does not have access to mutate configuration for this scope "
              + scopeUUID);
    }
    return scopedConfig.runtimeConfig(settableRuntimeConfigFactory);
  }

  private ScopedConfig getScopedConfigOrFail(
      UUID customerUUID, UUID scopeUUID, boolean isSuperAdmin) {
    Optional<ScopedConfig> optScopedConfig = getScopedConfig(customerUUID, scopeUUID, isSuperAdmin);
    if (!optScopedConfig.isPresent()) {
      throw new PlatformServiceException(
          NOT_FOUND, String.format("No scope %s found for customer %s", scopeUUID, customerUUID));
    }
    return optScopedConfig.get();
  }

  public Optional<ScopedConfig> getScopedConfig(
      UUID customerUUID, UUID scopeUUID, boolean isSuperAdmin) {
    RuntimeConfigFormData runtimeConfigFormData =
        listScopes(Customer.getOrBadRequest(customerUUID), isSuperAdmin);
    return runtimeConfigFormData.scopedConfigList.stream()
        .filter(config -> config.uuid.equals(scopeUUID))
        .findFirst();
  }

  public RuntimeConfigFormData listScopes(Customer customer, boolean includeGlobal) {
    RuntimeConfigFormData formData = new RuntimeConfigFormData();
    formData.addGlobalScope(includeGlobal);
    formData.addMutableScope(ScopeType.CUSTOMER, customer.getUuid());
    Provider.getAll(customer.getUuid())
        .forEach(provider -> formData.addMutableScope(ScopeType.PROVIDER, provider.getUuid()));
    Universe.getAllUUIDs(customer)
        .forEach(universeUUID -> formData.addMutableScope(ScopeType.UNIVERSE, universeUUID));
    return formData;
  }
}
