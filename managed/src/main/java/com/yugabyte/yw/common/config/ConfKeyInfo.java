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
import java.util.List;
import lombok.Data;

@Data
public class ConfKeyInfo<T> {
  final String key;

  final ScopeType scope;

  final String displayName;

  final String helpTxt;

  final ConfDataType<T> dataType;

  final List<ConfKeyTags> tags;

  public enum ConfKeyTags {
    // Keys Visible on the UI
    PUBLIC,
    // Keys hidden from the UI
    INTERNAL,
    // YBM Keys
    YBM,
    // Keys for which we do not have metadata yet
    BETA,
    // Keys with dedicated UI
    UIDriven,
    // Feature flag keys. Only allowed data type: boolean.
    // These can be viewed without authorising. Should only be set at global scope.
    FEATURE_FLAG
  }
}
