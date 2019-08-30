// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import play.libs.Json;

import java.util.Iterator;

public class CommonUtils {
  private static String maskRegex = "(?<!^.?).(?!.?$)";

  public static JsonNode maskConfig(JsonNode config) {
    if (config == null || config.size() == 0) {
      return Json.newObject();
    }
    JsonNode maskedData = config.deepCopy();
    for (Iterator<String> it = maskedData.fieldNames(); it.hasNext(); ) {
      String key = it.next();
      // TODO: make this a constant
      if (key.contains("KEY") || key.contains("SECRET")) {
        ((ObjectNode) maskedData).put(key, maskedData.get(key).asText().replaceAll(maskRegex, "*"));
      }
    }
    return maskedData;
  }
}
