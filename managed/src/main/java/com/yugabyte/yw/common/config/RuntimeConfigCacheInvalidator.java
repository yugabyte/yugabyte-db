// Copyright 2026 YugabyteDB, Inc. and Contributors

package com.yugabyte.yw.common.config;

import jakarta.inject.Inject;
import jakarta.inject.Singleton;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Singleton
public final class RuntimeConfigCacheInvalidator {

  private static volatile RuntimeConfigCacheInvalidator instance;

  private final RuntimeConfigFactory runtimeConfigFactory;

  @Inject
  public RuntimeConfigCacheInvalidator(RuntimeConfigFactory runtimeConfigFactory) {
    this.runtimeConfigFactory = runtimeConfigFactory;
    instance = this;
  }

  public void invalidateScope(UUID scopeUuid) {
    runtimeConfigFactory.invalidateScope(scopeUuid);
  }

  public void invalidateAllScopes() {
    runtimeConfigFactory.invalidateAllScopes();
  }

  public static void invalidateScopeForDeletedEntity(UUID scopeUuid) {
    RuntimeConfigCacheInvalidator invalidator = instance;

    if (invalidator != null) {
      invalidator.invalidateScope(scopeUuid);
    }
  }

  public static void invalidateAllScopesForCustomer() {
    RuntimeConfigCacheInvalidator invalidator = instance;

    if (invalidator != null) {
      invalidator.invalidateAllScopes();
    }
  }
}
