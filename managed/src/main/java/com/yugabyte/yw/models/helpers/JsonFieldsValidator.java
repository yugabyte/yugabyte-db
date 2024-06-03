// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers;

import com.google.common.annotations.VisibleForTesting;
import com.google.inject.Singleton;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.BeanValidator;
import java.util.Collection;
import java.util.HashMap;
import java.util.Map;
import javax.inject.Inject;
import org.yaml.snakeyaml.Yaml;
import play.Environment;

@Singleton
public class JsonFieldsValidator extends BaseBeanValidator {
  @VisibleForTesting
  static final String ACCEPTABLE_KEYS_RESOURCE_NAME = "configs/acceptableKeys.yml";

  private final Map<String, Collection<String>> acceptableKeys = new HashMap<>();

  @Inject
  public JsonFieldsValidator(BeanValidator beanValidator, Environment environment) {
    super(beanValidator);
    reloadAcceptableKeys(environment);
  }

  private void reloadAcceptableKeys(Environment environment) {
    Map<String, Object> data =
        new Yaml().load(environment.resourceAsStream(ACCEPTABLE_KEYS_RESOURCE_NAME));
    acceptableKeys.clear();
    for (String entityType : data.keySet()) {
      @SuppressWarnings("unchecked")
      Map<String, Collection<String>> keys = (Map<String, Collection<String>>) data.get(entityType);
      for (String subType : keys.keySet()) {
        acceptableKeys.put(String.format("%s:%s", entityType, subType), keys.get(subType));
      }
    }
  }

  public void validateFields(String masterKey, Map<String, String> data) {
    Collection<String> allowedKeys = acceptableKeys.get(masterKey);
    if (allowedKeys == null && !data.isEmpty()) {
      throwBeanValidatorError(data.keySet().iterator().next(), "Unknown field.", null);
    }

    for (String key : data.keySet()) {
      if (!allowedKeys.contains(key)) {
        throwBeanValidatorError(key, "Unknown field.", null);
      }
    }
  }

  public static String createProviderKey(Common.CloudType cloudType) {
    return String.format("provider:%s", cloudType);
  }
}
