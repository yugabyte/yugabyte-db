/*
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.models.helpers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import lombok.Getter;
import org.apache.commons.io.IOUtils;
import org.apache.commons.lang3.StringUtils;
import play.Environment;
import play.libs.Json;

@Getter
public enum MetricCollectionLevel {
  ALL(StringUtils.EMPTY, false),
  NORMAL("metric/normal_level_params.json", false),
  MINIMAL("metric/minimal_level_params.json", false),
  TABLE_OFF("metric/table_off_level_params.json", false),
  OFF(StringUtils.EMPTY, true);

  private final String paramsFilePath;
  private final boolean disableCollection;

  MetricCollectionLevel(String paramsFilePath, boolean disableCollection) {
    this.paramsFilePath = paramsFilePath;
    this.disableCollection = disableCollection;
  }

  public static MetricCollectionLevel fromString(String level) {
    if (level == null) {
      throw new IllegalArgumentException("Level can't be null");
    }
    return MetricCollectionLevel.valueOf(level.toUpperCase());
  }

  /**
   * Parses the params file for this metric collection level and returns a map of parameter names to
   * their string values. For array values, they are joined into a single string.
   *
   * @param environment the play environment for resource access
   * @return a map of parameter names to their string values, or empty map if no params file
   * @throws RuntimeException if there's an error reading or processing the params file
   */
  public Map<String, String> parseParamsFile(Environment environment) {
    Map<String, String> paramsMap = new HashMap<>();

    if (StringUtils.isEmpty(paramsFilePath)) {
      return paramsMap;
    }

    try (InputStream templateStream = environment.resourceAsStream(paramsFilePath)) {
      if (templateStream == null) {
        return paramsMap;
      }

      String paramsFileContent = IOUtils.toString(templateStream, StandardCharsets.UTF_8);
      ObjectNode params = (ObjectNode) Json.parse(paramsFileContent);

      Iterator<Map.Entry<String, JsonNode>> fields = params.fields();
      while (fields.hasNext()) {
        Map.Entry<String, JsonNode> field = fields.next();
        String paramName = field.getKey();
        JsonNode paramValue = field.getValue();
        String paramStringValue;

        if (paramValue.isArray()) {
          List<String> parts = new ArrayList<>();
          for (JsonNode part : paramValue) {
            parts.add(part.textValue());
          }
          paramStringValue = String.join("", parts);
        } else {
          paramStringValue = paramValue.textValue();
        }

        paramsMap.put(paramName, paramStringValue);
      }
    } catch (Exception e) {
      throw new RuntimeException("Failed to read or process params file " + paramsFilePath, e);
    }

    return paramsMap;
  }

  /**
   * Gets a specific parameter value from the params file as a string. For array values, they are
   * joined into a single string.
   *
   * @param paramName the name of the parameter to retrieve
   * @param environment the play environment for resource access
   * @return the parameter value as a string, or empty string if not found
   */
  public String getParamValue(String paramName, Environment environment) {
    Map<String, String> params = parseParamsFile(environment);
    return params.getOrDefault(paramName, "");
  }

  /**
   * Gets the priority regex from the params file.
   *
   * @param environment the play environment for resource access
   * @return the priority regex string, or empty string if not found
   */
  public String getPriorityRegex(Environment environment) {
    return getParamValue("priority_regex", environment);
  }

  /**
   * Gets the metrics parameter from the params file.
   *
   * @param environment the play environment for resource access
   * @return the metrics string, or empty string if not found
   */
  public String getMetrics(Environment environment) {
    return getParamValue("metrics", environment);
  }
}
