// Copyright (c) Yugabyte, Inc.
package com.yugabyte.yw.rbac.annotations;

import com.yugabyte.yw.rbac.handlers.AuthorizationHandler;
import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;
import play.mvc.With;

@With(AuthorizationHandler.class)
@Retention(RetentionPolicy.RUNTIME)
@Target(ElementType.METHOD)
public @interface AuthzPath {
  RequiredPermissionOnResource[] value() default {};
}
