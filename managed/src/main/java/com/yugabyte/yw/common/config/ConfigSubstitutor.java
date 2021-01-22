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
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

// Note: Moved from RuntimeConfig.apply() written by @spotachev
/**
 * Replace string template config keys by values from the config.
 */
public class ConfigSubstitutor {
  private static final Logger LOG = LoggerFactory.getLogger(ConfigSubstitutor.class);

  private final String parameterPrefix;
  private final String parameterSuffix;
  private final Config config;

  public ConfigSubstitutor(Config config) {
    this("{{", "}}", config);
  }

  public ConfigSubstitutor(String parameterPrefix, String parameterSuffix, Config config) {
    this.parameterPrefix = parameterPrefix;
    this.parameterSuffix = parameterSuffix;
    this.config = config;
  }

  /**
   * Substitutes all parameters of format '{{ config-path }}' with corresponding
   * values from the configuration.<br>
   * If the parameter doesn't exist in the configuration, it is simply removed
   * from the string.
   * <p>
   * <b>Usage example:</b> <br>
   * "database name is: {{ db.default.dbname }}" => "database name is: yugaware"
   *
   * @return String with substituted values
   */
  public String replace(String templateStr) {
    if (templateStr == null) {
      return null;
    }

    StringBuilder result = new StringBuilder();
    int position = 0;
    int prefixPosition;
    while ((prefixPosition = templateStr.indexOf(parameterPrefix, position)) >= 0) {
      int suffixPosition = templateStr.indexOf(parameterSuffix,
        prefixPosition + parameterPrefix.length());
      if (suffixPosition == -1) {
        break;
      }
      result.append(templateStr, position, prefixPosition);
      String parameter = templateStr.substring(prefixPosition + parameterPrefix.length(),
        suffixPosition);
      parameter = parameter.trim();
      try {
        result.append(config.getString(parameter));
      } catch (ConfigException.Missing e) {
        LOG.warn("Parameter '{}' not found", parameter);
      }
      position = suffixPosition + parameterSuffix.length();
    }
    // Copying tail.
    result.append(templateStr.substring(position));
    return result.toString();
  }
}
