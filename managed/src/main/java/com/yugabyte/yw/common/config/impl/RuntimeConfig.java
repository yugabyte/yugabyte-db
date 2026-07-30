/*
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common.config.impl;

import static com.yugabyte.yw.models.ScopedRuntimeConfig.GLOBAL_SCOPE_UUID;

import com.typesafe.config.Config;
import com.typesafe.config.ConfigFactory;
import com.typesafe.config.ConfigValue;
import com.typesafe.config.ConfigValueFactory;
import com.yugabyte.yw.forms.RuntimeConfigFormData.ScopedConfig.ScopeType;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.RuntimeConfigEntry;
import com.yugabyte.yw.models.Universe;
import io.ebean.Model;
import java.util.function.BiConsumer;
import java.util.function.Supplier;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * This class implements most (but not all) methods of com.typesafe.config.Config In addition this
 * also provides for mutating the config using `setValue(path, value)` method. Any mutations will be
 * persisted to database.
 */
public class RuntimeConfig<M extends Model> extends DelegatingConfig {
  private static final Logger LOG = LoggerFactory.getLogger(RuntimeConfig.class);

  private final M scope;

  // Listener to any changes to this runtime config.
  private final BiConsumer<ScopeType, RuntimeConfigEntry> changeListener;

  public RuntimeConfig(
      Config resolvedConfig, BiConsumer<ScopeType, RuntimeConfigEntry> changeListener) {
    this(null, resolvedConfig, changeListener);
  }

  RuntimeConfig(
      M scope, Config resolvedConfig, BiConsumer<ScopeType, RuntimeConfigEntry> changeListener) {
    super(resolvedConfig);
    this.scope = scope;
    this.changeListener = changeListener;
  }

  /**
   * @return modify single leaf path as any ref in underlying scoped config in the database and
   *     return modified config view.
   */
  public RuntimeConfig<M> setValue(String path, String strValue) {
    return setValueOrObj(path, strValue, () -> ConfigValueFactory.fromAnyRef(strValue));
  }

  /**
   * @return modify single leaf path as HOCON string in underlying scoped config in the database and
   *     return modified config view.
   */
  public RuntimeConfig<M> setObject(String path, String strValue) {
    return setValueOrObj(
        path, strValue, () -> ConfigFactory.parseString(path + "=" + strValue).getValue(path));
  }

  private RuntimeConfig<M> setValueOrObj(
      String path, String strValue, Supplier<ConfigValue> valueSupplier) {
    RuntimeConfigEntry entry = null;
    ScopeType scopeType = ScopeType.GLOBAL;
    if (scope == null) {
      entry = RuntimeConfigEntry.upsertGlobal(path, strValue);
    } else if (scope instanceof Customer) {
      scopeType = ScopeType.CUSTOMER;
      entry = RuntimeConfigEntry.upsert((Customer) scope, path, strValue);
    } else if (scope instanceof Universe) {
      scopeType = ScopeType.UNIVERSE;
      entry = RuntimeConfigEntry.upsert((Universe) scope, path, strValue);
    } else if (scope instanceof Provider) {
      scopeType = ScopeType.PROVIDER;
      entry = RuntimeConfigEntry.upsert((Provider) scope, path, strValue);
    } else {
      throw new UnsupportedOperationException("Unsupported Scope: " + scope);
    }
    super.setValueInternal(path, valueSupplier.get());
    if (entry != null) {
      changeListener.accept(scopeType, entry);
    }
    LOG.trace("After setValue {}", this);
    return this;
  }

  public RuntimeConfig<M> deleteEntry(String path) {
    RuntimeConfigEntry entry = null;
    ScopeType scopeType = ScopeType.GLOBAL;
    if (scope == null) {
      entry = RuntimeConfigEntry.getOrBadRequest(GLOBAL_SCOPE_UUID, path);
    } else if (scope instanceof Customer) {
      scopeType = ScopeType.CUSTOMER;
      entry = RuntimeConfigEntry.getOrBadRequest(((Customer) scope).getUuid(), path);
    } else if (scope instanceof Universe) {
      scopeType = ScopeType.UNIVERSE;
      entry = RuntimeConfigEntry.getOrBadRequest(((Universe) scope).getUniverseUUID(), path);
    } else if (scope instanceof Provider) {
      scopeType = ScopeType.PROVIDER;
      entry = RuntimeConfigEntry.getOrBadRequest(((Provider) scope).getUuid(), path);
    } else {
      throw new UnsupportedOperationException("Unsupported Scope: " + scope);
    }
    super.deleteValueInternal(path);
    if (entry != null) {
      entry.delete();
      changeListener.accept(scopeType, entry);
    }
    LOG.trace("After deleteEntry {}", this);
    return this;
  }
}
