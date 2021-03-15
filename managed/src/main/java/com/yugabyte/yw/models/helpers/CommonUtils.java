// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.YWServiceException;
import play.libs.Json;

import java.util.Iterator;

import static play.mvc.Http.Status.BAD_REQUEST;

public class CommonUtils {
  public static final String DEFAULT_YB_HOME_DIR = "/home/yugabyte";
  private static final String maskRegex = "(?<!^.?).(?!.?$)";

  public static JsonNode maskConfig(JsonNode config) {
    if (config == null || config.size() == 0) {
      return Json.newObject();
    }
    JsonNode maskedData = config.deepCopy();
    for (Iterator<String> it = maskedData.fieldNames(); it.hasNext(); ) {
      String key = it.next();
      String keyLowerCase = key.toLowerCase();
      // TODO: make this a constant
      if (keyLowerCase.contains("key")
        || keyLowerCase.contains("secret")
        || keyLowerCase.contains("api")
        || keyLowerCase.contains("policy")) {
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
      throw new YWServiceException(BAD_REQUEST, "Cannot merge empty nodes.");
    }

    if (!node1.isObject() || !node2.isObject()) {
      throw new YWServiceException(BAD_REQUEST, "Only ObjectNodes may be merged.");
    }

    for (Iterator<String> fieldNames = node2.fieldNames(); fieldNames.hasNext(); ) {
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

  /**
   * Gets the value at `path` of `object`. Traverses `object` and attempts to access each nested key
   * in `path`. Returns null if unable to find property. Based on lodash's get utility function:
   * https://lodash.com/docs/4.17.15#get
   *
   * @param object ObjectNode to be traversed
   * @param path   Dot-separated string notation to represent JSON property
   * @return JsonNode value of property or null
   */
  public static JsonNode getNodeProperty(JsonNode object, String path) {
    String[] jsonPropertyList = path.split("\\.");
    JsonNode currentNode = object;
    for (String key : jsonPropertyList) {
      if (currentNode != null && currentNode.has(key)) {
        currentNode = currentNode.get(key);
      } else {
        currentNode = null;
        break;
      }
    }
    return currentNode;
  }
}
