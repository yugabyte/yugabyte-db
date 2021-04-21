/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.yugabyte.yw.models.CustomerConfig;
import play.data.validation.Constraints;

/**
 * This class will be used by the API and UI Form Elements to validate constraints are met
 */
@Constraints.Validate(message = "Minimal length should be not less than the sum of minimal counts" +
  " for upper case, lower case, digits and special characters")
public class PasswordPolicyFormData implements Constraints.Validatable<String> {

  @Constraints.Required(message = "Minimal length is required")
  @Constraints.Min(value = 1, message = "Minimal length should be > 1")
  private int minLength = 8;

  @Constraints.Required(message = "Minimal number of uppercase letters is required")
  @Constraints.Min(value = 1, message = "Minimal number of uppercase letters should be > 0")
  private int minUppercase = 1;

  @Constraints.Required(message = "Minimal number of lowercase letters is required")
  @Constraints.Min(value = 1, message = "Minimal number of lowercase letters should be > 0")
  private int minLowercase = 1;

  @Constraints.Required(message = "Minimal number of digits is required")
  @Constraints.Min(value = 1, message = "Minimal number of digits should be > 0")
  private int minDigits = 1;

  @Constraints.Required(message = "Minimal number of special characters is required")
  @Constraints.Min(value = 1, message = "Minimal number of special characters should be > 0")
  private int minSpecialCharacters = 1;



  public int getMinLength() {
    return minLength;
  }

  public void setMinLength(int minLength) {
    this.minLength = minLength;
  }

  public int getMinUppercase() {
    return minUppercase;
  }

  public void setMinUppercase(int minUppercase) {
    this.minUppercase = minUppercase;
  }

  public int getMinLowercase() {
    return minLowercase;
  }

  public void setMinLowercase(int minLowercase) {
    this.minLowercase = minLowercase;
  }

  public int getMinDigits() {
    return minDigits;
  }

  public void setMinDigits(int minDigits) {
    this.minDigits = minDigits;
  }

  public int getMinSpecialCharacters() {
    return minSpecialCharacters;
  }

  public void setMinSpecialCharacters(int minSpecialCharacters) {
    this.minSpecialCharacters = minSpecialCharacters;
  }

  @Override
  public String validate() {
    if (minUppercase + minLowercase + minDigits + minSpecialCharacters > minLength) {
      return "invalid";
    }
    return null;
  }

  public static PasswordPolicyFormData fromCustomerConfig(CustomerConfig passwordPolicyConfig) {
    if (passwordPolicyConfig == null) {
      return null;
    }
    if (passwordPolicyConfig.type != CustomerConfig.ConfigType.PASSWORD_POLICY) {
      throw new IllegalArgumentException(
        "Received customer config of type " + passwordPolicyConfig.type.name());
    }
    return fromJson(passwordPolicyConfig.data);
  }

  public static PasswordPolicyFormData fromJson(JsonNode passwordPolicyJson) {
    ObjectMapper mapper = new ObjectMapper();
    try {
      return mapper.treeToValue(passwordPolicyJson, PasswordPolicyFormData.class);
    } catch (RuntimeException | JsonProcessingException e) {
      throw new IllegalArgumentException(
        "Failed to parse password policy config json " + passwordPolicyJson, e);
    }
  }
}
