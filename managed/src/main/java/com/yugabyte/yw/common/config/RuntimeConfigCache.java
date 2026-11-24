/*
 * Copyright 2023 YugabyteDB, Inc. and Contributors
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
import io.ebean.DB;
import io.ebean.Transaction;
import io.ebean.TransactionCallbackAdapter;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.Callable;
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
          GlobalConfKeys.blockOperatorApiResources.getKey(),
          GlobalConfKeys.enableGFlagsSensitiveDataApiRedaction.getKey());

  // The change listener clears a key, and clears it again when the transaction that writes it ends.
  // Writes that don't notify the listener, such as RuntimeConfig.setValue or a database restore,
  // leave the old value cached until the next change. An expiry bounds that for the gflags API
  // redaction key, which turns API redaction and the REDACTED merge-back on or off.
  private static final Map<String, Integer> EXPIRY_SECONDS =
      Map.of(GlobalConfKeys.enableGFlagsSensitiveDataApiRedaction.getKey(), 60);

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
              removeNowAndAfterTransaction(key);
            }
          });
    }
  }

  // The listener runs inside the transaction that writes the key, before the commit, and a read on
  // another connection in between loads the old value. Clearing the key again when the transaction
  // ends drops that value. A load still running then isn't kept either: the cache stores a load's
  // value only if the key still maps to that load when it completes.
  private void removeNowAndAfterTransaction(String key) {
    cache.remove(key);
    Transaction transaction = DB.currentTransaction();
    if (transaction != null && transaction.isActive()) {
      transaction.register(
          new TransactionCallbackAdapter() {
            @Override
            public void postCommit() {
              cache.remove(key);
            }

            @Override
            public void postRollback() {
              cache.remove(key);
            }
          });
    }
  }

  /**
   * Drop all cached values so subsequent reads reload from the DB. Primarily useful for tests that
   * reuse a single application across methods and mutate runtime config directly (bypassing the
   * change notifier that would otherwise invalidate this cache).
   */
  public void invalidateCache() {
    for (String key : cachedGlobalKeys) {
      cache.remove(key);
    }
  }

  // Get preferably from the cache. If not present, then fetch from the DB.
  public boolean getBoolean(String key) {
    if (!cachedGlobalKeys.contains(key)) {
      throw new RuntimeException(key + " is not cached with RuntimeConfigCache");
    }
    Callable<Boolean> fetch =
        () -> {
          log.info("Fetching {} from DB", key);
          return runtimeConfigFactory.globalRuntimeConf().getBoolean(key);
        };
    Integer expirySeconds = EXPIRY_SECONDS.get(key);
    return expirySeconds == null
        ? cache.getOrElseUpdate(key, fetch)
        : cache.getOrElseUpdate(key, fetch, expirySeconds);
  }

  // Add more getters for other types here as needed

}
