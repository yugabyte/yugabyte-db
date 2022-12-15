/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common.config;

import com.yugabyte.yw.forms.RuntimeConfigFormData.ScopedConfig.ScopeType;

public class ProviderConfKeys extends RuntimeConfigKeysModule {

  public static final ConfKeyInfo<Boolean> allowUnsupportedInstances =
      new ConfKeyInfo<>(
          "yb.internal.allow_unsupported_instances",
          ScopeType.PROVIDER,
          "Allow Unsupported Instances",
          "Enabling removes supported instance type filtering on AWS providers.",
          ConfDataType.BooleanType);
  public static final ConfKeyInfo<String> defaultAwsInstanceType =
      new ConfKeyInfo<>(
          "yb.internal.default_aws_instance_type",
          ScopeType.PROVIDER,
          "Default AWS Instance Type",
          "Default AWS Instance Type",
          ConfDataType.StringType);
}
