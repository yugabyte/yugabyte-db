// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.http.conditional;

import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;
import play.mvc.With;

/**
 * Enables HTTP conditional GET (If-None-Match -> 304) for a controller method. The action runs
 * after authentication/RBAC and short-circuits before the controller body when the revision
 * matches.
 */
@With(ConditionalGetAction.class)
@Target(ElementType.METHOD)
@Retention(RetentionPolicy.RUNTIME)
public @interface ConditionalGet {

  /** Guice-bound provider that supplies a cheap resource revision. */
  Class<? extends ResourceRevisionProvider> value();

  /**
   * Path segment that precedes the resource UUID (e.g. {@code universes} for {@code
   * .../customers/{c}/universes/{u}}).
   */
  String resourcePathSegment() default "universes";
}
