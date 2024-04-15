// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers;

import static java.util.Map.entry;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.fasterxml.jackson.databind.node.TextNode;
import com.google.common.collect.ImmutableMultiset;
import com.google.common.collect.Iterables;
import com.jayway.jsonpath.Configuration;
import com.jayway.jsonpath.JsonPath;
import com.jayway.jsonpath.spi.json.JacksonJsonNodeJsonProvider;
import com.jayway.jsonpath.spi.mapper.JacksonMappingProvider;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.controllers.RequestContext;
import com.yugabyte.yw.controllers.TokenAuthenticator;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.extended.UserWithFeatures;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.paging.PagedQuery;
import com.yugabyte.yw.models.paging.PagedResponse;
import io.ebean.ExpressionList;
import io.ebean.Junction;
import io.ebean.PagedList;
import io.ebean.Query;
import io.ebean.common.BeanList;
import java.lang.annotation.Annotation;
import java.time.Duration;
import java.time.Instant;
import java.time.temporal.ChronoUnit;
import java.time.temporal.TemporalUnit;
import java.util.*;
import java.util.Map.Entry;
import java.util.function.BiFunction;
import java.util.function.Function;
import java.util.function.Predicate;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.collections4.MapUtils;
import org.apache.commons.lang3.StringUtils;
import org.springframework.security.crypto.encrypt.Encryptors;
import org.springframework.security.crypto.encrypt.TextEncryptor;
import play.libs.Json;

@Slf4j
public class CommonUtils {

  public static final String DEFAULT_YB_HOME_DIR = "/home/yugabyte";
  public static final String DEFAULT_YBC_DIR = "%s/yugabyte";

  private static final Pattern RELEASE_REGEX =
      Pattern.compile("^(\\d+)\\.(\\d+)\\.(\\d+)\\.(\\d+)(.+)?$");

  public static final String maskRegex = "(?<!^.?).(?!.?$)";

  private static final String MASKED_FIELD_VALUE = "********";

  public static final int DB_MAX_IN_CLAUSE_ITEMS = 1000;
  public static final int DB_IN_CLAUSE_TO_WARN = 50000;
  public static final int DB_OR_CHAIN_TO_WARN = 100;

  public static final String MIN_PROMOTE_AUTO_FLAG_RELEASE = "2.17.0.0";

  private static final Configuration JSONPATH_CONFIG =
      Configuration.builder()
          .jsonProvider(new JacksonJsonNodeJsonProvider())
          .mappingProvider(new JacksonMappingProvider())
          .build();

  // Sensisitve field substrings
  private static final List<String> sensitiveFieldSubstrings =
      Arrays.asList(
          "KEY", "SECRET", "CREDENTIALS", "API", "POLICY", "HC_VAULT_TOKEN", "vaultToken");
  // Exclude following strings from being sensitive fields
  private static final List<String> excludedFieldNames =
      Arrays.asList(
          // GCP KMS fields
          "KEY_RING_ID",
          "CRYPTO_KEY_ID",
          // Azure KMS fields
          "AZU_KEY_NAME",
          "AZU_KEY_ALGORITHM",
          "AZU_KEY_SIZE",
          // Hashicorp KMS fields
          "HC_VAULT_KEY_NAME",
          "KEYSPACETABLELIST",
          // General API field
          "KEYSPACE",
          "APITOKENVERSION");

  public static final Map<Character, Character> CHAR_MAP =
      Map.ofEntries(
          entry('A', '#'),
          entry('B', '@'),
          entry('C', '*'),
          entry('D', '$'),
          entry('E', '%'),
          entry('F', '^'),
          entry('G', '&'),
          entry('H', '!'),
          entry('I', '~'),
          entry('J', '+'),
          entry('K', '-'),
          entry('L', '='),
          entry('M', ':'),
          entry('N', ';'),
          entry('O', '?'),
          entry('P', '<'),
          entry('Q', '>'),
          entry('R', '('),
          entry('S', ')'),
          entry('T', '{'),
          entry('U', '}'),
          entry('V', '['),
          entry('W', ']'),
          entry('X', '/'),
          entry('Y', '|'),
          entry('Z', '.'),
          entry('a', '#'),
          entry('b', '@'),
          entry('c', '*'),
          entry('d', '$'),
          entry('e', '%'),
          entry('f', '^'),
          entry('g', '&'),
          entry('h', '!'),
          entry('i', '~'),
          entry('j', '+'),
          entry('k', '-'),
          entry('l', '='),
          entry('m', ':'),
          entry('n', ';'),
          entry('o', '?'),
          entry('p', '<'),
          entry('q', '>'),
          entry('r', '('),
          entry('s', ')'),
          entry('t', '{'),
          entry('u', '}'),
          entry('v', '['),
          entry('w', ']'),
          entry('x', '/'),
          entry('y', '|'),
          entry('z', '.'),
          entry('0', '\''),
          entry('1', '\''),
          entry('2', '\''),
          entry('3', '\''),
          entry('4', '\''),
          entry('5', '\''),
          entry('6', '\''),
          entry('7', '\''),
          entry('8', '\''),
          entry('9', '\''),
          entry(' ', '\''));

