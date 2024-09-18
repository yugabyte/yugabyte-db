/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common.password;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.data.CustomerConfigPasswordPolicyData;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.ArrayUtils;
import org.apache.commons.lang3.StringUtils;
import play.data.validation.ValidationError;
import play.libs.Json;

@Singleton
@Slf4j
public class PasswordPolicyService {
  private static final char[] SPECIAL_CHARACTERS =
      "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~".toCharArray();
  private static final String DEFAULT_MIN_LENGTH_PARAM = "yb.pwdpolicy.default_min_length";
  private static final String DEFAULT_MIN_UPPERCASE_PARAM = "yb.pwdpolicy.default_min_uppercase";
  private static final String DEFAULT_MIN_LOWERCASE_PARAM = "yb.pwdpolicy.default_min_lowercase";
  private static final String DEFAULT_MIN_DIGITS_PARAM = "yb.pwdpolicy.default_min_digits";
  private static final String DEFAULT_MIN_SPECIAL_CHAR_PARAM =
      "yb.pwdpolicy.default_min_special_chars";
  private static final String DEFAULT_DISALLOWED_CHARS_PARAM =
      "yb.pwdpolicy.default_disallowed_chars";
  private List<PasswordValidator> validators = new ArrayList<>();

  private final Config config;

  @Inject
  public PasswordPolicyService(Config config) {
    this.config = config;
    validators.add(
        new PasswordComplexityValidator(
            CustomerConfigPasswordPolicyData::getMinLength, c -> true, "characters"));
    validators.add(
        new PasswordComplexityValidator(
            CustomerConfigPasswordPolicyData::getMinUppercase,
            Character::isUpperCase,
            "upper case letters"));
    validators.add(
        new PasswordComplexityValidator(
            CustomerConfigPasswordPolicyData::getMinLowercase,
            Character::isLowerCase,
            "lower case letters"));
    validators.add(
        new PasswordComplexityValidator(
            CustomerConfigPasswordPolicyData::getMinDigits, Character::isDigit, "digits"));
    validators.add(
        new PasswordComplexityValidator(
            CustomerConfigPasswordPolicyData::getMinSpecialCharacters,
            c -> ArrayUtils.contains(SPECIAL_CHARACTERS, c),
            "special characters"));
    validators.add(
        new PasswordDisAllowedCharactersValidators(
            CustomerConfigPasswordPolicyData::getDisallowedCharacters));
  }

  public void checkPasswordPolicy(UUID customerUUID, String password) {
    CustomerConfigPasswordPolicyData effectivePolicy = getCustomerPolicy(customerUUID);

    if (StringUtils.isEmpty(password)) {
      throw new PlatformServiceException(BAD_REQUEST, "Password shouldn't be empty.");
    }

    List<ValidationError> errors =
        validators.stream()
            .map(validator -> validator.validate(password, effectivePolicy))
            .filter(Objects::nonNull)
            .collect(Collectors.toList());

    if (!errors.isEmpty()) {
      String fullMessage =
          errors.stream()
              .map(ValidationError::messages)
              .flatMap(List::stream)
              .collect(Collectors.joining("; "));

      throw new PlatformServiceException(BAD_REQUEST, fullMessage);
    }
  }

  // Method to return the password policy
  public CustomerConfigPasswordPolicyData getPasswordPolicyData(UUID customerUUID) {
    CustomerConfigPasswordPolicyData effectivePolicy = getCustomerPolicy(customerUUID);
    CustomerConfigPasswordPolicyData policyData;
    JsonNode effectivePolicyJson = Json.toJson(effectivePolicy);
    ObjectMapper mapper = new ObjectMapper();

    try {
      policyData = mapper.treeToValue(effectivePolicyJson, CustomerConfigPasswordPolicyData.class);
    } catch (JsonProcessingException e) {
      throw new RuntimeException("Can not pretty print a Json object.");
    }

    return policyData;
  }

  public CustomerConfigPasswordPolicyData getCustomerPolicy(UUID customerUUID) {
    CustomerConfig customerConfig = CustomerConfig.getPasswordPolicyConfig(customerUUID);
    CustomerConfigPasswordPolicyData configuredPolicy =
        customerConfig != null
            ? (CustomerConfigPasswordPolicyData) customerConfig.getDataObject()
            : null;

    CustomerConfigPasswordPolicyData effectivePolicy;

    if (configuredPolicy == null) {
      effectivePolicy = new CustomerConfigPasswordPolicyData();
      effectivePolicy.setMinLength(config.getInt(DEFAULT_MIN_LENGTH_PARAM));
      effectivePolicy.setMinUppercase(config.getInt(DEFAULT_MIN_UPPERCASE_PARAM));
      effectivePolicy.setMinLowercase(config.getInt(DEFAULT_MIN_LOWERCASE_PARAM));
      effectivePolicy.setMinDigits(config.getInt(DEFAULT_MIN_DIGITS_PARAM));
      effectivePolicy.setMinSpecialCharacters(config.getInt(DEFAULT_MIN_SPECIAL_CHAR_PARAM));
      effectivePolicy.setDisallowedCharacters(config.getString(DEFAULT_DISALLOWED_CHARS_PARAM));
    } else {
      effectivePolicy = configuredPolicy;
    }

    return effectivePolicy;
  }
}
