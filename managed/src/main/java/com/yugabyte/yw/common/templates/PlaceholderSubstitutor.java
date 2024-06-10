/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common.templates;

import java.util.function.Function;

/**
 * Replace placeholders, surrounded by prefix and suffix strings with some values from domain model.
 */
public class PlaceholderSubstitutor implements PlaceholderSubstitutorIF {
  private final String parameterPrefix;
  private final String parameterSuffix;
  private final Function<String, String> valueProvider;

  public PlaceholderSubstitutor(Function<String, String> valueProvider) {
    this("{{", "}}", valueProvider);
  }

  public PlaceholderSubstitutor(
      String parameterPrefix, String parameterSuffix, Function<String, String> valueProvider) {
    this.parameterPrefix = parameterPrefix;
    this.parameterSuffix = parameterSuffix;
    this.valueProvider = valueProvider;
  }

  /**
   * Substitutes all parameters of format '{{ config-path }}' with corresponding values from the
   * configuration.<br>
   * If the parameter doesn't exist in the configuration, it is simply removed from the string.
   *
   * <p><b>Usage example:</b> <br>
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
      int suffixPosition =
          templateStr.indexOf(parameterSuffix, prefixPosition + parameterPrefix.length());
      if (suffixPosition == -1) {
        break;
      }
      int nextPrefixPosition =
          templateStr.indexOf(parameterPrefix, prefixPosition + parameterPrefix.length());
      if (nextPrefixPosition > 0 && nextPrefixPosition < suffixPosition) {
        // we need to find prefix right before suffix
        result.append(templateStr, position, nextPrefixPosition);
        position = nextPrefixPosition;
        continue;
      }
      result.append(templateStr, position, prefixPosition);
      String parameter =
          templateStr.substring(prefixPosition + parameterPrefix.length(), suffixPosition);
      parameter = parameter.trim();
      result.append(valueProvider.apply(parameter));
      position = suffixPosition + parameterSuffix.length();
    }
    // Copying tail.
    result.append(templateStr.substring(position));
    return result.toString();
  }
}
