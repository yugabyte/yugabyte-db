/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.models.common;

import com.yugabyte.yw.forms.RuntimeConfigFormData.ScopedConfig.ScopeType;
import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;
import play.mvc.With;

@With(YbaApiAction.class)
@Retention(RetentionPolicy.RUNTIME)
@Target({ElementType.METHOD, ElementType.PARAMETER, ElementType.FIELD})
public @interface YbaApi {
  // YBA Version in 0.0.0.0 or 0.0.0.0-b0 format
  public String sinceYBAVersion();

  // Visibility of this API
  public YbaApiVisibility visibility();

  // Optional runtime config flag associated with this API
  public String runtimeConfig() default "";

  public ScopeType runtimeConfigScope() default ScopeType.GLOBAL;

  public enum YbaApiVisibility {
    PUBLIC,
    PREVIEW,
    INTERNAL,
    DEPRECATED
  }
}
