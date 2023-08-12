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

import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;

public interface RuntimeConfigChangeListener {
  String getKeyPath();

  default void processGlobal() {
    // Do nothing by default
  }

  default void processCustomer(Customer customer) {
    // Do nothing by default
  }

  default void processUniverse(Universe universe) {
    // Do nothing by default
  }

  default void processProvider(Provider provider) {
    // Do nothing by default
  }
}