  /**
   * Checks whether the field name represents a field with a sensitive data or not.
   *
   * @param fieldname
   * @return true if yes, false otherwise
   */
  public static boolean isSensitiveField(String fieldname) {
    String ucFieldname = fieldname.toUpperCase();
    if (isStrictlySensitiveField(ucFieldname)) {
      return true;
    }

    // Needed for KMS UI - more specifically listKMSConfigs()
    // Can add more exclusions if required
    if (excludedFieldNames.contains(ucFieldname)) {
      return false;
    }

    // Check if any of sensitiveFieldSubstrings are substrings in ucFieldname and mark as sensitive
    return sensitiveFieldSubstrings.stream().anyMatch(sfs -> ucFieldname.contains(sfs));
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
  public static ObjectNode maskConfig(ObjectNode config) {
    return processData(
        "$",
        config,
        CommonUtils::isSensitiveField,
        (key, value, path) -> getMaskedValue(key, value));
  }

  public static Map<String, String> maskConfigNew(Map<String, String> config) {
    return processDataNew(config, CommonUtils::isSensitiveField, CommonUtils::getMaskedValue);
  }

  public static Map<String, String> maskAllFields(Map<String, String> config) {
    return processDataNew(
        config,
        (String s) -> {
          return true;
        },
        CommonUtils::getMaskedValue);
  }

  public static String getMaskedValue(String key, String value) {
    return isStrictlySensitiveField(key) || (value == null) || value.length() < 5
        ? MASKED_FIELD_VALUE
        : value.replaceAll(maskRegex, "*");
  }

  public static String getEmptiableMaskedValue(String key, String value) {
    if (StringUtils.isBlank(value)) {
      return "";
    }
    return getMaskedValue(key, value);
  }

  public static JsonNode getMaskedValue(JsonNode value, List<String> toMaskKeys) {
    if (value == null) {
      return value;
    }
    ObjectNode jsonNodeValue = (ObjectNode) value;
    for (String key : toMaskKeys) {
      if (jsonNodeValue.has(key)) {
        String keyValue = jsonNodeValue.get(key).toString();
        jsonNodeValue.put(key, keyValue.replaceAll(maskRegex, "*"));
      }
    }
    return value;
  }

  public static String getMaskedValue(String value) {
    if (value == null) {
      return value;
    }
    return value.replaceAll(maskRegex, "*");
  }

  @SuppressWarnings("unchecked")
  public static <T> T maskObject(T object) {
    try {
      JsonNode updatedJson = CommonUtils.maskConfig((ObjectNode) Json.toJson(object));
      return Json.fromJson(updatedJson, (Class<T>) object.getClass());
    } catch (Exception e) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Failed to parse " + object.getClass().getSimpleName() + " object: " + e.getMessage());
    }
  }

