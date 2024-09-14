// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.password;

import com.yugabyte.yw.models.configs.data.CustomerConfigPasswordPolicyData;
import java.util.function.Function;
import play.data.validation.ValidationError;

public class PasswordDisAllowedCharactersValidators implements PasswordValidator {

  private final Function<CustomerConfigPasswordPolicyData, String> disallowedCharactersExtractor;

  PasswordDisAllowedCharactersValidators(
      Function<CustomerConfigPasswordPolicyData, String> disallowedCharactersExtractor) {
    this.disallowedCharactersExtractor = disallowedCharactersExtractor;
  }

  @Override
  public ValidationError validate(
      String password, CustomerConfigPasswordPolicyData passwordPolicy) {
    String disallowedCharactersString = disallowedCharactersExtractor.apply(passwordPolicy);
    for (char passwordChar : password.toCharArray()) {
      if (disallowedCharactersString.contains(String.valueOf(passwordChar))) {
        return new ValidationError(PASSWORD_FIELD, "Password should not contain " + passwordChar);
      }
    }
    return null;
  }
}
