/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.forms;

import com.yugabyte.yw.common.config.impl.RuntimeConfig;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import io.ebean.Model;
import io.ebean.annotation.EnumValue;

import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;

import static com.yugabyte.yw.models.ScopedRuntimeConfig.GLOBAL_SCOPE_UUID;

@ApiModel(value = "Runtime config data", description = "Runtime config data")
public class RuntimeConfigFormData {

  @ApiModelProperty(value = "list of scoped config")
  public final List<ScopedConfig> scopedConfigList = new ArrayList<>();

  public void addGlobalScope(boolean asSuperAdmin) {
    scopedConfigList.add(
        new ScopedConfig(ScopedConfig.ScopeType.GLOBAL, GLOBAL_SCOPE_UUID, asSuperAdmin));
  }

  public void addMutableScope(ScopedConfig.ScopeType type, UUID uuid) {
    scopedConfigList.add(new ScopedConfig(type, uuid, true));
  }

  @ApiModel(value = "Scoped config", description = "Scoped config")
  public static class ScopedConfig {
    @ApiModelProperty(value = "Scope type")
    public final ScopeType type;

    @ApiModelProperty(value = "Scope UIID")
    public final UUID uuid;
    /**
     * global scope is mutable only if user is super admin other scopes can be mutated by the
     * customer
     */
    @ApiModelProperty(value = "Is scope mutable")
    public final boolean mutableScope;

    @ApiModelProperty(value = "List of configs")
    public final List<ConfigEntry> configEntries = new ArrayList<>();

    public ScopedConfig(ScopeType type, UUID uuid) {
      this(type, uuid, true);
    }

    public ScopedConfig(ScopeType type, UUID uuid, boolean mutableScope) {
      this.type = type;
      this.uuid = uuid;
      this.mutableScope = mutableScope;
    }

    public enum ScopeType {
      @EnumValue("GLOBAL")
      GLOBAL,
      @EnumValue("CUSTOMER")
      CUSTOMER,
      @EnumValue("UNIVERSE")
      UNIVERSE,
      @EnumValue("PROVIDER")
      PROVIDER;

      public RuntimeConfig<? extends Model> forScopeType(
          UUID scopeUUID, SettableRuntimeConfigFactory factory) {
        switch (this) {
          case GLOBAL:
            return factory.globalRuntimeConf();
          case CUSTOMER:
            return factory.forCustomer(Customer.get(scopeUUID));
          case UNIVERSE:
            return factory.forUniverse(Universe.getOrBadRequest(scopeUUID));
          case PROVIDER:
            return factory.forProvider(Provider.get(scopeUUID));
        }
        return null;
      }
    }
  }

  @ApiModel(value = "Configs entry", description = "Configs entry")
  public static class ConfigEntry {
    /**
     * When includeInherited is true; we will return inherited entries. For example a key may not be
     * defined in customer scope but may be defined in global scope will be returned with inherited
     * set to true.
     */
    @ApiModelProperty(value = "Is config inherited")
    public final boolean inherited;

    @ApiModelProperty(value = "Config key")
    public final String key;

    @ApiModelProperty(value = "Config value")
    public final String value;

    public ConfigEntry(boolean inherited, String key, String value) {
      this.inherited = inherited;
      this.key = key;
      this.value = value;
    }
  }
}
