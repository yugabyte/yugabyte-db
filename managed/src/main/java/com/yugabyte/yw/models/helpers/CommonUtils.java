// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.Iterables;
import com.yugabyte.yw.common.YWServiceException;
import io.ebean.ExpressionList;
import io.ebean.Junction;
import io.ebean.common.BeanList;
import io.jsonwebtoken.lang.Collections;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections.CollectionUtils;
import org.apache.commons.lang3.StringUtils;
import play.libs.Json;

import java.util.*;
import java.util.Map.Entry;
import java.util.function.BiFunction;
import java.util.function.Predicate;

import static play.mvc.Http.Status.BAD_REQUEST;

@Slf4j
public class CommonUtils {
  public static final String DEFAULT_YB_HOME_DIR = "/home/yugabyte";

  private static final String maskRegex = "(?<!^.?).(?!.?$)";

  private static final String MASKED_FIELD_VALUE = "********";

  public static final int DB_MAX_IN_CLAUSE_ITEMS = 1000;
  public static final int DB_IN_CLAUSE_TO_WARN = 50000;

  /**
   * Checks whether the field name represents a field with a sensitive data or not.
   *
   * @param fieldname
   * @return true if yes, false otherwise
   */
  public static boolean isSensitiveField(String fieldname) {
    String ucFieldname = fieldname.toUpperCase();
    return isStrictlySensitiveField(ucFieldname)
        || ucFieldname.contains("KEY")
        || ucFieldname.contains("SECRET")
        || ucFieldname.contains("CREDENTIALS")
        || ucFieldname.contains("API")
        || ucFieldname.contains("POLICY");
  }

  /**
   * Checks whether the field name represents a field with a very sensitive data or not. Such fields
   * require strict masking.
   *
   * @param fieldname
   * @return true if yes, false otherwise
   */
  public static boolean isStrictlySensitiveField(String fieldname) {
    String ucFieldname = fieldname.toUpperCase();
    return ucFieldname.contains("PASSWORD");
  }

  /**
   * Masks sensitive fields in the config. Sensitive fields could be of two types - sensitive and
   * strictly sensitive. First ones are masked partly (two first and two last characters are left),
   * strictly sensitive fields are masked with fixed 8 asterisk characters (recommended for
   * passwords).
   *
   * @param config Config which could hold some data to mask.
   * @return Masked config
   */
  public static JsonNode maskConfig(JsonNode config) {
    return processData(
        config, CommonUtils::isSensitiveField, (key, value) -> getMaskedValue(key, value));
  }

  public static Map<String, String> maskConfigNew(Map<String, String> config) {
    return processDataNew(
        config, CommonUtils::isSensitiveField, (key, value) -> getMaskedValue(key, value));
  }

  private static String getMaskedValue(String key, String value) {
    return isStrictlySensitiveField(key) || (value == null) || value.length() < 5
        ? MASKED_FIELD_VALUE
        : value.replaceAll(maskRegex, "*");
  }

  /**
   * Removes masks from the config. If some fields are sensitive but were updated, these fields are
   * remain the same (with the new values).
   *
   * @param originalData Previous config data. All masked data recovered from it.
   * @param data The new config data.
   * @return Updated config (all masked fields are recovered).
   */
  public static JsonNode unmaskConfig(JsonNode originalData, JsonNode data) {
    return originalData == null
        ? data
        : processData(
            data,
            CommonUtils::isSensitiveField,
            (key, value) ->
                StringUtils.equals(value, getMaskedValue(key, value))
                    ? originalData.get(key).textValue()
                    : value);
  }

  private static JsonNode processData(
      JsonNode data, Predicate<String> selector, BiFunction<String, String, String> getter) {
    if (data == null) {
      return Json.newObject();
    }
    JsonNode result = data.deepCopy();
    for (Iterator<Entry<String, JsonNode>> it = result.fields(); it.hasNext(); ) {
      Entry<String, JsonNode> entry = it.next();
      if (selector.test(entry.getKey())) {
        ((ObjectNode) result)
            .put(entry.getKey(), getter.apply(entry.getKey(), entry.getValue().textValue()));
      }
    }
    return result;
  }

