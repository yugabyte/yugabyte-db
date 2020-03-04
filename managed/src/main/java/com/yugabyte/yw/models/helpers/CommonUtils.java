// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import play.libs.Json;

import java.util.Iterator;

public class CommonUtils {
  public static final String DEFAULT_YB_HOME_DIR = "/home/yugabyte";
  private static String maskRegex = "(?<!^.?).(?!.?$)";

  public static JsonNode maskConfig(JsonNode config) {
    if (config == null || config.size() == 0) {
      return Json.newObject();
    }
    JsonNode maskedData = config.deepCopy();
    for (Iterator<String> it = maskedData.fieldNames(); it.hasNext(); ) {
      String key = it.next();
      String keyLowerCase = key.toLowerCase();
      // TODO: make this a constant
      if (keyLowerCase.contains("key") || keyLowerCase.contains("secret") ||
              keyLowerCase.contains("api") || keyLowerCase.contains("policy")) {
        ((ObjectNode) maskedData).put(key, maskedData.get(key).asText().replaceAll(maskRegex, "*"));
      }
    }
    return maskedData;
  }

  /**
   * Recursively merges second JsonNode into first JsonNode. ArrayNodes will be overwritten.
   */
  public static void deepMerge(JsonNode node1, JsonNode node2) {
    if (node1 == null || node1.size() == 0 || node2 == null || node2.size() == 0) {
      throw new RuntimeException("Cannot merge empty nodes.");
    }

    if (!node1.isObject() || !node2.isObject()) {
      throw new RuntimeException("Only ObjectNodes may be merged.");
    }

    for (Iterator<String> fieldNames = node2.fieldNames(); fieldNames.hasNext();) {
      String fieldName = fieldNames.next();
      JsonNode oldVal = node1.get(fieldName);
      JsonNode newVal = node2.get(fieldName);
      if (oldVal == null || oldVal.isNull() || !oldVal.isObject() || !newVal.isObject()) {
        ((ObjectNode) node1).replace(fieldName, newVal);
      } else {
        CommonUtils.deepMerge(oldVal, newVal);
      }
    }
  }
}
