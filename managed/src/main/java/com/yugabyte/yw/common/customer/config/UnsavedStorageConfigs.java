// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.customer.config;

import com.yugabyte.yw.models.configs.CustomerConfig;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import javax.inject.Singleton;

/**
 * Storage configs that a ValidateStorageConfig dry run checks without saving them, handed from the
 * request to the task in memory. Task params are stored in task_info as plain JSON, so an unsaved
 * config, with its secrets, must not go into them. A config left here by a task that never ran is
 * lost with the process, and a task that doesn't find its config fails.
 */
@Singleton
public class UnsavedStorageConfigs {

  private final Map<UUID, CustomerConfig> configs = new ConcurrentHashMap<>();

  /** Keeps the config for the task, under a new key that the task params carry. */
  public UUID put(CustomerConfig config) {
    UUID key = UUID.randomUUID();
    configs.put(key, config);
    return key;
  }

  /** Returns the config kept under the key, or null if there is none. */
  public CustomerConfig get(UUID key) {
    return configs.get(key);
  }

  public void remove(UUID key) {
    configs.remove(key);
  }
}
