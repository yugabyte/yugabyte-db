// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.helm;

import java.util.HashMap;
import java.util.Iterator;
import java.util.Map;
import java.util.Map.Entry;
import java.util.function.Function;
import org.apache.commons.lang3.StringUtils;
import org.yaml.snakeyaml.Yaml;

// Contains helper methods to parse/validate helm overrides for kubernetes universes.
public class HelmUtils {
  public static void validateYaml(String overridesStr) {
    if (!StringUtils.isNotBlank(overridesStr)) return;
    try {
      new Yaml().load(overridesStr.trim());
    } catch (Exception e) {
      String msg =
          String.format(
              "Error occured in yaml parsing %s, Error: %s", overridesStr.trim(), e.getMessage());
      throw new IllegalArgumentException(msg);
    }
  }

  public static Map<String, Object> convertYamlToMap(String overridesStr) {
    if (!StringUtils.isNotBlank(overridesStr)) return new HashMap<>();

    return new Yaml().load(overridesStr.trim());
  }

  public static Map<String, String> flattenMap(Map<String, Object> inputMap) {
    if (inputMap == null) return new HashMap<>();
    return flattenToStringMap(inputMap);
  }

  public static boolean equal(String overridesStr1, String overridesStr2) {
    Map<String, String> flatMap1 = flattenMap(convertYamlToMap(overridesStr1));
    Map<String, String> flatMap2 = flattenMap(convertYamlToMap(overridesStr2));
    return flatMap1.equals(flatMap2);
  }

  // Recursively traverses the override map and updates or adds the
  // keys to source map.
  public static void mergeYaml(Map<String, Object> source, Map<String, Object> override) {
    for (Entry<String, Object> entry : override.entrySet()) {
      String key = entry.getKey();
      if (!source.containsKey(key)) {
        source.put(key, override.get(key));
        continue;
      }
      if (!(override.get(key) instanceof Map) || !(source.get(key) instanceof Map)) {
        source.put(key, override.get(key));
        continue;
      }
      mergeYaml((Map<String, Object>) source.get(key), (Map<String, Object>) override.get(key));
    }
  }

  // Flattening logic copied from
  // https://github.com/spring-projects/spring-vault/blob/main/
  // spring-vault-core/src/main/java/org/springframework/vault/support/JsonMapFlattener.java
  // Made some modifications.
  private static Map<String, String> flattenToStringMap(Map<String, ? extends Object> inputMap) {
    Map<String, String> resultMap = new HashMap<>();
    doFlatten(
        "", inputMap.entrySet().iterator(), resultMap, it -> it == null ? null : it.toString());
    return resultMap;
  }

  private static void doFlatten(
      String propertyPrefix,
      Iterator<? extends Entry<String, ?>> inputMap,
      Map<String, ? extends Object> resultMap,
      Function<Object, Object> valueTransformer) {
    if (StringUtils.isNotBlank(propertyPrefix)) {
      propertyPrefix = propertyPrefix + ".";
    }
    while (inputMap.hasNext()) {
      Entry<String, ? extends Object> entry = inputMap.next();
      flattenElement(
          propertyPrefix.concat(entry.getKey()), entry.getValue(), resultMap, valueTransformer);
    }
  }

  @SuppressWarnings("unchecked")
  private static void flattenElement(
      String propertyPrefix,
      Object source,
      Map<String, ?> resultMap,
      Function<Object, Object> valueTransformer) {
    if (source instanceof Iterable) {
      flattenCollection(propertyPrefix, (Iterable<Object>) source, resultMap, valueTransformer);
      return;
    }
    if (source instanceof Map) {
      doFlatten(
          propertyPrefix,
          ((Map<String, ?>) source).entrySet().iterator(),
          resultMap,
          valueTransformer);
      return;
    }
    ((Map) resultMap).put(propertyPrefix, valueTransformer.apply(source));
  }

  private static void flattenCollection(
      String propertyPrefix,
      Iterable<Object> iterable,
      Map<String, ?> resultMap,
      Function<Object, Object> valueTransformer) {
    int counter = 0;
    for (Object element : iterable) {
      flattenElement(propertyPrefix + "[" + counter + "]", element, resultMap, valueTransformer);
      counter++;
    }
  }
}