  private static Map<String, String> processDataNew(
      Map<String, String> data,
      Predicate<String> selector,
      BiFunction<String, String, String> getter) {
    HashMap<String, String> result = new HashMap<>();
    if (data != null) {
      data.forEach(
          (k, v) -> {
            if (selector.test(k)) {
              result.put(k, getter.apply(k, v));
            } else {
              result.put(k, v);
            }
          });
    }
    return result;
  }

  /** Recursively merges second JsonNode into first JsonNode. ArrayNodes will be overwritten. */
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
   * @param path Dot-separated string notation to represent JSON property
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

  public static <T> ExpressionList<T> appendInClause(
      ExpressionList<T> query, String field, Collection<?> values) {
    if (!Collections.isEmpty(values)) {
      if (values.size() > DB_IN_CLAUSE_TO_WARN) {
        log.warn(
            "Querying for {} entries in field {} - may affect performance", values.size(), field);
      }
      Junction<T> orExpr = query.or();
      for (List<?> batch : Iterables.partition(values, CommonUtils.DB_MAX_IN_CLAUSE_ITEMS)) {
        orExpr.in(field, batch);
      }
      query.endOr();
    }
    return query;
  }

  public static <T> ExpressionList<T> appendNotInClause(
      ExpressionList<T> query, String field, Collection<?> values) {
    if (!Collections.isEmpty(values)) {
      for (List<?> batch : Iterables.partition(values, CommonUtils.DB_MAX_IN_CLAUSE_ITEMS)) {
        query.notIn(field, batch);
      }
    }
    return query;
  }

  /**
   * Should be used to set ebean entity list value in case list contains values, which are unique by
   * key. In this case list is not in fact ordered - it's more like map.
   *
   * <p>In case exactly the same value exists in the list - it remains in the list. In case exactly
   * the same value was removed from the BeanList earlier - it is restored so that it's not inserted
   * twice. In case value with same key is in the list - it's replaced with the new value. Otherwise
   * value is just added.
   *
   * @param list current list
   * @param entry entry to add
   * @param <T> entry type
   * @return resulting list
   */
  public static <T extends UniqueKeyListValue<T>> List<T> setUniqueListValue(
      List<T> list, T entry) {
    if (list == null) {
      list = new ArrayList<>();
    }
    T removedValue = getRemovedValue(list, entry, T::valueEquals);
    if (removedValue != null) {
      list.add(removedValue);
      return list;
    }
    T currentValue = list.stream().filter(e -> e.keyEquals(entry)).findFirst().orElse(null);
    if (currentValue != null) {
      if (currentValue.valueEquals(entry)) {
        return list;
      }
      list.remove(currentValue);
    }

    list.add(entry);
    return list;
  }

  /**
   * Should be used to set ebean entity list values in case list contains values, which are unique
   * by key. See setUniqueListValue for more details.
   *
   * @param list current list
   * @param values values to set
   * @param <T> entry type
   * @return resulting list
   */
  public static <T extends UniqueKeyListValue<T>> List<T> setUniqueListValues(
      List<T> list, List<T> values) {
    List<T> result = list != null ? list : values;
    if (list != null) {
      // Ebean ORM requires us to update existing loaded field rather than replace it completely.
      result.clear();
      values.forEach(value -> setUniqueListValue(result, value));
    }
    return result;
  }

  /*
   * Ebean ORM does not allow us to remove child object and add back exactly the same new object -
   * it tries to add it without prior removal. Once you add it back -
   * it removes it from "removed beans" list and just tries to insert it once again.
   */
  private static <T> T getRemovedValue(
      List<T> list, T entry, BiFunction<T, T, Boolean> equalityCheck) {
    if (list instanceof BeanList) {
      BeanList<T> beanList = (BeanList<T>) list;
      Set<T> removedBeans = beanList.getModifyRemovals();
      if (CollectionUtils.isEmpty(removedBeans)) {
        return null;
      }
      return removedBeans
          .stream()
          .filter(e -> equalityCheck.apply(e, entry))
          .findFirst()
          .orElse(null);
    }
    return null;
  }
}
