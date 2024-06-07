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
import java.util.UUID;

public interface RuntimeConfigPreChangeValidator {
  String getKeyPath();

  default void validateConfigGlobal(UUID scopeUUID, String path, String newValue) {
    validateAny(path, newValue);
  }

  default void validateConfigCustomer(
      Customer customer, UUID scopeUUID, String path, String newValue) {
    validateAny(path, newValue);
  }

  default void validateConfigUniverse(
      Universe universe, UUID scopeUUID, String path, String newValue) {
    validateAny(path, newValue);
  }

  default void validateConfigProvider(
      Provider provider, UUID scopeUUID, String path, String newValue) {
    validateAny(path, newValue);
  }

  default void validateDeleteConfig(UUID scopeUUID, String path) {
    // Override in each validator listener if required.
  }

  default void validateAny(String path, String newValue) {
    // Do nothing
  }
}
