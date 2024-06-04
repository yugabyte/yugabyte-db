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

import com.yugabyte.yw.models.configs.data.CustomerConfigPasswordPolicyData;
import java.util.function.Function;
import java.util.function.Predicate;
import play.data.validation.ValidationError;

class PasswordComplexityValidator implements PasswordValidator {
  private final Function<CustomerConfigPasswordPolicyData, Integer> characterNumberExtractor;
  private final Predicate<Character> characterTypePredicate;
  private final String characterTypeName;

  PasswordComplexityValidator(
      Function<CustomerConfigPasswordPolicyData, Integer> characterNumberExtractor,
      Predicate<Character> characterTypePredicate,
      String characterTypeName) {
    this.characterNumberExtractor = characterNumberExtractor;
    this.characterTypePredicate = characterTypePredicate;
    this.characterTypeName = characterTypeName;
  }

  @Override
  public ValidationError validate(
      String password, CustomerConfigPasswordPolicyData passwordPolicy) {
    int requiredCharacters = characterNumberExtractor.apply(passwordPolicy);
    int foundCharacters = 0;
    for (char passwordChar : password.toCharArray()) {
      if (characterTypePredicate.test(passwordChar)) {
        foundCharacters++;
      }
    }

    if (foundCharacters < requiredCharacters) {
      return new ValidationError(
          PASSWORD_FIELD,
          "Password should contain at least " + requiredCharacters + " " + characterTypeName);
    }
    return null;
  }
}
