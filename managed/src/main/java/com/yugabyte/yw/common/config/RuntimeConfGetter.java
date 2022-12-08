/*
 * Copyright 2022 YugaByte, Inc. and Contributors
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
import com.yugabyte.yw.models.Customer;

@Singleton
public class RuntimeConfGetter {
  private final RuntimeConfigFactory runtimeConfigFactory;

  @Inject
  public RuntimeConfGetter(
      RuntimeConfigFactory runtimeConfigFactory, CustomerConfKeys customerKeys) {
    this.runtimeConfigFactory = runtimeConfigFactory;
  }

  public <T> T getConfForScope(Customer customer, ConfKeyInfo<T> keyInfo) {
    return keyInfo
        .getDataType()
        .getGetter()
        .apply(runtimeConfigFactory.forCustomer(customer), keyInfo.key);
  }
  // TODO: Overloads for other scopes
}
