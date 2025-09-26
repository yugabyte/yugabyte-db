// Copyright (c) YugabyteDB, Inc.
package com.yugabyte.yw.rbac.annotations;

import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;

@Retention(RetentionPolicy.RUNTIME)
public @interface RequiredPermissionOnResource {
  PermissionAttribute requiredPermission();

  Resource resourceLocation();

  boolean checkOnlyPermission() default false;
}
