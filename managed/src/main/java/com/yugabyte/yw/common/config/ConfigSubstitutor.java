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

import com.typesafe.config.Config;
import com.typesafe.config.ConfigException;
import com.yugabyte.yw.common.templates.PlaceholderSubstitutor;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

// Note: Moved from RuntimeConfig.apply() written by @spotachev
/**
 * Replace string template config keys by values from the config.
 */
public class ConfigSubstitutor extends PlaceholderSubstitutor {
  private static final Logger LOG = LoggerFactory.getLogger(ConfigSubstitutor.class);

  public ConfigSubstitutor(Config config) {
    super("{{", "}}", key -> {
      try {
        return config.getString(key);
      } catch (ConfigException.Missing e) {
        LOG.warn("Parameter '{}' not found", key);
      }
      return "";
    });
  }
}
