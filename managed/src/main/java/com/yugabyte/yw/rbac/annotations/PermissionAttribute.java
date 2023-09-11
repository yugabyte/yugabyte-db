// Copyright (c) Yugabyte, Inc.
package com.yugabyte.yw.rbac.annotations;

import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;

@Retention(RetentionPolicy.RUNTIME)
public @interface PermissionAttribute {
  ResourceType resourceType() default ResourceType.OTHER;

  Action action() default Action.READ;
}
