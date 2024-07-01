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

import com.google.common.annotations.VisibleForTesting;
import com.yugabyte.yw.common.AppConfigHelper;
import com.yugabyte.yw.common.config.impl.*;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import java.util.*;
import javax.inject.Inject;
import javax.inject.Singleton;

@Singleton
public class RuntimeConfigChangeNotifier {

  private final Map<String, List<RuntimeConfigChangeListener>> listenerMap = new HashMap<>();

  @VisibleForTesting
  public void addListener(RuntimeConfigChangeListener listener) {
    listenerMap.computeIfAbsent(listener.getKeyPath(), k -> new ArrayList<>()).add(listener);
  }

  @Inject
  public RuntimeConfigChangeNotifier(
      MetricCollectionLevelListener metricCollectionLevelListener,
      MonitoredMountRootsListener monitoredMountRootsListener,
      UseNewRbacAuthzListener useNewRbacAuthzListener,
      BootstrapRequiredRpcPoolMaxThreadListener bootstrapRequiredRpcPoolMaxThreadListener,
      CpuUsageIntervalListener cpuUsageIntervalListener) {
    List<String> refreshableClients = AppConfigHelper.getRefreshableClients();
    for (String wsClientKey : refreshableClients) {
      addListener(new WSClientKeyListener(wsClientKey));
    }
    addListener(metricCollectionLevelListener);
    addListener(monitoredMountRootsListener);
    addListener(useNewRbacAuthzListener);
    addListener(bootstrapRequiredRpcPoolMaxThreadListener);
    addListener(cpuUsageIntervalListener);
  }

  public void notifyListeners(UUID scopeUUID, String path) {
    if (!listenerMap.containsKey(path)) {
      return;
    }
    List<RuntimeConfigChangeListener> listeners = listenerMap.get(path);
    if (scopeUUID.equals(GLOBAL_SCOPE_UUID)) {
      listeners.forEach(RuntimeConfigChangeListener::processGlobal);
    } else {
      Customer customer = Customer.get(scopeUUID);
      if (customer != null) {
        listeners.forEach(listener -> listener.processCustomer(customer));
        return;
      }
      Provider provider = Provider.get(scopeUUID);
      if (provider != null) {
        listeners.forEach(listener -> listener.processProvider(provider));
        return;
      }
      Universe.maybeGet(scopeUUID)
          .ifPresent(
              universe -> {
                listeners.forEach(listener -> listener.processUniverse(universe));
              });
    }
  }
}