  @SuppressWarnings("unchecked")
  public static <T> T unmaskObject(T originalObject, T object) {
    try {
      JsonNode updatedJson =
          CommonUtils.unmaskJsonObject(
              (ObjectNode) Json.toJson(originalObject), (ObjectNode) Json.toJson(object));
      return Json.fromJson(updatedJson, (Class<T>) object.getClass());
    } catch (Exception e) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Failed to parse " + object.getClass().getSimpleName() + " object: " + e.getMessage());
    }
  }

  /**
   * Removes masks from the config. If some fields are sensitive but were updated, these fields are
   * remain the same (with the new values).
   *
   * @param originalData Previous config data. All masked data recovered from it.
   * @param data The new config data.
   * @return Updated config (all masked fields are recovered).
   */
  public static ObjectNode unmaskJsonObject(ObjectNode originalData, ObjectNode data) {
    return originalData == null
        ? data
        : processData(
            "$",
            data,
            CommonUtils::isSensitiveField,
            (key, value, path) -> {
              JsonPath jsonPath = JsonPath.compile(path + "." + key);
              return StringUtils.equals(value, getMaskedValue(key, value))
                  ? ((TextNode) jsonPath.read(originalData, JSONPATH_CONFIG)).asText()
                  : value;
            });
  }

  public static Map<String, String> encryptProviderConfig(
      Map<String, String> config, UUID customerUUID, String providerCode) {
    if (MapUtils.isNotEmpty(config)) {
      try {
        final ObjectMapper mapper = new ObjectMapper();
        final String salt = generateSalt(customerUUID, providerCode);
        final TextEncryptor encryptor = Encryptors.delux(customerUUID.toString(), salt);
        final String encryptedConfig = encryptor.encrypt(mapper.writeValueAsString(config));
        Map<String, String> encryptMap = new HashMap<>();
        encryptMap.put("encrypted", encryptedConfig);
        return encryptMap;
      } catch (Exception e) {
        final String errMsg =
            String.format(
                "Could not encrypt provider configuration for customer %s",
                customerUUID.toString());
        log.error(errMsg, e);
      }
    }
    return new HashMap<>();
  }

  public static Map<String, String> decryptProviderConfig(
      Map<String, String> config, UUID customerUUID, String providerCode) {
    if (MapUtils.isNotEmpty(config)) {
      try {
        final ObjectMapper mapper = new ObjectMapper();
        final String encryptedConfig = config.get("encrypted");
        final String salt = generateSalt(customerUUID, providerCode);
        final TextEncryptor encryptor = Encryptors.delux(customerUUID.toString(), salt);
        final String decryptedConfig = encryptor.decrypt(encryptedConfig);
        return mapper.readValue(decryptedConfig, new TypeReference<Map<String, String>>() {});
      } catch (Exception e) {
        final String errMsg =
            String.format(
                "Could not decrypt provider configuration for customer %s",
                customerUUID.toString());
        log.error(errMsg, e);
      }
    }
    return new HashMap<>();
  }

  public static String generateSalt(UUID customerUUID, String providerCode) {
    final String kpValue = String.valueOf(providerCode.hashCode());
    final String saltBase = "%s%s";
    final String salt =
        String.format(saltBase, customerUUID.toString().replace("-", ""), kpValue.replace("-", ""));
    return salt.length() % 2 == 0 ? salt : salt + "0";
  }

  private static ObjectNode processData(
      String path,
      JsonNode data,
      Predicate<String> selector,
      TriFunction<String, String, String, String> getter) {
    if (data == null) {
      return Json.newObject();
    }
    ObjectNode result = data.deepCopy();
    for (Iterator<Entry<String, JsonNode>> it = result.fields(); it.hasNext(); ) {
      Entry<String, JsonNode> entry = it.next();
      if (entry.getValue().isObject()) {
        result.set(
            entry.getKey(),
            processData(path + "." + entry.getKey(), entry.getValue(), selector, getter));
      }
      if (selector.test(entry.getKey())) {
        result.put(
            entry.getKey(), getter.apply(entry.getKey(), entry.getValue().textValue(), path));
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
      return;
    }

    if (!node1.isObject() || !node2.isObject()) {
      throw new PlatformServiceException(BAD_REQUEST, "Only ObjectNodes may be merged.");
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
    if (!CollectionUtils.isEmpty(values)) {
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

  public static <T> ExpressionList<T> appendLikeClause(
      ExpressionList<T> query, String field, Collection<String> values) {
    if (!CollectionUtils.isEmpty(values)) {
      Junction<T> orExpr = query.or();
      for (String value : values) {
        orExpr.icontains(field, value);
      }
      query.endOr();
    }
    return query;
  }

  public static <T> ExpressionList<T> appendNotInClause(
      ExpressionList<T> query, String field, Collection<?> values) {
    if (!CollectionUtils.isEmpty(values)) {
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
      Set<T> removedBeans = beanList.modifyRemovals();
      if (CollectionUtils.isEmpty(removedBeans)) {
        return null;
      }
      return removedBeans.stream()
          .filter(e -> equalityCheck.apply(e, entry))
          .findFirst()
          .orElse(null);
    }
    return null;
  }

  public static <E, R extends PagedResponse<E>> R performPagedQuery(
      Query<E> query, PagedQuery<?, ?> pagedQuery, Class<R> responseClass) {
    if (pagedQuery.getDirection() == PagedQuery.SortDirection.DESC) {
      query.orderBy().desc(pagedQuery.getSortBy().getSortField());
    } else {
      query.orderBy().asc(pagedQuery.getSortBy().getSortField());
    }
    if (pagedQuery.getSortBy() != pagedQuery.getSortBy().getOrderField()) {
      query.orderBy().asc(pagedQuery.getSortBy().getOrderField().getSortField());
    }
    query.setFirstRow(pagedQuery.getOffset());
    query.setMaxRows(pagedQuery.getLimit() + 1);
    PagedList<E> pagedList = query.findPagedList();
    R response;
    try {
      response = responseClass.newInstance();
    } catch (Exception e) {
      throw new IllegalStateException(
          "Failed to create " + responseClass.getSimpleName() + " instance", e);
    }
    int actualSize = pagedList.getList().size();
    response.setEntities(
        pagedList.getList().subList(0, Math.min(actualSize, pagedQuery.getLimit())));
    response.setHasPrev(pagedList.hasPrev());
    response.setHasNext(actualSize > pagedQuery.getLimit());
    if (pagedQuery.isNeedTotalCount()) {
      response.setTotalCount(pagedList.getTotalCount());
    }
    return response;
  }

  public static Date nowWithoutMillis() {
    return Date.from(Instant.now().truncatedTo(ChronoUnit.SECONDS));
  }

  public static Date nowPlusWithoutMillis(long amount, TemporalUnit timeUnit) {
    return Date.from(Instant.now().plus(amount, timeUnit).truncatedTo(ChronoUnit.SECONDS));
  }

  public static Date nowMinusWithoutMillis(long amount, TemporalUnit timeUnit) {
    return Date.from(Instant.now().minus(amount, timeUnit).truncatedTo(ChronoUnit.SECONDS));
  }

  public static Date nowPlus(long amount, TemporalUnit timeUnit) {
    return Date.from(Instant.now().plus(amount, timeUnit));
  }

  public static Date nowMinus(long amount, TemporalUnit timeUnit) {
    return Date.from(Instant.now().minus(amount, timeUnit));
  }

  public static Date datePlus(Date date, long amount, TemporalUnit timeUnit) {
    return Date.from(date.toInstant().plus(amount, timeUnit));
  }

  public static Date dateMinus(Date date, long amount, TemporalUnit timeUnit) {
    return Date.from(date.toInstant().minus(amount, timeUnit));
  }

  public static long getDurationSeconds(Date startTime, Date endTime) {
    return getDurationSeconds(startTime.toInstant(), endTime.toInstant());
  }

  public static long getDurationSeconds(Instant startTime, Instant endTime) {
    Duration duration = Duration.between(startTime, endTime);
    return duration.getSeconds();
  }

  /**
   * Returns map with common entries (both key and value equals in all maps) from the list of input
   * maps. Basically it calculates count of key-value pairs in all the incoming maps and builds new
   * map from the ones, where count == maps.size().
   *
   * @param maps incoming maps
   * @param <K> Map key type
   * @param <V> Map value type
   * @return Map with common entries.
   */
  public static <K, V> Map<K, V> getMapCommonElements(List<Map<K, V>> maps) {
    if (CollectionUtils.isEmpty(maps)) {
      return Collections.emptyMap();
    }
    int mapsCount = maps.size();
    Map<Pair<K, V>, Long> pairCount =
        maps.stream()
            .flatMap(map -> map.entrySet().stream())
            .map(entry -> new Pair<>(entry.getKey(), entry.getValue()))
            .collect(Collectors.groupingBy(Function.identity(), Collectors.counting()));
    return pairCount.entrySet().stream()
        .filter(entry -> entry.getValue() == mapsCount)
        .map(Map.Entry::getKey)
        .collect(Collectors.toMap(Pair::getFirst, Pair::getSecond));
  }

  public static boolean isReleaseEqualOrAfter(String thresholdRelease, String actualRelease) {
    return compareReleases(thresholdRelease, actualRelease, false, true, true);
  }

  public static boolean isReleaseBefore(String thresholdRelease, String actualRelease) {
    return compareReleases(thresholdRelease, actualRelease, true, false, false);
  }

  public static boolean isReleaseBetween(
      String minRelease, String maxRelease, String actualRelease) {
    return isReleaseEqualOrAfter(minRelease, actualRelease)
        && isReleaseBefore(maxRelease, actualRelease);
  }

  public static boolean isReleaseEqual(String thresholdRelease, String actualRelease) {
    return compareReleases(thresholdRelease, actualRelease, false, false, true);
  }

  /**
   * This method compares 2 version strings. Make sure to only compare stable with stable and
   * preview with preview if using this function. If you are not sure of either, use method {@link
   * com.yugabyte.yw.common.Util#compareYBVersions}.
   *
   * @param thresholdRelease
   * @param actualRelease
   * @param beforeMatches
   * @param afterMatches
   * @param equalMatches
   * @return
   */
  private static boolean compareReleases(
      String thresholdRelease,
      String actualRelease,
      boolean beforeMatches,
      boolean afterMatches,
      boolean equalMatches) {
    Matcher thresholdMatcher = RELEASE_REGEX.matcher(thresholdRelease);
    Matcher actualMatcher = RELEASE_REGEX.matcher(actualRelease);
    if (!thresholdMatcher.matches()) {
      throw new IllegalArgumentException(
          "Threshold release " + thresholdRelease + " does not match release pattern");
    }
    if (!actualMatcher.matches()) {
      log.warn(
          "Actual release {} does not match release pattern - handle as latest release",
          actualRelease);
      return afterMatches;
    }
    for (int i = 1; i < 6; i++) {
      String thresholdPartStr = thresholdMatcher.group(i);
      String actualPartStr = actualMatcher.group(i);
      if (i == 5) {
        // Build number.
        thresholdPartStr = String.valueOf(convertBuildNumberForComparison(thresholdPartStr, true));
        actualPartStr = String.valueOf(convertBuildNumberForComparison(actualPartStr, false));
      }
      int thresholdPart = Integer.parseInt(thresholdPartStr);
      int actualPart = Integer.parseInt(actualPartStr);
      if (actualPart > thresholdPart) {
        return afterMatches;
      }
      if (actualPart < thresholdPart) {
        return beforeMatches;
      }
    }
    // Equal releases.
    return equalMatches;
  }

  private static int convertBuildNumberForComparison(String buildNumberStr, boolean threshold) {
    if (StringUtils.isEmpty(buildNumberStr) || !buildNumberStr.startsWith("-b")) {
      // Threshold without a build or with invalid build is treated as -b0,
      // while actual build is custom build and is always treated as later build.
      return threshold ? 0 : Integer.MAX_VALUE;
    }
    try {
      return Integer.parseInt(buildNumberStr.substring(2));
    } catch (Exception e) {
      // Same logic as above.
      return threshold ? 0 : Integer.MAX_VALUE;
    }
  }

  @FunctionalInterface
  private interface TriFunction<A, B, C, R> {
    R apply(A a, B b, C c);
  }

  /**
   * Finds if the annotation class is present on the given class or its super classes.
   *
   * @param clazz the given class.
   * @param annotationClass the annotation class.
   * @return the optional of annotation.
   */
  public static <T extends Annotation> Optional<T> isAnnotatedWith(
      Class<?> clazz, Class<T> annotationClass) {
    if (clazz == null) {
      return Optional.empty();
    }
    T annotation = clazz.getAnnotation(annotationClass);
    if (annotation != null) {
      return Optional.of(annotation);
    }
    return isAnnotatedWith(clazz.getSuperclass(), annotationClass);
  }

  public static NodeDetails getARandomLiveTServer(Universe universe) {
    UniverseDefinitionTaskParams.Cluster primaryCluster =
        universe.getUniverseDetails().getPrimaryCluster();
    List<NodeDetails> tserverLiveNodes =
        universe.getUniverseDetails().getNodesInCluster(primaryCluster.uuid).stream()
            .filter(nodeDetails -> nodeDetails.isTserver)
            .filter(nodeDetails -> nodeDetails.state == NodeState.Live)
            .collect(Collectors.toList());
    if (tserverLiveNodes.isEmpty()) {
      throw new IllegalStateException(
          "No live TServers found for Universe UUID: " + universe.getUniverseUUID());
    }
    return tserverLiveNodes.get(new Random().nextInt(tserverLiveNodes.size()));
  }

  public static NodeDetails getServerToRunYsqlQuery(Universe universe) {
    // Prefer the master leader since that will result in a faster query.
    // If the leader does not have a tserver process though, select any random tserver.
    NodeDetails nodeToUse = universe.getMasterLeaderNode();
    if (nodeToUse == null || !nodeToUse.isTserver) {
      nodeToUse = getARandomLiveTServer(universe);
    }
    return nodeToUse;
  }

  public static String logTableName(String tableName) {
    if (StringUtils.isBlank(tableName)) {
      return "";
    }
    char[] logTableName = new char[tableName.length() * 2];
    for (int i = 0; i < tableName.length(); i++) {
      logTableName[2 * i] = tableName.charAt(i);
      logTableName[2 * i + 1] = CHAR_MAP.getOrDefault(tableName.charAt(i), tableName.charAt(i));
    }
    return new String(logTableName).trim();
  }

  /**
   * This method extracts the json from shell response where the shell executes a SQL Query that
   * aggregates the response as JSON e.g. select jsonb_agg() The resultant shell output has json
   * response on line number 3
   */
  public static String extractJsonisedSqlResponse(ShellResponse shellResponse) {
    StringBuilder data = new StringBuilder();
    if (StringUtils.isNotBlank(shellResponse.message)) {
      try (Scanner scanner = new Scanner(shellResponse.message)) {
        boolean headerStarted = false;
        while (scanner.hasNextLine()) {
          String line = scanner.nextLine();
          if (!headerStarted && !line.startsWith("-")) {
            // Read till header start
            continue;
          } else if (line.startsWith("-")) {
            // Read '------...' header
            headerStarted = true;
            continue;
          }
          if (line.startsWith("(1 row)")) {
            // jsonb_agg(x) always returns 1 row - it's the last line
            break;
          }
          data.append(line);
        }
      }
    }
    return data.toString();
  }

  /**
   * Compares two collections ignoring items order. Different size of collections gives inequality
   * of collections.
   */
  public static <T> boolean isEqualIgnoringOrder(Collection<T> x, Collection<T> y) {
    if ((x == null) || (y == null)) {
      return x == y;
    }

    if (x.size() != y.size()) {
      return false;
    }

    return ImmutableMultiset.copyOf(x).equals(ImmutableMultiset.copyOf(y));
  }

  /**
   * Generates log message containing state information of universe and running status of scheduler.
   */
  public static String generateStateLogMsg(Universe universe, boolean alreadyRunning) {
    String stateLogMsg =
        String.format(
            "alreadyRunning=%s updateInProgress=%s universePaused=%s",
            alreadyRunning,
            universe.getUniverseDetails().updateInProgress,
            universe.getUniverseDetails().universePaused);
    return stateLogMsg;
  }

  /**
   * Get the user sending the API request from the HTTP context. It throws exception if the context
   * is not set.
   */
  public static Users getUserFromContext() {
    return RequestContext.get(TokenAuthenticator.USER).getUser();
  }

  /** Get the user sending the API request from the HTTP context if present. */
  public static Optional<Users> maybeGetUserFromContext() {
    UserWithFeatures value = RequestContext.getIfPresent(TokenAuthenticator.USER);
    return value == null ? Optional.empty() : Optional.ofNullable(value.getUser());
  }

  public static boolean isAutoFlagSupported(String dbVersion) {
    return isReleaseEqualOrAfter(MIN_PROMOTE_AUTO_FLAG_RELEASE, dbVersion);
  }

  // Only replace path at the beginning.
  public static String replaceBeginningPath(
      String pathToModify, String initialRoot, String finalRoot) {
    String regex = "^" + Pattern.quote(initialRoot);
    return pathToModify.replaceAll(regex, finalRoot);
  }
}
