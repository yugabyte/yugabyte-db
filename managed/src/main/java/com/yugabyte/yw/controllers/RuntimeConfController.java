/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.node.ArrayNode;
import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.forms.RuntimeConfigFormData;
import com.yugabyte.yw.forms.RuntimeConfigFormData.ConfigEntry;
import com.yugabyte.yw.forms.RuntimeConfigFormData.ScopedConfig;
import com.yugabyte.yw.forms.RuntimeConfigFormData.ScopedConfig.ScopeType;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.RuntimeConfigEntry;
import com.yugabyte.yw.models.Universe;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;
import play.mvc.Result;

import java.util.*;
import java.util.stream.Collector;
import java.util.stream.Collectors;

public class RuntimeConfController extends AuthenticatedController {
  private static final Logger LOG = LoggerFactory.getLogger(RuntimeConfController.class);
  private final SettableRuntimeConfigFactory settableRuntimeConfigFactory;
  private final Result mutableKeysResult;
  private final Set<String> mutableKeys;

  @Inject
  public RuntimeConfController(SettableRuntimeConfigFactory settableRuntimeConfigFactory) {
    this.settableRuntimeConfigFactory = settableRuntimeConfigFactory;
    this.mutableKeys = buildMutableKeysSet();
    this.mutableKeysResult = buildCachedResult();
  }

  private static RuntimeConfigFormData listScopesInternal(Customer customer) {
    boolean isSuperAdmin = TokenAuthenticator.superAdminAuthentication(ctx());
    RuntimeConfigFormData formData = new RuntimeConfigFormData();
    formData.addGlobalScope(isSuperAdmin);
    formData.addMutableScope(ScopeType.CUSTOMER, customer.uuid);
    Provider.getAll(customer.uuid)
      .forEach(provider -> formData.addMutableScope(ScopeType.PROVIDER, provider.uuid));
    Universe.getAllUUIDs(customer)
      .forEach(universeUUID -> formData.addMutableScope(ScopeType.UNIVERSE, universeUUID));
    return formData;
  }

  private static Optional<ScopedConfig> getScopedConfigInternal(UUID customerUUID, UUID scopeUUID) {
    RuntimeConfigFormData runtimeConfigFormData = listScopesInternal(Customer.get(customerUUID));
    return runtimeConfigFormData.scopedConfigList.stream()
      .filter(config -> config.uuid.equals(scopeUUID))
      .findFirst();
  }

  private Result buildCachedResult() {
    ArrayNode list =
      mutableKeys.stream()
        .sorted()
        .collect(Collector.of(Json::newArray, ArrayNode::add, ArrayNode::addAll));
    return ok(list);
  }

  private Set<String> buildMutableKeysSet() {
    Config config = settableRuntimeConfigFactory.staticApplicationConf();
    List<String> included = config.getStringList("runtime_config.included_paths");
    List<String> excluded = config.getStringList("runtime_config.excluded_paths");
    return config.entrySet().stream()
      .map(Map.Entry::getKey)
      .filter(
        key ->
          included.stream().anyMatch(key::startsWith)
            && excluded.stream().noneMatch(key::startsWith))
      .collect(Collectors.toSet());
  }

  public Result listKeys() {
    return mutableKeysResult;
  }

  public Result listScopes(UUID customerUUID) {
    RuntimeConfigFormData formData = listScopesInternal(Customer.get(customerUUID));
    return ApiResponse.success(formData);
  }

  public Result getConfig(UUID customerUUID, UUID scopeUUID, boolean includeInherited) {
    LOG.trace(
      "customerUUID: {} scopeUUID: {} includeInherited: {}",
      customerUUID,
      scopeUUID,
      includeInherited);

    Optional<ScopedConfig> optScopedConfig = getScopedConfigInternal(customerUUID, scopeUUID);

    if (!optScopedConfig.isPresent()) {
      return ApiResponse.error(
        NOT_FOUND, String.format("No scope %s  found for customer %s", scopeUUID, customerUUID));
    }

    Config fullConfig =
      optScopedConfig.get().type.forScopeType(scopeUUID, settableRuntimeConfigFactory);
    Map<String, String> overriddenInScope = RuntimeConfigEntry.getAsMapForScope(scopeUUID);
    for (String k : mutableKeys) {
      boolean isOverridden = overriddenInScope.containsKey(k);
      LOG.trace(
        "key: {} overriddenInScope: {} includeInherited: {}", k, isOverridden, includeInherited);

      if (isOverridden) {
        optScopedConfig.get().configEntries.add(new ConfigEntry(false, k, fullConfig.getString(k)));
      } else if (includeInherited) {
        // Show entries even if not overridden in this scope. We will lookup value from fullConfig
        // for this scope
        optScopedConfig.get().configEntries.add(new ConfigEntry(true, k, fullConfig.getString(k)));
      }
    }

    return ApiResponse.success(optScopedConfig.get());
  }

  public Result getKey(UUID customerUUID, UUID scopeUUID, String path) {
    if (!mutableKeys.contains(path))
      return ApiResponse.error(NOT_FOUND, "No mutable key found: " + path);

    Optional<ScopedConfig> scopedConfig = getScopedConfigInternal(customerUUID, scopeUUID);

    if (!scopedConfig.isPresent()) {
      return ApiResponse.error(
        NOT_FOUND, String.format("No scope %s  found for customer %s", scopeUUID, customerUUID));
    }

    RuntimeConfigEntry runtimeConfigEntry = RuntimeConfigEntry.get(scopeUUID, path);
    if (runtimeConfigEntry == null)
      return ApiResponse.error(
        NOT_FOUND, String.format("Key %s is not defined in scope %s", path, scopeUUID));
    return ok(runtimeConfigEntry.getValue());
  }

  public Result setKey(UUID customerUUID, UUID scopeUUID, String path) {
    String newValue = request().body().asText();
    if (!mutableKeys.contains(path)) {
      return ApiResponse.error(NOT_FOUND, "No mutable key found: " + path);
    }
    Optional<ScopedConfig> optScopedConfig = getScopedConfigInternal(customerUUID, scopeUUID);

    if (!optScopedConfig.isPresent()) {
      return ApiResponse.error(
        NOT_FOUND, String.format("No scope %s  found for customer %s", scopeUUID, customerUUID));
    }

    if (!optScopedConfig.get().mutableScope)
      return ApiResponse.error(
        FORBIDDEN,
        "Customer "
          + customerUUID
          + "does not have access to mutate configuration for this scope "
          + scopeUUID);
    optScopedConfig
      .get()
      .type
      .forScopeType(scopeUUID, settableRuntimeConfigFactory)
      .setValue(path, newValue);

    return ok();
  }

  public Result deleteKey(UUID customerUUID, UUID scopeUUID, String path) {
    if (!mutableKeys.contains(path))
      return ApiResponse.error(NOT_FOUND, "No mutable key found: " + path);

    Optional<ScopedConfig> optScopedConfig = getScopedConfigInternal(customerUUID, scopeUUID);
    if (!optScopedConfig.isPresent()) {
      return ApiResponse.error(
        NOT_FOUND, String.format("No scope %s  found for customer %s", scopeUUID, customerUUID));
    }

    if (!optScopedConfig.get().mutableScope)
      return ApiResponse.error(
        FORBIDDEN,
        "Customer "
          + customerUUID
          + " does not have access to mutate configuration for this scope "
          + scopeUUID);
    optScopedConfig
      .get()
      .type
      .forScopeType(scopeUUID, settableRuntimeConfigFactory)
      .deleteEntry(path);
    return ok();
  }
}
