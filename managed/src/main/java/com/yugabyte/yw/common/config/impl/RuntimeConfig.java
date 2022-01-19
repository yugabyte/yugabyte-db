/*
 * Copyright 2021 YugaByte, Inc. and Contributors
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
import com.typesafe.config.ConfigValueFactory;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.RuntimeConfigEntry;
import com.yugabyte.yw.models.Universe;
import io.ebean.Model;
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

  public RuntimeConfig(Config config) {
    this(null, config);
  }

  RuntimeConfig(M scope, Config config) {
    super(config.resolve());
    this.scope = scope;
  }

  /**
   * @return modify single path in underlying scoped config in the database and return modified
   *     config view.
   */
  public RuntimeConfig<M> setValue(String path, String strValue, boolean isObject) {
    if (scope == null) {
      RuntimeConfigEntry.upsertGlobal(path, strValue);
    } else if (scope instanceof Customer) {
      RuntimeConfigEntry.upsert((Customer) scope, path, strValue);
    } else if (scope instanceof Universe) {
      RuntimeConfigEntry.upsert((Universe) scope, path, strValue);
    } else if (scope instanceof Provider) {
      RuntimeConfigEntry.upsert((Provider) scope, path, strValue);
    } else {
      throw new UnsupportedOperationException("Unsupported Scope: " + scope);
    }
    if (isObject) {
      super.setObjInternal(path, strValue);
    } else {
      super.setValueInternal(path, ConfigValueFactory.fromAnyRef(strValue));
    }
    LOG.trace("After setValue {}", this);
    return this;
  }

  public RuntimeConfig<M> deleteEntry(String path) {
    if (scope == null) {
      RuntimeConfigEntry.getOrBadRequest(GLOBAL_SCOPE_UUID, path).delete();
    } else if (scope instanceof Customer) {
      RuntimeConfigEntry.getOrBadRequest(((Customer) scope).uuid, path).delete();
    } else if (scope instanceof Universe) {
      RuntimeConfigEntry.getOrBadRequest(((Universe) scope).universeUUID, path).delete();
    } else if (scope instanceof Provider) {
      RuntimeConfigEntry.getOrBadRequest(((Provider) scope).uuid, path).delete();
    } else {
      throw new UnsupportedOperationException("Unsupported Scope: " + scope);
    }
    super.deleteValueInternal(path);
    LOG.trace("After setValue {}", this);
    return this;
  }
}
