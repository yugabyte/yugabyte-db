/*
 * Copyright 2022 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common.config;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.RuntimeConfigFormData.ScopedConfig.ScopeType;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import java.util.Map;

@Singleton
public class RuntimeConfGetter {

  private final Map<String, ConfKeyInfo<?>> keyMetaData;
  private final RuntimeConfigFactory runtimeConfigFactory;

  @Inject
  public RuntimeConfGetter(
      RuntimeConfigFactory runtimeConfigFactory, Map<String, ConfKeyInfo<?>> keyMetaData) {
    this.runtimeConfigFactory = runtimeConfigFactory;
    this.keyMetaData = keyMetaData;
  }

  public <T> T getConfForScope(Customer customer, ConfKeyInfo<T> keyInfo) {
    if (keyInfo.scope != ScopeType.CUSTOMER) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Key " + keyInfo.getKey() + " isn't defined in Customer scope");
    }
    return keyInfo
        .getDataType()
        .getGetter()
        .apply(runtimeConfigFactory.forCustomer(customer), keyInfo.key);
  }

  public <T> T getConfForScope(Universe universe, ConfKeyInfo<T> keyInfo) {
    if (keyInfo.scope != ScopeType.UNIVERSE) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Key " + keyInfo.getKey() + " isn't defined in Universe scope");
    }
    return keyInfo
        .getDataType()
        .getGetter()
        .apply(runtimeConfigFactory.forUniverse(universe), keyInfo.key);
  }

  public <T> T getConfForScope(Provider provider, ConfKeyInfo<T> keyInfo) {
    if (keyInfo.scope != ScopeType.PROVIDER) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Key " + keyInfo.getKey() + " isn't defined in Provider scope");
    }
    return keyInfo
        .getDataType()
        .getGetter()
        .apply(runtimeConfigFactory.forProvider(provider), keyInfo.key);
  }

  public <T> T getGlobalConf(ConfKeyInfo<T> keyInfo) {
    if (keyInfo.scope != ScopeType.GLOBAL) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Key " + keyInfo.getKey() + " isn't defined in Global scope");
    }
    return keyInfo
        .getDataType()
        .getGetter()
        .apply(runtimeConfigFactory.globalRuntimeConf(), keyInfo.key);
  }

  public Config getStaticConf() {
    return runtimeConfigFactory.staticApplicationConf();
  }

  public Config getGlobalConf() {
    return runtimeConfigFactory.globalRuntimeConf();
  }

  public Config getProviderConf(Provider p) {
    return runtimeConfigFactory.forProvider(p);
  }

  public Config getCustomerConf(Customer c) {
    return runtimeConfigFactory.forCustomer(c);
  }

  public Config getUniverseConf(Universe u) {
    return runtimeConfigFactory.forUniverse(u);
  }

  @SuppressWarnings("unchecked")
  public <T> ConfKeyInfo<T> getConfKeyInfo(String path, Class<T> type) {
    return (ConfKeyInfo<T>) keyMetaData.get(path);
  }
}
