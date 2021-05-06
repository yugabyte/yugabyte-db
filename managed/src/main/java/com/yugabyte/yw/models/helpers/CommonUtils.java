// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.YWServiceException;
import play.libs.Json;

import java.util.Iterator;
import java.util.Map.Entry;
import java.util.function.BiFunction;
import java.util.function.Predicate;

import org.apache.commons.lang3.StringUtils;

import static play.mvc.Http.Status.BAD_REQUEST;

public class CommonUtils {
  public static final String DEFAULT_YB_HOME_DIR = "/home/yugabyte";

  private static final String maskRegex = "(?<!^.?).(?!.?$)";

  private static final String MASKED_FIELD_VALUE = "********";

  /**
   * Checks whether the field name represents a field with a sensitive data or
   * not.
   *
   * @param fieldname
   * @return true if yes, false otherwise
   */
  public static boolean isSensitiveField(String fieldname) {
    String ucFieldname = fieldname.toUpperCase();
    return isStrictlySensitiveField(ucFieldname) || ucFieldname.contains("KEY")
        || ucFieldname.contains("SECRET") || ucFieldname.contains("CREDENTIALS")
        || ucFieldname.contains("API") || ucFieldname.contains("POLICY");
  }

  /**
   * Checks whether the field name represents a field with a very sensitive data
   * or not. Such fields require strict masking.
   *
   * @param fieldname
   * @return true if yes, false otherwise
   */
  public static boolean isStrictlySensitiveField(String fieldname) {
    String ucFieldname = fieldname.toUpperCase();
    return ucFieldname.contains("PASSWORD");
  }

  /**
   * Masks sensitive fields in the config. Sensitive fields could be of two types
   * - sensitive and strictly sensitive. First ones are masked partly (two first
   * and two last characters are left), strictly sensitive fields are masked with
   * fixed 8 asterisk characters (recommended for passwords).
   *
   * @param config Config which could hold some data to mask.
   * @return Masked config
   */
  public static JsonNode maskConfig(JsonNode config) {
    return processData(config, CommonUtils::isSensitiveField,
        (key, value) -> getMaskedValue(key, value));
  }

  private static String getMaskedValue(String key, String value) {
    return isStrictlySensitiveField(key) || (value == null)
        || value.length() < 5 ? MASKED_FIELD_VALUE : value.replaceAll(maskRegex, "*");
  }

  /**
   * Removes masks from the config. If some fields are sensitive but were updated,
   * these fields are remain the same (with the new values).
   *
   * @param originalData Previous config data. All masked data recovered from it.
   * @param data         The new config data.
   * @return Updated config (all masked fields are recovered).
   */
  public static JsonNode unmaskConfig(JsonNode originalData, JsonNode data) {
    return originalData == null ? data
        : processData(data, CommonUtils::isSensitiveField,
            (key, value) -> StringUtils.equals(value, getMaskedValue(key, value))
                ? originalData.get(key).textValue()
                : value);
  }

  private static JsonNode processData(JsonNode data, Predicate<String> selector,
      BiFunction<String, String, String> getter) {
    if (data == null) {
      return Json.newObject();
    }
    JsonNode result = data.deepCopy();
    for (Iterator<Entry<String, JsonNode>> it = result.fields(); it.hasNext();) {
      Entry<String, JsonNode> entry = it.next();
      if (selector.test(entry.getKey())) {
        ((ObjectNode) result).put(entry.getKey(),
            getter.apply(entry.getKey(), entry.getValue().textValue()));
      }
    }
    return result;
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
