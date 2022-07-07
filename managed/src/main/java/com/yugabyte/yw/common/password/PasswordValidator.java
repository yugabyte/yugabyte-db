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
import play.data.validation.ValidationError;

interface PasswordValidator {
  String PASSWORD_FIELD = "password";

  ValidationError validate(String password, CustomerConfigPasswordPolicyData passwordPolicy);
}
