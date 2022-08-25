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

import com.yugabyte.yw.common.config.impl.MetricCollectionLevelValidator;
import com.yugabyte.yw.common.config.impl.SSH2EnabledKeyValidator;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;
import javax.inject.Inject;
import javax.inject.Singleton;

@Singleton
public class RuntimeConfigPreChangeNotifier {

  private final Map<String, RuntimeConfigPreChangeValidator> listenerMap = new HashMap<>();

  private void addListener(RuntimeConfigPreChangeValidator listener) {
    listenerMap.computeIfAbsent(listener.getKeyPath(), k -> listener);
  }

  @Inject
  public RuntimeConfigPreChangeNotifier(
      SSH2EnabledKeyValidator ssh2EnabledKeyValidator,
      MetricCollectionLevelValidator metricCollectionLevelValidator) {
    addListener(ssh2EnabledKeyValidator);
    addListener(metricCollectionLevelValidator);
  }

  public void notifyListeners(UUID scopeUUID, String path, String newValue) {
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
      } else {
        Universe.maybeGet(scopeUUID)
            .ifPresent(
                universe -> {
                  listener.validateConfigUniverse(universe, scopeUUID, path, newValue);
                });
      }
    }
  }
}
