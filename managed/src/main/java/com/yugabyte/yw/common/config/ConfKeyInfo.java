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
import lombok.Data;

@Data
public class ConfKeyInfo<T> {
  final String key;

  final ScopeType scope;

  final String displayName;

  final String helpTxt;

  final ConfDataType<T> dataType;

  // TODO: anything else we need to add?
}
