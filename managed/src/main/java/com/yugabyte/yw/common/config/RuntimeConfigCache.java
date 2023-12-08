/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common.config;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import java.util.Set;
import lombok.extern.slf4j.Slf4j;
import play.cache.NamedCache;
import play.cache.SyncCacheApi;

/**
 * For most purposes the runtime config values can be fetched directly using the getters in
 * RuntimeConfigFactory. This will fetch the config state from the YBA DB. This class provides a
 * cache for performance critical paths that want to avoid a DB read and instead use an in-memory
 * cached value. For example, when a runtime feature flag needs to be consulted for every API call.
 */
@Singleton
@Slf4j
public class RuntimeConfigCache {
  // This class utilizes the caffeine cache under the hoods
  @Inject
  @NamedCache("runtimeConfigsCache")
  private SyncCacheApi cache;

  @Inject private RuntimeConfigFactory runtimeConfigFactory;
  private RuntimeConfigChangeNotifier runtimeConfigChangeNotifier;
  // Add any key that needs to be cached below. Other keys will not be cached.
  private Set<String> cachedGlobalKeys =
      Set.of(
          GlobalConfKeys.useNewRbacAuthz.getKey(),
          GlobalConfKeys.ybaApiStrictMode.getKey(),
          GlobalConfKeys.ybaApiSafeMode.getKey(),
          GlobalConfKeys.blockOperatorApiResources.getKey());

  @Inject
  public RuntimeConfigCache(RuntimeConfigChangeNotifier runtimeConfigChangeNotifier) {
    this.runtimeConfigChangeNotifier = runtimeConfigChangeNotifier;
    initializeChangeListener();
  }

  // Add a listener to clear the cache whenever the key is changed in the DB.
  private void initializeChangeListener() {
    for (String key : cachedGlobalKeys) {
      runtimeConfigChangeNotifier.addListener(
          new RuntimeConfigChangeListener() {
            public String getKeyPath() {
              return key;
            }

            public void processGlobal() {
              log.info("Removed {} from cache", key);
              cache.remove(key);
            }
          });
    }
  }

  // Get preferably from the cache. If not present, then fetch from the DB.
  public boolean getBoolean(String key) {
    if (!cachedGlobalKeys.contains(key)) {
      throw new RuntimeException(key + " is not cached with RuntimeConfigCache");
    }
    return cache.getOrElseUpdate(
        key,
        () -> {
          log.info("Fetching {} from DB", key);
          return runtimeConfigFactory.globalRuntimeConf().getBoolean(key);
        });
  }

  // Add more getters for other types here as needed

}
