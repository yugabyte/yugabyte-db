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
import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.impl.EnableRollbackSupportKeyValidator;
import com.yugabyte.yw.common.config.impl.MetricCollectionLevelValidator;
import com.yugabyte.yw.common.config.impl.OidcEnabledKeyValidator;
import com.yugabyte.yw.common.config.impl.SSH2EnabledKeyValidator;
import com.yugabyte.yw.common.config.impl.UseNewRbacAuthzValidator;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;

@Singleton
@Slf4j
public class RuntimeConfigPreChangeNotifier {

  private final Map<String, RuntimeConfigPreChangeValidator> listenerMap = new HashMap<>();

  @Inject Map<String, ConfKeyInfo<?>> keyMetaData;

  @Inject RuntimeConfGetter confGetter;

  private void addListener(RuntimeConfigPreChangeValidator listener) {
    listenerMap.computeIfAbsent(listener.getKeyPath(), k -> listener);
  }

  @Inject
  public RuntimeConfigPreChangeNotifier(
      SSH2EnabledKeyValidator ssh2EnabledKeyValidator,
      MetricCollectionLevelValidator metricCollectionLevelValidator,
      UseNewRbacAuthzValidator useNewRbacAuthzValidator,
      EnableRollbackSupportKeyValidator enableRollbackSupportKeyValidator,
      OidcEnabledKeyValidator oidcEnabledKeyValidator) {
    addListener(ssh2EnabledKeyValidator);
    addListener(metricCollectionLevelValidator);
    addListener(useNewRbacAuthzValidator);
    addListener(enableRollbackSupportKeyValidator);
    addListener(oidcEnabledKeyValidator);
  }

  public void notifyListenersDeleteConfig(UUID scopeUUID, String path) {
    if (!listenerMap.containsKey(path)) {
      return;
    }
    RuntimeConfigPreChangeValidator listener = listenerMap.get(path);
    listener.validateDeleteConfig(scopeUUID, path);
  }

  public void notifyListeners(UUID scopeUUID, String path, String newValue) {

    if (keyMetaData.containsKey(path)) {
      maybeValidateMetadata(scopeUUID, path, newValue);
    } else {
      log.warn("No metadata for key {} being set", path);
    }

    if (!listenerMap.containsKey(path)) {
      return;
    }
    RuntimeConfigPreChangeValidator listener = listenerMap.get(path);
    if (scopeUUID.equals(GLOBAL_SCOPE_UUID)) {
      listener.validateConfigGlobal(scopeUUID, path, newValue);
    } else {
      Customer customer = Customer.get(scopeUUID);
      if (customer != null) {
        listener.validateConfigCustomer(customer, scopeUUID, path, newValue);
        return;
      }
      Provider provider = Provider.get(scopeUUID);
      if (provider != null) {
        listener.validateConfigProvider(provider, scopeUUID, path, newValue);
        return;
      }
      Universe.maybeGet(scopeUUID)
          .ifPresent(
              universe -> {
                listener.validateConfigUniverse(universe, scopeUUID, path, newValue);
              });
    }
  }

  private void maybeValidateMetadata(UUID scopeUUID, String path, String newValue) {
    boolean validation = confGetter.getGlobalConf(GlobalConfKeys.dataValidationEnabled);

    if (validation) {
      keyMetaData.get(path).getDataType().getParser().apply(newValue);
    } else {
      log.debug("Data validation disabled");
    }

    boolean scopeStrictness = confGetter.getGlobalConf(GlobalConfKeys.scopeStrictnessEnabled);

    if (scopeStrictness) {
      ConfKeyInfo<?> keyInfo = keyMetaData.get(path);
      if (!keyInfo.getScope().isValid(scopeUUID)) {
        throw new PlatformServiceException(BAD_REQUEST, "Cannot set the key in this scope");
      }
    } else {
      log.debug("Scope strictness disabled");
    }
  }
}
