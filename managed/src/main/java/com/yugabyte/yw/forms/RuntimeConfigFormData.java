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

import static com.yugabyte.yw.models.ScopedRuntimeConfig.GLOBAL_SCOPE_UUID;

import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.impl.RuntimeConfig;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import io.ebean.Model;
import io.ebean.annotation.EnumValue;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import play.mvc.Http;

@ApiModel(value = "RuntimeConfigData", description = "Runtime configuration data")
public class RuntimeConfigFormData {

  @ApiModelProperty(value = "List of scoped configurations")
  public final List<ScopedConfig> scopedConfigList = new ArrayList<>();

  public void addGlobalScope(boolean asSuperAdmin) {
    scopedConfigList.add(
        new ScopedConfig(ScopedConfig.ScopeType.GLOBAL, GLOBAL_SCOPE_UUID, asSuperAdmin));
  }

  public void addMutableScope(ScopedConfig.ScopeType type, UUID uuid) {
    scopedConfigList.add(new ScopedConfig(type, uuid, true));
  }

  @ApiModel(description = "Scoped configuration")
  public static class ScopedConfig {
    @ApiModelProperty(value = "Scope type")
    public final ScopeType type;

    @ApiModelProperty(value = "Scope UIID")
    public final UUID uuid;

    /**
     * global scope is mutable only if user is super admin other scopes can be mutated by the
     * customer
     */
    @ApiModelProperty(
        value =
            "Mutability of the scope. Only super admin users can change global scope; other scopes"
                + " are customer-mutable.")
    public final boolean mutableScope;

    @ApiModelProperty(value = "List of configurations")
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
      GLOBAL {
        @Override
        public boolean isValid(UUID scopeUUID) {
          return scopeUUID.equals(GLOBAL_SCOPE_UUID);
        }
      },
      @EnumValue("CUSTOMER")
      CUSTOMER {
        @Override
        public boolean isValid(UUID scopeUUID) {
          return (scopeUUID.equals(GLOBAL_SCOPE_UUID) || Customer.get(scopeUUID) != null);
        }
      },
      @EnumValue("UNIVERSE")
      UNIVERSE {
        @Override
        public boolean isValid(UUID scopeUUID) {
          return !Provider.maybeGet(scopeUUID).isPresent();
        }
      },
      @EnumValue("PROVIDER")
      PROVIDER {
        @Override
        public boolean isValid(UUID scopeUUID) {
          return !Universe.maybeGet(scopeUUID).isPresent();
        }
      };

      public abstract boolean isValid(UUID scopeUUID);
    }

    public RuntimeConfig<? extends Model> runtimeConfig(SettableRuntimeConfigFactory factory) {
      switch (type) {
        case GLOBAL:
          return factory.globalRuntimeConf();
        case CUSTOMER:
          return factory.forCustomer(Customer.get(uuid));
        case UNIVERSE:
          return factory.forUniverse(Universe.getOrBadRequest(uuid));
        case PROVIDER:
          return factory.forProvider(Provider.get(uuid));
      }
      throw new PlatformServiceException(
          Http.Status.INTERNAL_SERVER_ERROR, "Unexpected Type " + type);
    }
  }

  @ApiModel(description = "Configuration entry")
  public static class ConfigEntry {
    /**
     * When includeInherited is true; we will return inherited entries. For example a key may not be
     * defined in customer scope but may be defined in global scope will be returned with inherited
     * set to true.
     */
    @ApiModelProperty(value = "Is this configuration inherited?")
    public final boolean inherited;

    @ApiModelProperty(value = "Configuration key")
    public final String key;

    @ApiModelProperty(value = "Configuration value")
    public final String value;

    public ConfigEntry(boolean inherited, String key, String value) {
      this.inherited = inherited;
      this.key = key;
      this.value = value;
    }
  }
}
